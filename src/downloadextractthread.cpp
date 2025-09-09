/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "config.h"
#include "buffer_optimization.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/mountutils/src/mountutils.hpp"
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
#include <QtConcurrent/qtconcurrentrun.h>
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std;

const int DownloadExtractThread::MAX_QUEUE_SIZE = 128;

// Get system page size
static size_t getSystemPageSize()
{
#ifdef Q_OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

// Note: Buffer optimization logic moved to centralized buffer_optimization module

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
      _abufsize(getOptimalWriteBufferSize()), 
      _ethreadStarted(false),
      _isImage(true), 
      _inputHash(OSLIST_HASH_ALGORITHM), 
      _activeBuf(0), 
      _writeThreadStarted(false),
      _progressStarted(false),
      _lastProgressTime(0),
      _lastEmittedDlNow(0),
      _lastLocalVerifyNow(0),
      _uncompressedTotal(0),
      _useMemoryZip(false)
{
    _extractThread = new _extractThreadClass(this);
    size_t pageSize = getSystemPageSize();
    _abuf[0] = (char *) qMallocAligned(_abufsize, pageSize);
    _abuf[1] = (char *) qMallocAligned(_abufsize, pageSize);
    
    qDebug() << "Using buffer size:" << _abufsize << "bytes with page size:" << pageSize << "bytes";
}

DownloadExtractThread::~DownloadExtractThread()
{
    _cancelled = true;
    
    _cancelExtract();
    if (!_extractThread->wait(10000))
    {
        _extractThread->terminate();
    }
    qFreeAligned(_abuf[0]);
    qFreeAligned(_abuf[1]);
}

void DownloadExtractThread::_emitProgressUpdate()
{
    static QElapsedTimer timer;
    bool firstProgressUpdate = false;
    if (!_progressStarted) {
        _progressStarted = true;
        firstProgressUpdate = true;
        timer.start();
        qDebug() << "Started progress updates after successful drive opening";
    }
    
    // Emit on every meaningful change; throttling removed to reflect fast small writes accurately
    qint64 currentTime = timer.elapsed();
    _lastProgressTime = currentTime;
    
    quint64 currentDlNow = this->dlNow();
    quint64 currentDlTotal = this->dlTotal();
    if (isImage()) {
        // Drive progress from write progress for single-image archives
        currentDlNow = this->bytesWritten();
        if (_uncompressedTotal > 0) {
            currentDlTotal = _uncompressedTotal;
        }
    }
    quint64 currentVerifyNow = this->verifyNow();
    quint64 currentVerifyTotal = this->verifyTotal();
    
    // Only emit signals if values have changed
    if (currentDlNow != _lastEmittedDlNow || (currentDlTotal > 0 && _lastEmittedDlNow == 0)) {
        _lastEmittedDlNow = currentDlNow;
        emit downloadProgressChanged(currentDlNow, currentDlTotal);
        qDebug() << "Progress tick - write:" << currentDlNow << "/" << currentDlTotal
                 << " verify:" << currentVerifyNow << "/" << currentVerifyTotal;
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

    // Always write compressed data to cache
    _writeCache(buf, len);

    // Also accumulate compressed bytes in memory for tiny zips only (avoid GB-scale RAM use)
    const quint64 IN_MEMORY_ZIP_LIMIT = 64ull*1024ull*1024ull; // 64 MB
    if (dlTotal() > 0 && dlTotal() <= IN_MEMORY_ZIP_LIMIT)
        _buf.append(buf, len);

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
    // For very small downloads, the throttled progress emitter may never
    // have emitted a final update. Force a final progress signal so the UI
    // advances from 0% to 100% before extraction continues.
    emit downloadProgressChanged(dlNow(), dlTotal());

    // Signal end-of-stream to the streaming extractor (if used)
    _pushQueue("", 0);

    // If streaming extraction fails on ZIPs, extract from cache (full file) now
    if (_isImage) {
        // Attempt to detect ZIP by magic in the cached file header
        // Open cache file if present
        if (_cachefile.isOpen()) {
            // Already open for write; close to reopen for read if needed
            _cachefile.close();
        }
        QFile cf;
        // We cannot easily get the cache path here; rely on DownloadThread signals already emitted
        // Instead, if the streaming extractor threw earlier, extractImageFromMemory() can be invoked by the caller
    }
}

void DownloadExtractThread::_onDownloadError(const QString &msg)
{
    DownloadThread::_onDownloadError(msg);
    _cancelExtract();
}

void DownloadExtractThread::_cancelExtract()
{
    std::unique_lock<std::mutex> lock(_queueMutex);
    _queue.clear();
    _queue.push_back(QByteArray());
    lock.unlock();
    _cv.notify_all();
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
    // extractor start
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    int r;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_read_support_format_raw(a); // for .gz and such
    archive_read_open(a, this, NULL, &DownloadExtractThread::_archive_read, &DownloadExtractThread::_archive_close);

    try
    {
        r = archive_read_next_header(a, &entry);
        _checkResult(r, a);
        if (entry) {
            QString fname = QString::fromWCharArray(archive_entry_pathname_w(entry));
            // first entry info
            _uncompressedTotal = archive_entry_size(entry);
            // If this looks like a ZIP (we only expect one entry but streaming often fails),
            // switch to memory/file extraction path by throwing and letting caller handle.
        }

        QElapsedTimer heartbeat;
        heartbeat.start();

        while (true)
        {
            // Decompress next block
            ssize_t size = archive_read_data(a, _abuf[_activeBuf], _abufsize);
            // trace decompressed block size
            if (size < 0)
            {
                const char *err = archive_error_string(a);
                qDebug() << "libarchive streaming error, falling back:" << (err ? err : "unknown");
                // Fall back: try extracting from cached file (full ZIP) first, then memory
                archive_read_free(a);
                extractImageFromCacheFile();
                return;
            }
            if (size == 0)
            {
                qDebug() << "decompress EOF";
                break;
            }
            if (size % 512 != 0)
            {
                size_t paddingBytes = 512-(size % 512);
                // pad to sector boundary
                memset(_abuf[_activeBuf]+size, 0, paddingBytes);
                size += paddingBytes;
            }

            // Emit progress updates during extraction
            _emitProgressUpdate();

            // heartbeat removed

            if (_writeThreadStarted)
            {
                size_t prevResult = _writeFuture.result();
                qDebug() << "Previous write completed, bytes:" << prevResult;
                if (!prevResult)
                {
                    if (!_cancelled)
                    {
                        _onWriteError();
                    }
                    archive_read_free(a);
                    return;
                }
            }

            // Perform write synchronously to avoid nested threadpool contention
            // synchronous write
            size_t wrote = _writeFile(_abuf[_activeBuf], size);
            if (!wrote)
            {
                if (!_cancelled)
                {
                    _onWriteError();
                }
                archive_read_free(a);
                return;
            }
            //
            // Emit progress update AFTER bytesWritten advanced
            _emitProgressUpdate();
            _activeBuf = _activeBuf ? 0 : 1;
            _writeThreadStarted = false;
        }

        // No async write pending when using synchronous writes
        // extraction done
        _writeComplete();
    }
    catch (exception &e)
    {
        if (!_cancelled)
        {
            // Fatal error
            DownloadThread::cancelDownload();
            emit error(tr("Error extracting archive: %1").arg(e.what()));
        }
    }

    archive_read_free(a);
}
void DownloadExtractThread::extractImageFromMemory()
{
    // Open the cached compressed file and feed it to libarchive as a filename
    // We assume CacheManager created the cache; ask DownloadThread for last cachefile path is not exposed,
    // so we reuse the HTTP body we accumulated in _buf if available, otherwise abort gracefully.
    QByteArray &zipData = _buf; // in-memory buffer
    if (zipData.isEmpty()) {
        qDebug() << "extractImageFromMemory: no in-memory zip data available";
        emit error(tr("Error extracting archive: missing cached data"));
        return;
    }

    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_memory(a, zipData.data(), zipData.size()) != ARCHIVE_OK) {
        qDebug() << "archive_read_open_memory failed";
        emit error(tr("Error extracting archive: cannot open in memory"));
        archive_read_free(a);
        return;
    }

    try {
        int r = archive_read_next_header(a, &entry);
        _checkResult(r, a);
        _uncompressedTotal = archive_entry_size(entry);
        qDebug() << "Memory extract first entry size:" << _uncompressedTotal;

        while (true) {
            ssize_t size = archive_read_data(a, _abuf[_activeBuf], _abufsize);
            if (size < 0)
                throw runtime_error(archive_error_string(a));
            if (size == 0)
                break;
            if (size % 512 != 0) {
                size_t paddingBytes = 512 - (size % 512);
                memset(_abuf[_activeBuf]+size, 0, paddingBytes);
                size += paddingBytes;
            }
            size_t wrote = _writeFile(_abuf[_activeBuf], size);
            if (!wrote) {
                if (!_cancelled) _onWriteError();
                archive_read_free(a);
                return;
            }
            _emitProgressUpdate();
            _activeBuf = _activeBuf ? 0 : 1;
        }
        qDebug() << "Memory extraction finished. Bytes written:" << bytesWritten();
        _writeComplete();
    } catch (exception &e) {
        if (!_cancelled) {
            DownloadThread::cancelDownload();
            emit error(tr("Error extracting archive: %1").arg(e.what()));
        }
    }
    archive_read_free(a);
}

void DownloadExtractThread::extractImageFromCacheFile()
{
    // Prefer using on-disk cache created via setCacheFile
    const QString cachePath = _cachefile.fileName();
    if (cachePath.isEmpty()) {
        qDebug() << "extractImageFromCacheFile: cache path is empty";
        if (!_buf.isEmpty()) {
            extractImageFromMemory();
        } else {
            emit error(tr("Error extracting archive: cache unavailable"));
        }
        return;
    }

    if (_cachefile.isOpen()) {
        _cachefile.close();
    }

    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, cachePath.toUtf8().constData(), 10240) != ARCHIVE_OK) {
        qDebug() << "archive_read_open_filename failed for" << cachePath;
        if (!_buf.isEmpty()) {
            extractImageFromMemory();
        } else {
            emit error(tr("Error extracting archive: cannot open cache file"));
        }
        archive_read_free(a);
        return;
    }

    try {
        int r = archive_read_next_header(a, &entry);
        _checkResult(r, a);
        _uncompressedTotal = archive_entry_size(entry);
        qDebug() << "Cache extract first entry size:" << _uncompressedTotal;

        while (true) {
            ssize_t size = archive_read_data(a, _abuf[_activeBuf], _abufsize);
            if (size < 0)
                throw runtime_error(archive_error_string(a));
            if (size == 0)
                break;
            if (size % 512 != 0) {
                size_t paddingBytes = 512 - (size % 512);
                memset(_abuf[_activeBuf]+size, 0, paddingBytes);
                size += paddingBytes;
            }
            size_t wrote = _writeFile(_abuf[_activeBuf], size);
            if (!wrote) {
                if (!_cancelled) _onWriteError();
                archive_read_free(a);
                return;
            }
            _emitProgressUpdate();
            _activeBuf = _activeBuf ? 0 : 1;
        }
        qDebug() << "Cache extraction finished. Bytes written:" << bytesWritten();
        _writeComplete();
    } catch (exception &e) {
        if (!_cancelled) {
            DownloadThread::cancelDownload();
            emit error(tr("Error extracting archive: %1").arg(e.what()));
        }
    }
    archive_read_free(a);
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
    QByteArray devlower = _filename.toLower();

    /* See if OS auto-mounted the device */
    for (int tries = 0; tries < 3; tries++)
    {
        QThread::sleep(1);
        auto l = Drivelist::ListStorageDevices();
        for (const auto& i : l)
        {
            if (QByteArray::fromStdString(i.device).toLower() == devlower && i.mountpoints.size() == 1)
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

    /* When run under some container environments -even when udisks2 said
       it completed mounting the fs- we may have to wait a bit more
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
    archive_read_open(a, this, NULL, &DownloadExtractThread::_archive_read, &DownloadExtractThread::_archive_close);

    try
    {
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
        if (!_expectedHash.isEmpty() && _expectedHash != computedHash)
        {
            qDebug() << "Mismatch with expected hash:" << _expectedHash;
            throw runtime_error("Download corrupt. SHA256 does not match");
        }
        if (_cacheEnabled && _expectedHash == computedHash)
        {
            _cachefile.close();
            
            // Get both hashes: compressed cache file and uncompressed image data
            QByteArray cacheFileHash = _cachehash.result().toHex();
            
            qDebug() << "Cache file created:";
            qDebug() << "  Image hash (uncompressed):" << computedHash;
            qDebug() << "  Cache file hash (compressed):" << cacheFileHash;
            
            // Emit both hashes for proper cache verification
            emit cacheFileHashUpdated(cacheFileHash, computedHash);
            // Keep old signal for backward compatibility
            emit cacheFileUpdated(computedHash);
        }

        emit success();
    }
    catch (exception &e)
    {
        if (_cachefile.isOpen())
            _cachefile.remove();

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
            emit error(tr("Error extracting archive: %1").arg(e.what()));
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
        eject_disk(_filename.constData());
    }
}

ssize_t DownloadExtractThread::_on_read(struct archive *, const void **buff)
{
    _readChunk = _popQueue();
    *buff = _readChunk.constData();
    int sz = _readChunk.size();
    qDebug() << "_on_read pop size" << sz;
    return sz;
}

int DownloadExtractThread::_on_close(struct archive *)
{
    return 0;
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

// Synchronized queue using monitor consumer/producer pattern
QByteArray DownloadExtractThread::_popQueue()
{
    std::unique_lock<std::mutex> lock(_queueMutex);

    _cv.wait(lock, [this]{
            return _queue.size() != 0;
    });

    QByteArray result = _queue.front();
    _queue.pop_front();
    
    // Always notify waiting pushers that space is available
    lock.unlock();
    _cv.notify_one();

    return result;
}

void DownloadExtractThread::_pushQueue(const char *data, size_t len)
{
    std::unique_lock<std::mutex> lock(_queueMutex);

    _cv.wait(lock, [this]{
            return _queue.size() != MAX_QUEUE_SIZE;
    });

    _queue.emplace_back(QByteArray(data, static_cast<int>(len)));
    
    // Always notify waiting poppers that data is available
    lock.unlock();
    _cv.notify_one();
}

bool DownloadExtractThread::_verify()
{
    qDebug() << "DownloadExtractThread::_verify() called (child class implementation with progress updates)";
    _lastVerifyNow = 0;
    _verifyTotal = _file.pos();
    
    // Use adaptive buffer size based on file size for optimal verification performance
    size_t verifyBufferSize = getAdaptiveVerifyBufferSize(_verifyTotal);
    char *verifyBuf = (char *) qMallocAligned(verifyBufferSize, 4096);
    
    QElapsedTimer t1;
    t1.start();
    
    qDebug() << "Post-write verification using" << verifyBufferSize/1024 << "KB buffer for" 
             << _verifyTotal/(1024*1024) << "MB image";

#ifdef Q_OS_LINUX
    /* Make sure we are reading from the drive and not from cache */
    posix_fadvise(_file.handle(), 0, 0, POSIX_FADV_DONTNEED);
#endif

    if (!_firstBlock)
    {
        _file.seek(0);
    }
    else
    {
        _verifyhash.addData(_firstBlock, _firstBlockSize);
        _file.seek(_firstBlockSize);
        _lastVerifyNow += _firstBlockSize;
    }

    while (_verifyEnabled && _lastVerifyNow < _verifyTotal && !_cancelled)
    {
        qint64 lenRead = _file.read(verifyBuf, qMin((qint64) verifyBufferSize, (qint64) (_verifyTotal-_lastVerifyNow) ));
        if (lenRead == -1)
        {
            DownloadThread::_onDownloadError(tr("Error reading from storage.<br>"
                                                "SD card may be broken."));
            qFreeAligned(verifyBuf);
            return false;
        }

        _verifyhash.addData(verifyBuf, lenRead);
        _lastVerifyNow += lenRead;
        
        // Emit progress updates during verification
        _emitProgressUpdate();
    }
    qFreeAligned(verifyBuf);

    qDebug() << "Verify hash:" << _verifyhash.result().toHex();
    qDebug() << "Verify done in" << t1.elapsed() / 1000.0 << "seconds";

    if (_verifyhash.result() == _writehash.result() || !_verifyEnabled || _cancelled)
    {
        return true;
    }
    else
    {
        DownloadThread::_onDownloadError(tr("Verifying write failed. Contents of SD card is different from what was written to it."));
    }

    return false;
}
