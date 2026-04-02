/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "fastbootflashthread.h"
#include "rpiboot/libusb_transport.h"
#include "fastboot/fastboot_protocol.h"
#include "fastboot/sparse_encoder.h"
#include "fastboot/bmap.h"
#include "curlnetworkconfig.h"
#include "acceleratedcryptographichash.h"
#include "ringbuffer.h"
#include "systemmemorymanager.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QScopeGuard>

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include <cstring>
#include <thread>

// Use the canonical FASTBOOT_VID/PID from rpiboot_types.h
using rpiboot::FASTBOOT_VID;
using rpiboot::FASTBOOT_PID;

// Default max-download-size if the device doesn't report one
static constexpr uint32_t DEFAULT_MAX_DOWNLOAD_SIZE = 256 * 1024 * 1024;  // 256 MB

FastbootFlashThread::FastbootFlashThread(const QString& fastbootId,
                                           const QString& blockDevice,
                                           const QUrl& imageUrl,
                                           quint64 downloadLen,
                                           quint64 extractLen,
                                           const QByteArray& expectedHash,
                                           QObject* parent)
    : QThread(parent)
    , _fastbootId(fastbootId)
    , _blockDevice(blockDevice)
    , _imageUrl(imageUrl)
    , _downloadLen(downloadLen)
    , _extractLen(extractLen)
    , _expectedHash(expectedHash)
{
}

FastbootFlashThread::~FastbootFlashThread()
{
    cancel();
    if (!wait(10000)) {
        // Thread is stuck — most likely deadlocked in libusb_exit on macOS
        // (libusb bug: darwin_exit holds active_contexts_lock while waiting
        // for the IONotification run loop, which itself needs that lock to
        // process a pending IOKit callback).  Since we are in the destructor
        // path and the process will exit shortly, force-kill the thread.
        // QThread::~QThread() calls qFatal() if the thread is still running,
        // so we must wait until the OS has confirmed it is gone.
        qWarning() << "FastbootFlashThread: thread did not finish within 10s after cancel, terminating";
        terminate();
        wait();
    }
}

void FastbootFlashThread::cancel()
{
    _cancelled.store(true);
    if (_compressedRing)
        _compressedRing->cancel();
    if (_decompressedRing)
        _decompressedRing->cancel();
}

void FastbootFlashThread::setImageCustomisation(const QByteArray &config,
                                                  const QByteArray &cmdline,
                                                  const QByteArray &firstrun,
                                                  const QByteArray &cloudinit,
                                                  const QByteArray &cloudinitNetwork,
                                                  const QByteArray &initFormat)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudinitNetwork;
    _initFormat = initFormat;
}

bool FastbootFlashThread::applyCustomisation(fastboot::FastbootProtocol& fb,
                                              rpiboot::IUsbTransport& transport)
{
    bool hasCustomisation = !_config.isEmpty() || !_cmdline.isEmpty() ||
                            !_firstrun.isEmpty() || !_cloudinit.isEmpty() ||
                            !_cloudinitNetwork.isEmpty();
    if (!hasCustomisation || _initFormat.isEmpty())
        return true;

    emit preparationStatusUpdate(tr("Applying OS customisation..."));
    qDebug() << "applyCustomisation: initFormat=" << _initFormat
             << "config=" << _config.size() << "bytes"
             << "cmdline=" << _cmdline.size() << "bytes"
             << "cloudinit=" << _cloudinit.size() << "bytes";

    // Mount the boot partition on the device so file I/O commands can
    // reach the filesystem we just flashed.
    static const std::string MOUNTPOINT = "/mnt/bootfs";
    std::string bootPartition = _blockDevice.toStdString() + "p1";

    if (!fb.mountDevice(transport, bootPartition, MOUNTPOINT, "vfat")) {
        emit error(tr("Failed to mount boot partition: %1")
                   .arg(QString::fromStdString(fb.lastError())));
        return false;
    }

    // All file paths are relative to the mountpoint
    static const std::string BOOT = MOUNTPOINT + "/";

    // Ensure umount happens even on early return
    auto umountGuard = qScopeGuard([&] {
        if (!fb.umountDevice(transport, MOUNTPOINT)) {
            qWarning() << "applyCustomisation: umount failed:"
                       << QString::fromStdString(fb.lastError());
        }
    });

    // Helper: QByteArray → span
    auto toSpan = [](const QByteArray& ba) {
        return std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(ba.constData()),
            static_cast<size_t>(ba.size()));
    };

    // ── config.txt: read, uncomment/append entries, write back ──
    if (!_config.isEmpty()) {
        auto configData = fb.readDeviceFile(transport, BOOT + "config.txt", _cancelled);
        if (configData.empty()) {
            emit error(tr("Failed to read config.txt: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }
        QByteArray config(reinterpret_cast<const char*>(configData.data()),
                          static_cast<int>(configData.size()));

        auto items = _config.split('\n');
        items.removeAll("");
        for (const QByteArray& item : std::as_const(items)) {
            if (config.contains("#" + item)) {
                config.replace("#" + item, item);
            } else if (!config.contains("\n" + item)) {
                if (config.right(1) != "\n")
                    config += "\n";
                config += item + "\n";
            }
        }

        if (!fb.writeDeviceFile(transport, BOOT + "config.txt", toSpan(config), _cancelled)) {
            emit error(tr("Failed to write config.txt: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }
    }

    // ── firstrun.sh (systemd format) ──
    QByteArray cmdlineAppend = _cmdline;
    if (!_firstrun.isEmpty() && _initFormat == "systemd") {
        if (!fb.writeDeviceFile(transport, BOOT + "firstrun.sh", toSpan(_firstrun), _cancelled)) {
            emit error(tr("Failed to write firstrun.sh: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }
        cmdlineAppend += " systemd.run=/boot/firstrun.sh"
                         " systemd.run_success_action=reboot"
                         " systemd.unit=kernel-command-line.target";
    }

    // ── cloud-init files ──
    // Only write meta-data and the ds=nocloud cmdline when there is actual
    // cloud-init content.  A stale cmdline entry (e.g. recommendedWifiCountry)
    // alone should not trigger cloud-init file writes.
    bool initCloud = (_initFormat == "cloudinit" || _initFormat == "cloudinit-rpi");
    bool hasCloudContent = !_cloudinit.isEmpty() || !_cloudinitNetwork.isEmpty();
    if (initCloud && hasCloudContent) {
        QByteArray instanceId = "rpi-imager-" + QByteArray::number(QDateTime::currentMSecsSinceEpoch());
        QByteArray metadata = "instance-id: " + instanceId + "\n";
        if (!fb.writeDeviceFile(transport, BOOT + "meta-data", toSpan(metadata), _cancelled)) {
            emit error(tr("Failed to write meta-data: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }

        cmdlineAppend += " ds=nocloud;i=" + instanceId;

        if (!_cloudinit.isEmpty()) {
            QByteArray userData = "#cloud-config\n" + _cloudinit;
            if (!fb.writeDeviceFile(transport, BOOT + "user-data", toSpan(userData), _cancelled)) {
                emit error(tr("Failed to write user-data: %1")
                           .arg(QString::fromStdString(fb.lastError())));
                return false;
            }
        }

        if (!_cloudinitNetwork.isEmpty()) {
            if (!fb.writeDeviceFile(transport, BOOT + "network-config",
                                     toSpan(_cloudinitNetwork), _cancelled)) {
                emit error(tr("Failed to write network-config: %1")
                           .arg(QString::fromStdString(fb.lastError())));
                return false;
            }
        }
    }

    // ── cmdline.txt: read, append, write back ──
    if (!cmdlineAppend.isEmpty()) {
        auto cmdlineData = fb.readDeviceFile(transport, BOOT + "cmdline.txt", _cancelled);
        if (cmdlineData.empty()) {
            emit error(tr("Failed to read cmdline.txt: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }
        QByteArray cmdline = QByteArray(reinterpret_cast<const char*>(cmdlineData.data()),
                                         static_cast<int>(cmdlineData.size())).trimmed();
        cmdline += cmdlineAppend;

        if (!fb.writeDeviceFile(transport, BOOT + "cmdline.txt", toSpan(cmdline), _cancelled)) {
            emit error(tr("Failed to write cmdline.txt: %1")
                       .arg(QString::fromStdString(fb.lastError())));
            return false;
        }
    }

    qDebug() << "FastbootFlashThread: customisation applied successfully";
    return true;
}

// ── Curl write callback → compressed ring buffer ─────────────────────
struct CurlWriteContext {
    RingBuffer* ring;
    RingBuffer::Slot* currentSlot;
    size_t slotOffset;
    std::atomic<bool>* cancelled;
};

static size_t curlWriteToRing(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *ctx = static_cast<CurlWriteContext*>(userdata);
    size_t totalSize = size * nmemb;
    size_t offset = 0;

    while (offset < totalSize) {
        if (ctx->cancelled->load())
            return 0;  // Abort transfer

        // Acquire a slot if we don't have one
        if (!ctx->currentSlot) {
            ctx->currentSlot = ctx->ring->acquireWriteSlot(100);
            if (!ctx->currentSlot) {
                if (ctx->ring->isCancelled() || ctx->cancelled->load())
                    return 0;
                continue;  // Retry
            }
            ctx->slotOffset = 0;
        }

        // Fill the current slot
        size_t space = ctx->currentSlot->capacity - ctx->slotOffset;
        size_t chunk = std::min(totalSize - offset, space);
        memcpy(ctx->currentSlot->data + ctx->slotOffset, ptr + offset, chunk);
        ctx->slotOffset += chunk;
        offset += chunk;

        // Commit if full
        if (ctx->slotOffset >= ctx->currentSlot->capacity) {
            ctx->ring->commitWriteSlot(ctx->currentSlot, ctx->slotOffset);
            ctx->currentSlot = nullptr;
            ctx->slotOffset = 0;
        }
    }

    return totalSize;
}

// ── Curl progress callback ───────────────────────────────────────────
struct CurlProgressContext {
    std::atomic<quint64>* dlnow;
    std::atomic<quint64>* dltotal;
    std::atomic<bool>* cancelled;
};

static int curlProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                 curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto *ctx = static_cast<CurlProgressContext*>(clientp);
    if (ctx->cancelled->load())
        return 1;  // Abort
    ctx->dlnow->store(static_cast<quint64>(dlnow));
    ctx->dltotal->store(static_cast<quint64>(dltotal));
    return 0;
}

// ── Thread 1: Download producer ──────────────────────────────────────
void FastbootFlashThread::downloadProducer()
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        _downloadError = tr("Failed to initialize curl");
        _compressedRing->cancel();
        return;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    CurlNetworkConfig::instance().applyCurlSettings(
        curl, CurlNetworkConfig::FetchProfile::LargeFile, errorBuffer);

    QByteArray urlBytes = _imageUrl.toEncoded();
    curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());

    CurlWriteContext writeCtx{_compressedRing.get(), nullptr, 0, &_cancelled};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToRing);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeCtx);

    CurlProgressContext progressCtx{&_dlnow, &_dltotal, &_cancelled};
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressCtx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);

    // Commit any partially-filled slot
    if (writeCtx.currentSlot && writeCtx.slotOffset > 0) {
        _compressedRing->commitWriteSlot(writeCtx.currentSlot, writeCtx.slotOffset);
    }

    curl_easy_cleanup(curl);

    if (_cancelled.load()) {
        _compressedRing->cancel();
        return;
    }

    if (res != CURLE_OK) {
        _downloadError = errorBuffer[0]
            ? QString::fromUtf8(errorBuffer)
            : QString::fromUtf8(curl_easy_strerror(res));
        _compressedRing->cancel();
        return;
    }

    _compressedRing->producerDone();
}

// ── libarchive read callback ← compressed ring buffer ────────────────
struct ArchiveReadContext {
    RingBuffer* ring;
    RingBuffer::Slot* currentSlot;
};

static ssize_t archiveReadFromRing(struct archive *, void *clientData, const void **buffer)
{
    auto *ctx = static_cast<ArchiveReadContext*>(clientData);

    // Release previous slot
    if (ctx->currentSlot) {
        ctx->ring->releaseReadSlot(ctx->currentSlot);
        ctx->currentSlot = nullptr;
    }

    // Acquire next slot
    ctx->currentSlot = ctx->ring->acquireReadSlot(100);
    while (!ctx->currentSlot && !ctx->ring->isCancelled() && !ctx->ring->isComplete()) {
        ctx->currentSlot = ctx->ring->acquireReadSlot(100);
    }

    if (!ctx->currentSlot) {
        *buffer = nullptr;
        return 0;  // EOF or cancelled
    }

    *buffer = ctx->currentSlot->data;
    return static_cast<ssize_t>(ctx->currentSlot->size);
}

static int archiveCloseRing(struct archive *, void *clientData)
{
    auto *ctx = static_cast<ArchiveReadContext*>(clientData);
    if (ctx->currentSlot) {
        ctx->ring->releaseReadSlot(ctx->currentSlot);
        ctx->currentSlot = nullptr;
    }
    return ARCHIVE_OK;
}

// ── Thread 2: Decompress consumer/producer ───────────────────────────
void FastbootFlashThread::decompressConsumerProducer()
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_raw(a);
    archive_read_support_format_all(a);

    ArchiveReadContext readCtx{_compressedRing.get(), nullptr};

    int r = archive_read_open(a, &readCtx, nullptr, archiveReadFromRing, archiveCloseRing);
    if (r != ARCHIVE_OK) {
        _decompressError = QString::fromUtf8(archive_error_string(a));
        archive_read_free(a);
        _decompressedRing->cancel();
        return;
    }

    struct archive_entry *entry;
    if (archive_read_next_header(a, &entry) != ARCHIVE_OK) {
        _decompressError = tr("No entries in image archive");
        archive_read_free(a);
        _decompressedRing->cancel();
        return;
    }

    // Read decompressed data and push into the decompressed ring buffer
    const void *buff;
    size_t size;
    la_int64_t offset;
    while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
        if (_cancelled.load())
            break;

        // Push decompressed data into the output ring in slot-sized chunks
        size_t pos = 0;
        while (pos < size && !_cancelled.load()) {
            auto *slot = _decompressedRing->acquireWriteSlot(100);
            if (!slot) {
                if (_decompressedRing->isCancelled() || _cancelled.load())
                    break;
                continue;
            }

            size_t chunk = std::min(size - pos, slot->capacity);
            memcpy(slot->data, static_cast<const char*>(buff) + pos, chunk);
            _decompressedRing->commitWriteSlot(slot, chunk);
            pos += chunk;
        }
    }

    archive_read_free(a);

    if (_cancelled.load()) {
        _decompressedRing->cancel();
        return;
    }

    _decompressedRing->producerDone();
}

// ── Main thread: Flash consumer ──────────────────────────────────────
void FastbootFlashThread::run()
{
    try {
        runImpl();
    } catch (const std::exception& e) {
        qCritical() << "FastbootFlashThread: uncaught exception:" << e.what();
        emit error(tr("Fastboot error: %1").arg(QString::fromUtf8(e.what())));
    } catch (...) {
        qCritical() << "FastbootFlashThread: unknown uncaught exception";
        emit error(tr("Fastboot error: unexpected internal error"));
    }
}

void FastbootFlashThread::runImpl()
{
    emit preparationStatusUpdate(tr("Connecting to fastboot device..."));

    // 1. Open the fastboot USB device
    QElapsedTimer deviceOpenTimer;
    deviceOpenTimer.start();

    rpiboot::LibusbContext ctx;
    std::unique_ptr<rpiboot::LibusbTransport> transport;

    rpiboot::UsbDeviceInfo targetInfo{};
    targetInfo.vendorId = FASTBOOT_VID;
    targetInfo.productId = FASTBOOT_PID;

    // Parse bus:addr from _fastbootId
    auto parts = _fastbootId.split(':');
    if (parts.size() >= 2) {
        targetInfo.busNumber = static_cast<uint8_t>(parts[0].toUInt());
        targetInfo.deviceAddress = static_cast<uint8_t>(parts[1].toUInt());
    }

    transport = ctx.openDevice(targetInfo);
    if (!transport || !transport->isOpen()) {
        emit eventFastbootDeviceOpen(static_cast<quint32>(deviceOpenTimer.elapsed()), false, _fastbootId);
        emit error(tr("Failed to open fastboot device: %1").arg(_fastbootId));
        return;
    }

    // 2. Query max-download-size
    fastboot::FastbootProtocol fb;
    auto maxDlSizeStr = fb.getVar(*transport, "max-download-size");
    uint32_t maxDownloadSize = DEFAULT_MAX_DOWNLOAD_SIZE;
    if (maxDlSizeStr) {
        try {
            std::string val = *maxDlSizeStr;
            if (val.starts_with("0x") || val.starts_with("0X"))
                maxDownloadSize = static_cast<uint32_t>(std::stoul(val, nullptr, 16));
            else
                maxDownloadSize = static_cast<uint32_t>(std::stoul(val));
        } catch (...) {
            qDebug() << "FastbootFlashThread: could not parse max-download-size:"
                     << QString::fromStdString(*maxDlSizeStr)
                     << "using default" << maxDownloadSize;
        }
    }
    qDebug() << "FastbootFlashThread: max-download-size =" << maxDownloadSize;
    emit eventFastbootDeviceOpen(static_cast<quint32>(deviceOpenTimer.elapsed()), true,
                                 QStringLiteral("max-download-size=%1").arg(maxDownloadSize));

    // 3. Allocate ring buffers
    size_t inputSlotSize, writeSlotSize, inputSlots, writeSlots;
    SystemMemoryManager::instance().getCoordinatedRingBufferConfig(
        1024 * 1024,   // 1MB input slot hint (compressed data)
        16 * 1024 * 1024, // 16MB write slot hint (fastboot download unit)
        inputSlots, writeSlots,
        inputSlotSize, writeSlotSize);

    _compressedRing = std::make_unique<RingBuffer>(inputSlots, inputSlotSize);
    _decompressedRing = std::make_unique<RingBuffer>(writeSlots, writeSlotSize);

    // 4. Initialize incremental hash if verification is needed
    if (!_expectedHash.isEmpty())
        _imageHash = std::make_unique<AcceleratedCryptographicHash>(QCryptographicHash::Sha256);

    // 5. Start pipeline threads
    emit writing();
    emit preparationStatusUpdate(tr("Downloading and flashing OS image..."));

    std::thread downloadThread([this]() { downloadProducer(); });
    std::thread decompressThread([this]() { decompressConsumerProducer(); });

    // 6. Flash consumer loop — sparse-encode and stream to device
    //
    // The sparse encoder converts the raw decompressed image into Android
    // sparse image segments.  When a bmap is available, unmapped blocks
    // become DONT_CARE (skipped entirely).  Otherwise zero-filled regions
    // are FILL(0) to guarantee correctness on non-erased storage.
    fastboot::SparseEncoder sparse(maxDownloadSize, _extractLen);

    // Fetch and apply optional block map
    if (!_bmapUrl.isEmpty()) {
        emit preparationStatusUpdate(tr("Fetching block map..."));
        qDebug() << "FastbootFlashThread: fetching bmap from" << _bmapUrl;

        QByteArray bmapData;
        {
            // Synchronous fetch — bmap files are tiny (few KB)
            CURL *curl = curl_easy_init();
            if (curl) {
                std::string responseData;
                auto writeCallback = +[](char *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
                    auto *resp = static_cast<std::string*>(userdata);
                    resp->append(ptr, size * nmemb);
                    return size * nmemb;
                };
                curl_easy_setopt(curl, CURLOPT_URL, _bmapUrl.toString().toUtf8().constData());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
                CURLcode res = curl_easy_perform(curl);
                curl_easy_cleanup(curl);

                if (res == CURLE_OK)
                    bmapData = QByteArray::fromStdString(responseData);
                else
                    qWarning() << "FastbootFlashThread: bmap fetch failed:" << curl_easy_strerror(res);
            }
        }

        if (!bmapData.isEmpty()) {
            auto blockMap = std::make_unique<fastboot::BlockMap>();
            std::string parseError;
            if (blockMap->parse(std::string_view(bmapData.constData(), bmapData.size()), &parseError)) {
                qDebug() << "FastbootFlashThread: bmap loaded —"
                         << blockMap->mappedBlockCount() << "of"
                         << blockMap->blockCount() << "blocks mapped ("
                         << (100 * blockMap->mappedBlockCount() / std::max<uint64_t>(blockMap->blockCount(), 1))
                         << "%)";
                sparse.setBlockMap(std::move(blockMap));
            } else {
                qWarning() << "FastbootFlashThread: bmap parse failed:" << QString::fromStdString(parseError);
            }
        }
    }
    quint64 totalFed = 0;
    bool flashError = false;

    // Helper: send one sparse segment via download + flash
    uint32_t segmentIndex = 0;
    auto sendSegment = [&](std::span<const uint8_t> seg) -> bool {
        rpiboot::ProgressCallback progressCb = [this, totalFed](
            uint64_t current, uint64_t /*total*/, const std::string&) {
            emit writeProgress(totalFed,
                               _extractLen > 0 ? _extractLen : totalFed);
        };

        qDebug() << "FastbootFlashThread: downloading segment" << segmentIndex
                 << "(" << seg.size() << "bytes)";

        if (!fb.download(*transport, seg, progressCb, _cancelled)) {
            qDebug() << "FastbootFlashThread: download FAILED for segment"
                     << segmentIndex << ":" << QString::fromStdString(fb.lastError());
            if (!_cancelled.load())
                emit error(tr("Fastboot download failed: %1")
                           .arg(QString::fromStdString(fb.lastError())));
            return false;
        }

        qDebug() << "FastbootFlashThread: flashing segment" << segmentIndex;
        if (!fb.flash(*transport, _blockDevice.toStdString(), 120000)) {
            qDebug() << "FastbootFlashThread: flash FAILED for segment"
                     << segmentIndex << ":" << QString::fromStdString(fb.lastError());
            if (!_cancelled.load())
                emit error(tr("Fastboot flash failed: %1")
                           .arg(QString::fromStdString(fb.lastError())));
            return false;
        }

        qDebug() << "FastbootFlashThread: segment" << segmentIndex << "OK";
        ++segmentIndex;
        return true;
    };

    while (!_cancelled.load()) {
        auto *slot = _decompressedRing->acquireReadSlot(100);
        if (!slot) {
            if (_decompressedRing->isCancelled())
                break;
            if (_decompressedRing->isComplete())
                break;
            continue;
        }

        // Incremental hash on raw decompressed data (before sparse encoding)
        if (_imageHash)
            _imageHash->addData(slot->data, static_cast<int>(slot->size));

        totalFed += slot->size;

        // Feed data to the sparse encoder, draining completed segments
        // as they appear.  feed() returns fewer bytes than offered when
        // a segment is ready, so we loop until all data is consumed.
        const auto* feedPtr = reinterpret_cast<const uint8_t*>(slot->data);
        size_t feedRemaining = slot->size;
        while (feedRemaining > 0 && !flashError) {
            size_t consumed = sparse.feed(feedPtr, feedRemaining);
            feedPtr += consumed;
            feedRemaining -= consumed;

            fastboot::SparseEncoder::SegmentStats segStats;
            auto seg = sparse.takeSegment(&segStats);
            if (!seg.empty()) {
                qDebug() << "FastbootFlashThread: segment" << segmentIndex
                         << "blocks=" << segStats.blocks
                         << "(raw=" << segStats.rawBlocks
                         << "fill=" << segStats.fillBlocks
                         << "skip=" << segStats.dontCareBlocks << ")"
                         << "chunks=" << segStats.chunks
                         << "wire=" << segStats.wireBytes << "bytes";
                if (!sendSegment(seg)) {
                    flashError = true;
                } else {
                    emit writeProgress(totalFed,
                                       _extractLen > 0 ? _extractLen : totalFed);
                    emit downloadProgress(_dlnow.load(), _dltotal.load());
                }
            }
        }
        _decompressedRing->releaseReadSlot(slot);

        if (flashError)
            break;
    }

    // Finish the sparse stream and drain all remaining segments.
    // finish() may need multiple calls when the trailing partial block
    // triggers a segment split.
    while (!flashError && !_cancelled.load()) {
        sparse.finish();
        fastboot::SparseEncoder::SegmentStats segStats;
        auto seg = sparse.takeSegment(&segStats);
        if (seg.empty())
            break;
        qDebug() << "FastbootFlashThread: final segment" << segmentIndex
                 << "blocks=" << segStats.blocks
                 << "(raw=" << segStats.rawBlocks
                 << "fill=" << segStats.fillBlocks
                 << "skip=" << segStats.dontCareBlocks << ")"
                 << "wire=" << segStats.wireBytes << "bytes";
        if (!sendSegment(seg))
            flashError = true;
    }

    if (!flashError && !_cancelled.load()) {
        qDebug() << "Sparse stats: raw=" << sparse.rawBlockCount()
                 << "fill=" << sparse.fillBlockCount()
                 << "dontcare=" << sparse.dontCareBlockCount()
                 << "total=" << sparse.totalBlocksProcessed();
    }

    qDebug() << "FastbootFlashThread: flash loop ended —"
             << "flashError=" << flashError
             << "cancelled=" << _cancelled.load()
             << "totalFed=" << totalFed
             << "segments=" << segmentIndex;

    // 7. Wait for pipeline threads to finish
    if (_cancelled.load()) {
        _compressedRing->cancel();
        _decompressedRing->cancel();
    }

    downloadThread.join();
    decompressThread.join();

    // Check for pipeline errors
    if (!_downloadError.isEmpty() && !_cancelled.load()) {
        emit error(tr("Download failed: %1").arg(_downloadError));
        return;
    }
    if (!_decompressError.isEmpty() && !_cancelled.load()) {
        emit error(tr("Decompression failed: %1").arg(_decompressError));
        return;
    }

    if (_cancelled.load()) {
        emit error(tr("Cancelled"));
        return;
    }

    // A flash error was already reported via error() from sendSegment().
    // Do not fall through to hash verification / customisation / success().
    if (flashError)
        return;

    // 8. Verify hash
    if (_imageHash) {
        QByteArray actualHash = _imageHash->result().toHex();
        if (actualHash != _expectedHash) {
            emit error(tr("Image hash mismatch. Expected: %1 Got: %2")
                       .arg(QString::fromLatin1(_expectedHash),
                            QString::fromLatin1(actualHash)));
            return;
        }
    }

    // 9. Apply OS customisation via fastboot file transfer
    qDebug() << "FastbootFlashThread: applying OS customisation...";
    if (!applyCustomisation(fb, *transport)) {
        qDebug() << "FastbootFlashThread: customisation FAILED";
        return;
    }
    qDebug() << "FastbootFlashThread: customisation OK";

    // 10. Finalize
    emit finalizing();

    auto resp = fb.sendCommand(*transport, "reboot", 10000);
    if (resp.type != fastboot::Response::Okay) {
        qDebug() << "FastbootFlashThread: reboot command returned:"
                 << QString::fromStdString(resp.message);
        // Non-fatal — flash succeeded even if reboot fails
    }

    qDebug() << "FastbootFlashThread: flash complete, total" << totalFed << "bytes";
    emit success();
}
