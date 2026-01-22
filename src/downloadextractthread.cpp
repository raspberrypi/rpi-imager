/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "config.h"
#include "platformquirks.h"
#include "systemmemorymanager.h"
#include "drivelist/drivelist.h"
#include <iostream>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;

// Ring buffer slot count is now determined dynamically by SystemMemoryManager
const int DownloadExtractThread::RING_BUFFER_SLOTS = 0;  // Placeholder, actual value set at runtime

// Buffer optimization logic now handled by centralized SystemMemoryManager

class _extractThreadClass : public QThread {
public:
    _extractThreadClass(DownloadExtractThread *parent)
        : QThread(parent), _de(parent)
    {
    }

    virtual void run()
    {
        if (_de->isImage())
            _de->extractImageRun();
        else
            _de->extractMultiFileRun();
    }

protected:
    DownloadExtractThread *_de;
};

DownloadExtractThread::DownloadExtractThread(const QByteArray &url, const QByteArray &localfilename, const QByteArray &expectedHash, QObject *parent)
    : DownloadThread(url, localfilename, expectedHash, parent), 
      _writeBufferSize(SystemMemoryManager::instance().getOptimalWriteBufferSize()), 
      _currentReadSlot(nullptr),
      _currentWriteSlot(nullptr),
      _ethreadStarted(false),
      _isImage(true), 
      _inputHash(OSLIST_HASH_ALGORITHM), 
      _progressStarted(false),
      _lastProgressTime(0),
      _lastEmittedDlNow(0),
      _lastLocalVerifyNow(0),
      _lastEmittedDecompressNow(0),
      _lastEmittedWriteNow(0),
      _bytesDecompressed(0),
      _downloadComplete(false),
      _totalDecompressionMs(0),
      _totalRingBufferWaitMs(0),
      _bytesReadFromRingBuffer(0)
{
    _extractThread = new _extractThreadClass(this);
    size_t pageSize = SystemMemoryManager::instance().getSystemPageSize();
    
    // Get optimal buffer slot sizes (hints based on total system memory)
    size_t inputBufferSizeHint = SystemMemoryManager::instance().getOptimalInputBufferSize();
    size_t writeBufferSizeHint = _writeBufferSize;  // Already set from getOptimalWriteBufferSize()
    
    // Use COORDINATED ring buffer allocation to prevent memory exhaustion
    // This ensures both ring buffers together fit within 30% of available memory.
    // Buffer sizes may be scaled down on low-memory systems while maintaining
    // page alignment for I/O efficiency.
    size_t inputSlots, writeSlots;
    size_t actualInputSize, actualWriteSize;
    size_t totalMemory = SystemMemoryManager::instance().getCoordinatedRingBufferConfig(
        inputBufferSizeHint, writeBufferSizeHint, 
        inputSlots, writeSlots,
        actualInputSize, actualWriteSize);
    
    // Update write buffer size if it was scaled down
    if (actualWriteSize != _writeBufferSize) {
        qDebug() << "Write buffer size adjusted:" << _writeBufferSize << "->" << actualWriteSize;
        _writeBufferSize = actualWriteSize;
    }
    
    // Create zero-copy ring buffer for curl -> libarchive data transfer (compressed data)
    _ringBuffer = std::make_unique<RingBuffer>(inputSlots, actualInputSize, pageSize);
    
    // Create ring buffer for decompress -> write path (decompressed data)
    _writeRingBuffer = std::make_unique<RingBuffer>(writeSlots, actualWriteSize, pageSize);
    
    qDebug() << "Using buffer size:" << _writeBufferSize << "bytes with page size:" << pageSize << "bytes";
    qDebug() << "Input ring buffer:" << inputSlots << "slots of" << actualInputSize << "bytes";
    qDebug() << "Write ring buffer:" << writeSlots << "slots of" << actualWriteSize << "bytes";
    qDebug() << "Total ring buffer memory:" << (totalMemory / (1024 * 1024)) << "MB";
}

DownloadExtractThread::~DownloadExtractThread()
{
    _cancelled = true;
    
    _cancelExtract();
    if (!_extractThread->wait(10000))
    {
        _extractThread->terminate();
    }
    
    // Wait for any pending async writes before destroying ring buffers
    // The async completion callbacks reference the ring buffer, so we must
    // ensure they've all completed before destruction
    if (_file && _file->IsAsyncIOSupported()) {
        _file->WaitForPendingWrites();
    }
    
    // Ring buffer destructors handle memory cleanup
    _writeRingBuffer.reset();
    _ringBuffer.reset();
}

void DownloadExtractThread::_emitProgressUpdate()
{
    bool firstProgressUpdate = false;
    if (!_progressStarted) {
        _progressStarted = true;
        firstProgressUpdate = true;
        _sessionTimer.start();
        
        // Set session timer on ring buffers for stall event timestamps
        if (_ringBuffer) {
            _ringBuffer->setSessionTimer(&_sessionTimer);
        }
        if (_writeRingBuffer) {
            _writeRingBuffer->setSessionTimer(&_sessionTimer);
        }
        
        qDebug() << "Started progress updates after successful drive opening";
    }
    
    // Only emit progress updates every 100ms to avoid flooding (but always emit the first one)
    qint64 currentTime = _sessionTimer.elapsed();
    if (!firstProgressUpdate && currentTime - _lastProgressTime < PROGRESS_UPDATE_INTERVAL) {
        return;
    }
    _lastProgressTime = currentTime;
    
    // Emit any pending ring buffer stall events for time-series correlation
    // Input ring buffer (download -> decompress)
    if (_ringBuffer) {
        auto stallEvents = _ringBuffer->getPendingStallEvents();
        for (const auto& event : stallEvents) {
            QString metadata = QString("buffer: input; type: %1; duration_ms: %2")
                .arg(event.isProducer ? "producer_stall" : "consumer_stall")
                .arg(event.durationMs);
            emit eventRingBufferStats(event.timestampMs, event.durationMs, metadata);
        }
    }
    
    // Write ring buffer (decompress -> write)
    if (_writeRingBuffer) {
        auto stallEvents = _writeRingBuffer->getPendingStallEvents();
        for (const auto& event : stallEvents) {
            QString metadata = QString("buffer: write; type: %1; duration_ms: %2")
                .arg(event.isProducer ? "producer_stall" : "consumer_stall")
                .arg(event.durationMs);
            emit eventRingBufferStats(event.timestampMs, event.durationMs, metadata);
        }
    }
    
    quint64 currentDlNow = this->dlNow();
    quint64 currentDlTotal = this->dlTotal();
    quint64 currentExtractTotal = this->extractTotal();
    quint64 currentVerifyNow = this->verifyNow();
    quint64 currentVerifyTotal = this->verifyTotal();
    quint64 currentDecompressNow = _bytesDecompressed.load();
    quint64 currentWriteNow = this->bytesWritten();
    
    // For write progress, use extract size (uncompressed) if set, otherwise fall back to download size
    quint64 writeTotal = currentExtractTotal > 0 ? currentExtractTotal : currentDlTotal;
    
    // Only emit signals if values have changed
    if (currentDlNow != _lastEmittedDlNow || (currentDlTotal > 0 && _lastEmittedDlNow == 0)) {
        _lastEmittedDlNow = currentDlNow;
        emit downloadProgressChanged(currentDlNow, currentDlTotal);
    }
    
    if (currentDecompressNow != _lastEmittedDecompressNow) {
        _lastEmittedDecompressNow = currentDecompressNow;
        emit decompressProgressChanged(currentDecompressNow, writeTotal);
    }
    
    if (currentWriteNow != _lastEmittedWriteNow) {
        _lastEmittedWriteNow = currentWriteNow;
        emit writeProgressChanged(currentWriteNow, writeTotal);
    }
    
    if (currentVerifyNow != _lastLocalVerifyNow || (currentVerifyTotal > 0 && _lastLocalVerifyNow == 0)) {
        _lastLocalVerifyNow = currentVerifyNow;
        emit verifyProgressChanged(currentVerifyNow, currentVerifyTotal);
    }
}

size_t DownloadExtractThread::_writeData(const char *buf, size_t len)
{
    if (_cancelled)
        return 0;

    // Emit progress updates when data starts flowing
    _emitProgressUpdate();

    _writeCache(buf, len);

    if (!_ethreadStarted)
    {
        // Extract thread is started when first data comes in
        _ethreadStarted = true;
        _extractThread->start();
        msleep(100);
    }

    if (!_isImage)
    {
        _inputHash.addData(buf, len);
    }

    _pushQueue(buf, len);

    return len;
}

void DownloadExtractThread::_onDownloadSuccess()
{
    _downloadComplete = true;
    
    // Signal ring buffer that producer is done
    if (_ringBuffer) {
        _ringBuffer->producerDone();
    }
    
    // Wait for extraction thread to finish processing all data
    _extractThread->wait();
    
    // Extraction thread already called _writeComplete(), so just emit success to signal thread completion
    emit success();
}

void DownloadExtractThread::_onDownloadError(const QString &msg)
{
    DownloadThread::_onDownloadError(msg);
    _cancelExtract();
}

void DownloadExtractThread::_cancelExtract()
{
    if (_ringBuffer) {
        _ringBuffer->cancel();
    }
}

void DownloadExtractThread::cancelDownload()
{
    DownloadThread::cancelDownload();
    _cancelExtract();
}

// Raise exception on libarchive errors
static inline void _checkResult(int r, struct archive *a)
{
    if (r == ARCHIVE_FATAL)
    {
        // Fatal
        throw runtime_error(archive_error_string(a));
    }
    if (r < ARCHIVE_OK)
    {
        // Non-fatal (e.g., WARN, RETRY): log but do not abort
        qDebug() << archive_error_string(a);
    }
}

// libarchive thread
void DownloadExtractThread::extractImageRun()
{
    QElapsedTimer extractionTimer;
    extractionTimer.start();
    
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    int r;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a); // for .gz and such
    
    // Configure decompression options for optimal performance
    // Note: These options are hints - libarchive ignores unsupported ones
    _configureArchiveOptions(a);
    
    archive_read_open(a, this, NULL, &DownloadExtractThread::_archive_read, &DownloadExtractThread::_archive_close);

    try
    {
        r = archive_read_next_header(a, &entry);
        _checkResult(r, a);
        
        // Log the compression filter(s) being used for diagnostics
        _logCompressionFilters(a);
        
        // Emit image extraction setup event (archive opened and header read)
        emit eventImageExtraction(static_cast<quint32>(extractionTimer.elapsed()), true);

        // Timer for pipeline instrumentation
        QElapsedTimer decompressTimer;
        
        while (true)
        {
            // Acquire a slot from the write ring buffer
            // This blocks if all slots are in use (back-pressure from slow writes or async I/O)
            RingBuffer::Slot* slot = _writeRingBuffer->acquireWriteSlot(100);
            while (!slot && !_cancelled && !_writeRingBuffer->isCancelled() && !_writeRingBuffer->isStallTimeoutExceeded()) {
                // CRITICAL: Poll for async I/O completions while waiting for ring buffer slots!
                // Without this, we deadlock: slots are freed by async write callbacks,
                // but callbacks only fire when we poll IOCP. If we're blocked here not
                // polling, completions pile up and slots never get freed.
                if (_file && _file->IsAsyncIOSupported()) {
                    _file->PollAsyncCompletions();
                }
                slot = _writeRingBuffer->acquireWriteSlot(100);
            }
            if (!slot) {
                if (_cancelled) break;
                if (_writeRingBuffer->isStallTimeoutExceeded()) {
                    // Ring buffer stall timeout - record event and emit a clear error message
                    RingBuffer::StallType stallType = _writeRingBuffer->getStallType();
                    qDebug() << "DownloadExtractThread: Write ring buffer stall timeout:" << RingBuffer::stallTypeToString(stallType);
                    
                    // Emit a ring buffer stall event
                    qint64 timestampMs = _sessionTimer.isValid() ? _sessionTimer.elapsed() : 0;
                    QString metadata = QString("buffer: write; type: stall_timeout; stall_type: %1").arg(RingBuffer::stallTypeToString(stallType));
                    emit eventRingBufferStats(timestampMs, 30000, metadata);  // 30s stall timeout
                    
                    // Convert stall type to user-facing message
                    QString errorMsg = tr("The write operation has stalled.\n\n"
                                         "No data has been written for 30 seconds. "
                                         "This could be caused by:\n"
                                         "• Storage device disconnected or unresponsive\n"
                                         "• Device has failed or is faulty\n"
                                         "• System resource exhaustion\n\n"
                                         "Please check the storage device and try again.");
                    throw runtime_error(errorMsg.toStdString());
                }
                throw runtime_error(tr("Failed to acquire write buffer slot").toStdString());
            }
            
            // Time decompression (includes ring buffer wait inside libarchive's read callback)
            decompressTimer.start();
            ssize_t size = archive_read_data(a, slot->data, slot->capacity);
            _totalDecompressionMs.fetch_add(static_cast<quint64>(decompressTimer.elapsed()));
            
            if (size < 0) {
                const char* errorStr = archive_error_string(a);
                
                // Release the slot we acquired but won't use
                _writeRingBuffer->releaseReadSlot(slot);
                
                // Check if this is the expected "No progress is possible" error after download completion
                if (size == ARCHIVE_FATAL && errorStr && strstr(errorStr, "No progress is possible")) {
                    break;
                }
                
                throw runtime_error(errorStr);
            }
            if (size == 0) {
                // Release the slot we acquired but won't use
                _writeRingBuffer->releaseReadSlot(slot);
                break;
            }
            if (size % 512 != 0)
            {
                size_t paddingBytes = 512-(size % 512);
                qDebug() << "Image is NOT a valid disk image, as its length is not a multiple of the sector size of 512 bytes long";
                qDebug() << "Last write() would be" << size << "bytes, but padding to" << size + paddingBytes << "bytes";
                memset(slot->data + size, 0, paddingBytes);
                size += paddingBytes;
            }
            
            // Track decompressed bytes
            _bytesDecompressed.fetch_add(static_cast<quint64>(size));

            // Emit progress updates during extraction
            _emitProgressUpdate();

            // Create a completion callback that releases the ring buffer slot
            // This enables ZERO-COPY async I/O: the slot stays valid until the
            // async write truly completes, then is returned to the pool.
            // Capture slot and buffer pointers by value for the callback.
            RingBuffer* ringBuf = _writeRingBuffer.get();
            RingBuffer::Slot* slotToRelease = slot;
            DownloadThread::WriteCompleteCallback releaseCallback = [ringBuf, slotToRelease]() {
                ringBuf->releaseReadSlot(slotToRelease);
            };
            
            // IMPORTANT: Call _writeFile directly from extraction thread instead of via
            // QtConcurrent::run(). Using the thread pool causes deadlock when async I/O
            // is enabled: _writeFile waits for previous hash computation, but hash runs
            // in the same thread pool. With many queued _writeFile calls, all pool threads
            // block waiting for hashes that can't run (no available threads).
            //
            // With async I/O, _writeFile returns quickly after queuing the I/O operation,
            // so running it synchronously in the extraction thread doesn't block progress.
            // The actual I/O happens asynchronously via io_uring/IOCP.
            bool writeOk = _writeFile(slot->data, static_cast<size_t>(size), releaseCallback) > 0;
            if (!writeOk && !_cancelled) {
                // Wait for pending async writes before cleanup
                if (_file && _file->IsAsyncIOSupported()) {
                    _file->WaitForPendingWrites();
                }
                _onWriteError();
                archive_read_free(a);
                return;
            }
        }

        _writeComplete();
    }
    catch (exception &e)
    {
        // Wait for pending async writes before cleanup
        // Their callbacks reference the ring buffer, so we must wait
        if (_file && _file->IsAsyncIOSupported()) {
            _file->WaitForPendingWrites();
        }
        
        if (!_cancelled)
        {
            // Fatal error
            DownloadThread::cancelDownload();
            
            // Use stall error message if set (from ring buffer stall), otherwise use exception message
            if (!_stallErrorMessage.isEmpty()) {
                emit error(_stallErrorMessage);
            } else {
                emit error(tr("Error extracting archive: %1").arg(e.what()));
            }
        }
    }

    archive_read_free(a);
    
    // Emit pipeline timing summary events for performance analysis
    // These show where time was spent in the extraction pipeline
    emit eventPipelineDecompressionTime(
        static_cast<quint32>(_totalDecompressionMs.load()),
        _bytesDecompressed.load());
    emit eventPipelineRingBufferWaitTime(
        static_cast<quint32>(_totalRingBufferWaitMs.load()),
        _bytesReadFromRingBuffer.load());
    
    qDebug() << "Pipeline timing summary:"
             << "decompress=" << _totalDecompressionMs.load() << "ms"
             << "(ring_wait=" << _totalRingBufferWaitMs.load() << "ms)";
    
    // Emit detailed write timing breakdown for hypothesis testing
    _emitWriteTimingStats();
    
    // Log and emit write ring buffer statistics
    if (_writeRingBuffer) {
        uint64_t producerStalls, consumerStalls, producerWaitMs, consumerWaitMs;
        _writeRingBuffer->getStarvationStats(producerStalls, consumerStalls, producerWaitMs, consumerWaitMs);
        if (producerStalls > 0 || consumerStalls > 0) {
            qDebug() << "Write ring buffer stats:"
                     << "producer stalls:" << producerStalls << "(" << producerWaitMs << "ms),"
                     << "consumer stalls:" << consumerStalls << "(" << consumerWaitMs << "ms)";
        }
        // Emit for performance tracking even if no stalls (shows buffer was used)
        emit eventWriteRingBufferStats(producerStalls, consumerStalls, producerWaitMs, consumerWaitMs);
    }
}

#ifdef Q_OS_LINUX
/* Returns true if folder lives on a different device than parent directory */
inline bool isMountPoint(const QString &folder)
{
    struct stat statFolder, statParent;
    QFileInfo fi(folder);
    QByteArray folderAscii = folder.toLatin1();
    QByteArray parentDir   = fi.dir().path().toLatin1();

    if ( ::stat(folderAscii.constData(), &statFolder) == -1
         || ::stat(parentDir.constData(), &statParent) == -1)
    {
        return false;
    }

    return (statFolder.st_dev != statParent.st_dev);
}
#endif

void DownloadExtractThread::extractMultiFileRun()
{
    QString folder;
    QStringList filesExtracted, dirExtracted;
    // Use canonical path for comparison since drivelist returns /dev/disk, not /dev/rdisk
    QByteArray canonicalDevice = PlatformQuirks::getEjectDevicePath(_filename).toLower().toUtf8();

    /* See if OS auto-mounted the device */
    for (int tries = 0; tries < 3; tries++)
    {
        QThread::sleep(1);
        auto l = Drivelist::ListStorageDevices();
        for (const auto& i : l)
        {
            if (QByteArray::fromStdString(i.device).toLower() == canonicalDevice && i.mountpoints.size() == 1)
            {
                folder = QByteArray::fromStdString(i.mountpoints.front());
                break;
            }
        }
    }

#ifdef Q_OS_LINUX
    bool manualmount = false;

    if (folder.isEmpty())
    {
        /* Manually mount folder */
        QTemporaryDir td;
        QStringList args;
        folder = td.path();
        QByteArray fatpartition = _filename;
        if (isdigit(fatpartition.at(fatpartition.length()-1)))
            fatpartition += "p1";
        else
            fatpartition += "1";
        args << "-t" << "vfat" << fatpartition << folder;

        if (QProcess::execute("mount", args) != 0)
        {
            emit error(tr("Error mounting FAT32 partition"));
            return;
        }
        td.setAutoRemove(false);
        manualmount = true;
    }

    /* When run under some container environments, we may have to wait a bit more
       until mountpoint is available in sandbox which lags behind */
    for (int tries=0; tries<3; tries++)
    {
        if (isMountPoint(folder))
            break;
        QThread::sleep(1);
    }
#endif

    if (folder.isEmpty())
    {
        emit error(tr("Operating system did not mount FAT32 partition"));
        return;
    }

    QString currentDir = QDir::currentPath();

    if (!QDir::setCurrent(folder))
    {
        DownloadThread::cancelDownload();
        emit error(tr("Error changing to directory '%1'").arg(folder));
        return;
    }

    // Now create libarchive handles after all early returns are handled
    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    struct archive_entry *entry;
    /* Extra safety checks: do not allow existing files to be overwritten (SD card should be formatted by previous step),
     * do not allow absolute paths, do not allow insecure symlinks, no special permissions */
    int r, flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS
            | ARCHIVE_EXTRACT_SECURE_NODOTDOT | ARCHIVE_EXTRACT_SECURE_SYMLINKS | ARCHIVE_EXTRACT_NO_OVERWRITE
            /*ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS | ARCHIVE_EXTRACT_XATTR*/;
#ifndef Q_OS_WIN
    if (::getuid() == 0)
        flags |= ARCHIVE_EXTRACT_OWNER;
#endif

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_write_disk_set_options(ext, flags);
    
    // Configure decompression options for optimal performance
    _configureArchiveOptions(a);
    
    archive_read_open(a, this, NULL, &DownloadExtractThread::_archive_read, &DownloadExtractThread::_archive_close);

    try
    {
        // Log the compression filter(s) being used
        _logCompressionFilters(a);
        while ( (r = archive_read_next_header(a, &entry)) != ARCHIVE_EOF)
        {
          _checkResult(r, a);
          r = archive_write_header(ext, entry);
          if (r < ARCHIVE_OK)
              qDebug() << archive_error_string(ext);
          else if (archive_entry_size(entry) > 0)
          {
              //checkResult(copyData(a, ext), a);
              const void *buff;
              size_t size;
              int64_t offset;
              QString filename = QString::fromWCharArray(archive_entry_pathname_w(entry));

              if (archive_entry_filetype(entry) == AE_IFDIR) // Empty directory
                  dirExtracted.append(filename);
              else
                  filesExtracted.append(filename);

              while ( (r = archive_read_data_block(a, &buff, &size, &offset)) != ARCHIVE_EOF)
              {
                  _checkResult(r, a);
                  _checkResult(archive_write_data_block(ext, buff, size, offset), ext);
                  _bytesWritten += size;
              }
          }
          _checkResult(archive_write_finish_entry(ext), ext);
        }

        QByteArray computedHash = _inputHash.result().toHex();
        qDebug() << "Hash of compressed multi-file zip:" << computedHash;
        if (!_cancelled && !_expectedHash.isEmpty() && _expectedHash != computedHash)
        {
            qDebug() << "Mismatch with expected hash:" << _expectedHash;
            throw runtime_error("Download corrupt. SHA256 does not match");
        }
        if (_cacheEnabled && _expectedHash == computedHash)
        {
            // Finish async cache writer (waits for all pending writes to complete)
            if (_asyncCacheWriter && _asyncCacheWriter->isActive()) {
                _asyncCacheWriter->finish();
                
                // Get cache file hash from async writer
                QByteArray cacheFileHash = _asyncCacheWriter->hash();
                
                qDebug() << "Cache file created (async):";
                qDebug() << "  Image hash (uncompressed):" << computedHash;
                qDebug() << "  Cache file hash (compressed):" << cacheFileHash;
                
                // Emit both hashes for proper cache verification
                emit cacheFileHashUpdated(cacheFileHash, computedHash);
                // Keep old signal for backward compatibility
                emit cacheFileUpdated(computedHash);
            }
        }

        emit success();
    }
    catch (exception &e)
    {
        // Cancel async cache writer (this will remove the cache file)
        if (_asyncCacheWriter) {
            _asyncCacheWriter->cancel();
        }

        qDebug() << "Deleting extracted files";
        for (const auto& filename : filesExtracted)
        {
            QFileInfo fi(filename);
            QString path = fi.path();
            if (!path.isEmpty() && path != "." && !dirExtracted.contains(path))
                dirExtracted.append(path);

            QFile::remove(filename);
        }
        for (int idx = dirExtracted.count()-1; idx >= 0; idx--)
        {
            QDir d;
            d.rmdir(dirExtracted[idx]);
        }
        qDebug() << filesExtracted << dirExtracted;

        if (!_cancelled)
        {
            /* Fatal error */
            DownloadThread::cancelDownload();
            
            // Use stall error message if set (from ring buffer stall), otherwise use exception message
            if (!_stallErrorMessage.isEmpty()) {
                emit error(_stallErrorMessage);
            } else {
                emit error(tr("Error extracting archive: %1").arg(e.what()));
            }
        }
    }

    // Ensure proper cleanup sequence
    
    // 1. Close libarchive handles properly (this should flush any pending writes)
    if (archive_write_close(ext) != ARCHIVE_OK) {
        qDebug() << "Warning: Failed to properly close archive write handle";
    }
    archive_read_free(a);
    archive_write_free(ext);
    
    // 2. Change back to original directory BEFORE sync to avoid holding references
    QDir::setCurrent(currentDir);
    
    // 3. Force filesystem sync to ensure all writes are committed
#ifdef Q_OS_LINUX
    sync(); // Force all cached writes to be flushed to disk
    qDebug() << "Filesystem sync completed";
#endif

#ifdef Q_OS_LINUX
    if (manualmount)
    {
        QStringList args;
        args << folder;
        int umountResult = QProcess::execute("umount", args);
        QDir d;
        bool rmResult = d.rmdir(folder);
        qDebug() << "Manual cleanup: umount result:" << umountResult << ", rmdir result:" << rmResult << ", folder:" << folder;
    }
#endif

    // Give the filesystem a moment to settle after sync before ejecting
    QThread::msleep(500);

    if (_ejectEnabled)
    {
        // Use canonical device path for eject (e.g., /dev/disk on macOS, not rdisk)
        QString ejectPath = PlatformQuirks::getEjectDevicePath(_filename);
        PlatformQuirks::ejectDisk(ejectPath);
    }
}

ssize_t DownloadExtractThread::_on_read(struct archive *, const void **buff)
{
    if (!_ringBuffer) {
        *buff = nullptr;
        return 0;
    }
    
    // Release previous slot if any (it's been consumed by libarchive)
    if (_currentReadSlot) {
        _ringBuffer->releaseReadSlot(_currentReadSlot);
        _currentReadSlot = nullptr;
    }
    
    // Time how long we wait for ring buffer data
    QElapsedTimer ringBufferWaitTimer;
    ringBufferWaitTimer.start();
    
    // Acquire next read slot (blocks until data available or producer done)
    _currentReadSlot = _ringBuffer->acquireReadSlot(100);  // 100ms timeout
    
    // Handle timeout - retry, but also check for stall timeout
    while (!_currentReadSlot && !_ringBuffer->isCancelled() && !_ringBuffer->isComplete() && !_ringBuffer->isStallTimeoutExceeded()) {
        _currentReadSlot = _ringBuffer->acquireReadSlot(100);
    }
    
    // Check for stall timeout (network stalled for too long)
    if (_ringBuffer->isStallTimeoutExceeded()) {
        RingBuffer::StallType stallType = _ringBuffer->getStallType();
        qDebug() << "DownloadExtractThread: Input ring buffer stall timeout:" << RingBuffer::stallTypeToString(stallType);
        
        // Emit a ring buffer stall event
        qint64 timestampMs = _sessionTimer.isValid() ? _sessionTimer.elapsed() : 0;
        QString metadata = QString("buffer: input; type: stall_timeout; stall_type: %1").arg(RingBuffer::stallTypeToString(stallType));
        emit eventRingBufferStats(timestampMs, 30000, metadata);  // 30s stall timeout
        
        // Set error message for user - this is a consumer stall (waiting for download data)
        _stallErrorMessage = tr("The download has stalled.\n\n"
                               "No data received for 30 seconds. "
                               "This could be caused by:\n"
                               "• Network connection lost or unstable\n"
                               "• Remote server became unresponsive\n"
                               "• Firewall or proxy blocking the connection\n\n"
                               "Please check your network connection and try again.");
        
        *buff = nullptr;
        return -1;  // Signal error to libarchive
    }
    
    // Record ring buffer wait time
    _totalRingBufferWaitMs.fetch_add(static_cast<quint64>(ringBufferWaitTimer.elapsed()));
    
    // Check for EOF or cancellation
    if (!_currentReadSlot) {
        *buff = nullptr;
        return 0;  // EOF or cancelled
    }
    
    // Track bytes read from ring buffer
    _bytesReadFromRingBuffer.fetch_add(static_cast<quint64>(_currentReadSlot->size));
    
    // Return pointer directly to pre-allocated buffer (zero-copy!)
    *buff = _currentReadSlot->data;
    return static_cast<ssize_t>(_currentReadSlot->size);
}

int DownloadExtractThread::_on_close(struct archive *)
{
    // Release final read slot if any
    if (_currentReadSlot && _ringBuffer) {
        _ringBuffer->releaseReadSlot(_currentReadSlot);
        _currentReadSlot = nullptr;
    }
    return 0;
}

void DownloadExtractThread::_configureArchiveOptions(struct archive *a)
{
    // Get number of CPU cores for multi-threading hints
    int numCores = QThread::idealThreadCount();
    if (numCores < 1) numCores = 1;
    if (numCores > 8) numCores = 8;  // Cap at 8 to avoid excessive memory usage
    
    QString threadsStr = QString::number(numCores);
    QByteArray threadsBytes = threadsStr.toLatin1();
    
    // XZ/LZMA: Enable multi-threaded decoding if file has multiple blocks
    // Note: Only works if the .xz file was compressed with block threading
    // libarchive 3.3+ supports "threads" option for xz filter
    int ret = archive_read_set_option(a, "xz", "threads", threadsBytes.constData());
    if (ret == ARCHIVE_OK) {
        qDebug() << "XZ multi-threaded decoding enabled with" << numCores << "threads";
    }
    
    // Also try lzma filter (some files use raw lzma)
    archive_read_set_option(a, "lzma", "threads", threadsBytes.constData());
    
    // ZSTD: Standard decompression is single-threaded by design
    // The zstd format requires sequential block processing due to dictionary dependencies
    // However, we can hint for optimal buffer sizing
    // Note: As of libarchive 3.8, there's no "threads" option for zstd decompression
    
    // Gzip: Single-threaded, no threading options available
    // The gzip format doesn't support parallel decompression
}

void DownloadExtractThread::_logCompressionFilters(struct archive *a)
{
    // Log all active filters for diagnostics
    int filterCount = archive_filter_count(a);
    if (filterCount <= 1) {
        qDebug() << "No compression filter detected (raw or uncompressed)";
        return;
    }
    
    QStringList filters;
    for (int i = 0; i < filterCount; i++) {
        const char* name = archive_filter_name(a, i);
        if (name && strcmp(name, "none") != 0) {
            filters << QString::fromUtf8(name);
        }
    }
    
    if (!filters.isEmpty()) {
        qDebug() << "Decompression pipeline:" << filters.join(" -> ");
        
        // Provide performance hints based on compression format
        if (filters.contains("xz") || filters.contains("lzma")) {
            qDebug() << "XZ/LZMA: Multi-threaded decode enabled if file has multiple blocks";
        } else if (filters.contains("zstd")) {
            qDebug() << "ZSTD: Using single-threaded streaming decompression (format limitation)";
        } else if (filters.contains("gzip")) {
            qDebug() << "GZIP: Using single-threaded decompression (format limitation)";
        }
    }
}

// static callback functions that call object oriented equivalents
ssize_t DownloadExtractThread::_archive_read(struct archive *a, void *client_data, const void **buff)
{
   return qobject_cast<DownloadExtractThread *>((QObject *) client_data)->_on_read(a, buff);
}

int DownloadExtractThread::_archive_close(struct archive *a, void *client_data)
{
   return qobject_cast<DownloadExtractThread *>((QObject *) client_data)->_on_close(a);
}

bool DownloadExtractThread::isImage()
{
    return _isImage;
}

void DownloadExtractThread::enableMultipleFileExtraction()
{
    _isImage = false;
}

void DownloadExtractThread::_pushQueue(const char *data, size_t len)
{
    if (!_ringBuffer || _cancelled) {
        return;
    }
    
    // Handle data larger than slot capacity by chunking
    size_t offset = 0;
    while (offset < len && !_cancelled) {
        // Acquire a write slot (blocks if buffer is full)
        RingBuffer::Slot* slot = _ringBuffer->acquireWriteSlot(100);  // 100ms timeout
        if (!slot) {
            if (_ringBuffer->isCancelled() || _cancelled) {
                return;
            }
            // Poll for async I/O completions while waiting (prevents deadlock)
            if (_file && _file->IsAsyncIOSupported()) {
                _file->PollAsyncCompletions();
            }
            // Check for stall timeout (disk writes stalled for too long)
            if (_ringBuffer->isStallTimeoutExceeded()) {
                qDebug() << "DownloadExtractThread: Write ring buffer stall timeout in _pushQueue";
                return;  // Let the caller handle the error
            }
            // Timeout - try again
            continue;
        }
        
        // Copy data directly into the pre-allocated slot buffer (zero-copy from slot's perspective)
        size_t chunkSize = std::min(len - offset, slot->capacity);
        memcpy(slot->data, data + offset, chunkSize);
        
        // Commit the slot
        _ringBuffer->commitWriteSlot(slot, chunkSize);
        offset += chunkSize;
    }
}

void DownloadExtractThread::_onVerifyProgress()
{
    // Emit progress updates during verification
    _emitProgressUpdate();
}
