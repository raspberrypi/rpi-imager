/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "fastbootflashthread.h"
#include "rpiboot/libusb_transport.h"
#include "fastboot/fastboot_protocol.h"
#include "curlnetworkconfig.h"
#include "acceleratedcryptographichash.h"
#include "ringbuffer.h"
#include "systemmemorymanager.h"

#include <QDebug>

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include <cstring>
#include <thread>

// Fastboot VID/PID for the RPi fastboot gadget
static constexpr uint16_t FASTBOOT_VID = 0x18d1;  // Google
static constexpr uint16_t FASTBOOT_PID = 0x4ee0;  // Fastboot

// Default max-download-size if the device doesn't report one
static constexpr uint32_t DEFAULT_MAX_DOWNLOAD_SIZE = 256 * 1024 * 1024;  // 256 MB

FastbootFlashThread::FastbootFlashThread(const QString& fastbootId,
                                           const QUrl& imageUrl,
                                           quint64 downloadLen,
                                           quint64 extractLen,
                                           const QByteArray& expectedHash,
                                           QObject* parent)
    : QThread(parent)
    , _fastbootId(fastbootId)
    , _imageUrl(imageUrl)
    , _downloadLen(downloadLen)
    , _extractLen(extractLen)
    , _expectedHash(expectedHash)
{
}

FastbootFlashThread::~FastbootFlashThread()
{
    cancel();
    if (!wait(5000))
        terminate();
}

void FastbootFlashThread::cancel()
{
    _cancelled.store(true);
    if (_compressedRing)
        _compressedRing->cancel();
    if (_decompressedRing)
        _decompressedRing->cancel();
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
    emit preparationStatusUpdate(tr("Connecting to fastboot device..."));

    // 1. Open the fastboot USB device
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
    emit preparationStatusUpdate(tr("Downloading and flashing OS image..."));

    std::thread downloadThread([this]() { downloadProducer(); });
    std::thread decompressThread([this]() { decompressConsumerProducer(); });

    // 6. Flash consumer loop (runs in this thread)
    quint64 totalFlashed = 0;

    while (!_cancelled.load()) {
        auto *slot = _decompressedRing->acquireReadSlot(100);
        if (!slot) {
            if (_decompressedRing->isCancelled()) {
                // Pipeline error — check for error messages from producer threads
                break;
            }
            if (_decompressedRing->isComplete())
                break;  // All data consumed
            continue;
        }

        // Incremental hash
        if (_imageHash)
            _imageHash->addData(slot->data, static_cast<int>(slot->size));

        // Download this chunk to the device
        auto slotSpan = std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(slot->data), slot->size);

        rpiboot::ProgressCallback progressCb = [this, totalFlashed](
            uint64_t current, uint64_t /*total*/, const std::string&) {
            emit writeProgress(totalFlashed + current,
                               _extractLen > 0 ? _extractLen : totalFlashed + current);
        };

        if (!fb.download(*transport, slotSpan, progressCb, _cancelled)) {
            _decompressedRing->releaseReadSlot(slot);
            if (!_cancelled.load())
                emit error(tr("Fastboot download failed: %1")
                           .arg(QString::fromStdString(fb.lastError())));
            break;
        }

        // Flash the downloaded chunk
        if (!fb.flash(*transport, "0", 120000)) {
            _decompressedRing->releaseReadSlot(slot);
            if (!_cancelled.load())
                emit error(tr("Fastboot flash failed: %1")
                           .arg(QString::fromStdString(fb.lastError())));
            break;
        }

        totalFlashed += slot->size;
        _decompressedRing->releaseReadSlot(slot);

        emit writeProgress(totalFlashed,
                           _extractLen > 0 ? _extractLen : totalFlashed);

        // Emit download progress too
        emit downloadProgress(_dlnow.load(), _dltotal.load());
    }

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

    // 9. Finalize
    emit finalizing();

    auto resp = fb.sendCommand(*transport, "reboot", 10000);
    if (resp.type != fastboot::Response::Okay) {
        qDebug() << "FastbootFlashThread: reboot command returned:"
                 << QString::fromStdString(resp.message);
        // Non-fatal — flash succeeded even if reboot fails
    }

    qDebug() << "FastbootFlashThread: flash complete, total" << totalFlashed << "bytes";
    emit success();
}
