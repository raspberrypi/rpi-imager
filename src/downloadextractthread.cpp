/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "config.h"
#include "systemmemorymanager.h"
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
      _abufsize(SystemMemoryManager::instance().getOptimalWriteBufferSize()), 
      _ethreadStarted(false),
      _isImage(true), 
      _inputHash(OSLIST_HASH_ALGORITHM), 
      _activeBuf(0), 
      _writeThreadStarted(false),
      _progressStarted(false),
      _lastProgressTime(0),
      _lastEmittedDlNow(0),
      _lastLocalVerifyNow(0),
      _downloadComplete(false)
{
    _extractThread = new _extractThreadClass(this);
    size_t pageSize = SystemMemoryManager::instance().getSystemPageSize();
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
    
    // Only emit progress updates every 100ms to avoid flooding (but always emit the first one)
    qint64 currentTime = timer.elapsed();
    if (!firstProgressUpdate && currentTime - _lastProgressTime < PROGRESS_UPDATE_INTERVAL) {
        return;
    }
    _lastProgressTime = currentTime;
    
    quint64 currentDlNow = this->dlNow();
    quint64 currentDlTotal = this->dlTotal();
    quint64 currentVerifyNow = this->verifyNow();
    quint64 currentVerifyTotal = this->verifyTotal();
    
    // Only emit signals if values have changed
    if (currentDlNow != _lastEmittedDlNow || (currentDlTotal > 0 && _lastEmittedDlNow == 0)) {
        _lastEmittedDlNow = currentDlNow;
        emit downloadProgressChanged(currentDlNow, currentDlTotal);
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
    std::unique_lock<std::mutex> lock(_queueMutex);
    _downloadComplete = true;
    lock.unlock();
    
    // Notify the extraction thread that download is complete
    _cv.notify_all();
    
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

        while (true)
        {
            ssize_t size = archive_read_data(a, _abuf[_activeBuf], _abufsize);
            if (size < 0) {
                const char* errorStr = archive_error_string(a);
                
                // Check if this is the expected "No progress is possible" error after download completion
                if (size == ARCHIVE_FATAL && errorStr && strstr(errorStr, "No progress is possible")) {
                    break;
                }
                
                throw runtime_error(errorStr);
            }
            if (size == 0)
                break;
            if (size % 512 != 0)
            {
                size_t paddingBytes = 512-(size % 512);
                qDebug() << "Image is NOT a valid disk image, as its length is not a multiple of the sector size of 512 bytes long";
                qDebug() << "Last write() would be" << size << "bytes, but padding to" << size + paddingBytes << "bytes";
                memset(_abuf[_activeBuf]+size, 0, paddingBytes);
                size += paddingBytes;
            }

            // Emit progress updates during extraction
            _emitProgressUpdate();

            if (_writeThreadStarted)
            {
                //if (_writeFile(_abuf, size) != (size_t) size)
                if (!_writeFuture.result())
                {
                    if (!_cancelled)
                    {
                        _onWriteError();
                    }
                    archive_read_free(a);
                    return;
                }
            }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            _writeFuture = QtConcurrent::run(&DownloadThread::_writeFile, static_cast<DownloadThread*>(this), _abuf[_activeBuf], size);
#else
            _writeFuture = QtConcurrent::run(static_cast<DownloadThread*>(this), &DownloadThread::_writeFile, _abuf[_activeBuf], size);
#endif
            _activeBuf = _activeBuf ? 0 : 1;
            _writeThreadStarted = true;
        }

        if (_writeThreadStarted)
            _writeFuture.waitForFinished();
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
    // Use synchronized queue access to check for completion
    std::unique_lock<std::mutex> lock(_queueMutex);
    
    // Check if download is complete and queue is empty - if so, signal EOF
    if (_downloadComplete && _queue.empty()) {
        lock.unlock();
        *buff = nullptr;
        return 0;
    }
    
    // Wait for data to be available OR download completion with empty queue
    _cv.wait(lock, [this]{
        return _queue.size() != 0 || (_downloadComplete && _queue.empty());
    });
    
    // Check again after wait - download might have completed while we were waiting
    if (_downloadComplete && _queue.empty()) {
        lock.unlock();
        *buff = nullptr;
        return 0;
    }
    
    // Get the data
    _buf = _queue.front();
    _queue.pop_front();
    
    // Notify any waiting pushers
    lock.unlock();
    _cv.notify_one();
    
    *buff = _buf.data();
    return _buf.size();
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
    _verifyTotal = _file->Tell();
    
    // Use adaptive buffer size based on file size and system memory for optimal verification performance
    size_t verifyBufferSize = SystemMemoryManager::instance().getAdaptiveVerifyBufferSize(_verifyTotal);
    char *verifyBuf = (char *) qMallocAligned(verifyBufferSize, 4096);
    
    QElapsedTimer t1;
    t1.start();
    
    qDebug() << "Post-write verification using" << verifyBufferSize/1024 << "KB buffer for" 
             << _verifyTotal/(1024*1024) << "MB image";

#ifdef Q_OS_LINUX
    /* Make sure we are reading from the drive and not from cache */
    posix_fadvise(_file->GetHandle(), 0, 0, POSIX_FADV_DONTNEED);
#endif

    if (!_firstBlock)
    {
        _file->Seek(0);
    }
    else
    {
        _verifyhash.addData(_firstBlock, _firstBlockSize);
        _file->Seek(_firstBlockSize);
        _lastVerifyNow += _firstBlockSize;
    }

    while (_verifyEnabled && _lastVerifyNow < _verifyTotal && !_cancelled)
    {
        size_t bytes_to_read = qMin((qint64) verifyBufferSize, (qint64) (_verifyTotal-_lastVerifyNow));
        size_t lenRead = 0;
        rpi_imager::FileError read_result = _file->ReadSequential(reinterpret_cast<std::uint8_t*>(verifyBuf), bytes_to_read, lenRead);
        if (read_result != rpi_imager::FileError::kSuccess)
        {
            DownloadThread::_onDownloadError(tr("Error reading from storage.<br>"
                                                "SD card may be broken."));
            qFreeAligned(verifyBuf);
            return false;
        }

        _verifyhash.addData(verifyBuf, static_cast<qint64>(lenRead));
        _lastVerifyNow += static_cast<qint64>(lenRead);
        
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
