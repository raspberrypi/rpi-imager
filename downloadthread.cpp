/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include "downloadthread.h"
#include "config.h"
#include "dependencies/mountutils/src/mountutils.hpp"
#include "dependencies/drivelist/src/drivelist.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <utime.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex>
#include <QDebug>
#include <QProcess>
#include <QtConcurrent/QtConcurrent>

#ifdef Q_OS_LINUX
#include <sys/ioctl.h>
#include <linux/fs.h>
#include "linux/udisks2api.h"
#endif

using namespace std;

QByteArray DownloadThread::_proxy;
int DownloadThread::_curlCount = 0;

DownloadThread::DownloadThread(const QByteArray &url, const QByteArray &localfilename, const QByteArray &expectedHash, QObject *parent) :
    QThread(parent), _startOffset(0), _lastDlTotal(0), _lastDlNow(0), _verifyTotal(0), _lastVerifyNow(0), _bytesWritten(0), _url(url), _filename(localfilename), _expectedHash(expectedHash),
    _firstBlock(nullptr), _cancelled(false), _successful(false), _verifyEnabled(false), _cacheEnabled(false), _lastModified(0), _serverTime(0),  _lastFailureTime(0),
    _file(NULL), _writehash(OSLIST_HASH_ALGORITHM), _verifyhash(OSLIST_HASH_ALGORITHM), _inputBufferSize(0)
{
    if (!_curlCount)
        curl_global_init(CURL_GLOBAL_DEFAULT);
    _curlCount++;
}

DownloadThread::~DownloadThread()
{
    _cancelled = true;
    wait();
    if (_file.isOpen())
        _file.close();

    if (_firstBlock)
        qFreeAligned(_firstBlock);

    if (!--_curlCount)
        curl_global_cleanup();
}

void DownloadThread::setProxy(const QByteArray &proxy)
{
    _proxy = proxy;
}

QByteArray DownloadThread::proxy()
{
    return _proxy;
}

void DownloadThread::setUserAgent(const QByteArray &ua)
{
    _useragent = ua;
}

/* Curl write callback function, let it call the object oriented version */
size_t DownloadThread::_curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    return static_cast<DownloadThread *>(userdata)->_writeData(ptr, size * nmemb);
}

int DownloadThread::_curl_xferinfo_callback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    return (static_cast<DownloadThread *>(userdata)->_progress(dltotal, dlnow, ultotal, ulnow) == false);
}

size_t DownloadThread::_curl_header_callback( void *ptr, size_t size, size_t nmemb, void *userdata)
{
    int len = size*nmemb;
    string headerstr((char *) ptr, len);
    static_cast<DownloadThread *>(userdata)->_header(headerstr);

    return len;
}

bool DownloadThread::_openAndPrepareDevice()
{
    if (_filename.startsWith("/dev/"))
    {
        unmount_disk(_filename.constData());
    }

    _file.setFileName(_filename);

#ifdef Q_OS_WIN
    qDebug() << "device" << _filename;

    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (std::regex_match(_filename.constData(), m, windriveregex))
    {
        QByteArray nr = QByteArray::fromStdString(m[1]);

        if (!nr.isEmpty()) {
            qDebug() << "Removing partition table from Windows drive #" << nr << "(" << _filename << ")";

            QProcess proc;
            proc.start("diskpart");
            proc.waitForStarted();
            proc.write("select disk "+nr+"\r\n"
                            "clean\r\n"
                            "rescan\r\n");
            proc.closeWriteChannel();
            proc.waitForFinished();

            if (proc.exitCode())
            {
                emit error(tr("Error running diskpart: %1").arg(QString(proc.readAllStandardError())));
                return false;
            }
        }
    }

    auto l = Drivelist::ListStorageDevices();
    QByteArray devlower = _filename.toLower();
    QByteArray driveLetter;
    for (auto i : l)
    {
        if (QByteArray::fromStdString(i.device).toLower() == devlower)
        {
            if (i.mountpoints.size() == 1)
            {
                driveLetter = QByteArray::fromStdString(i.mountpoints.front());
                if (driveLetter.endsWith("\\"))
                    driveLetter.chop(1);
            }
            else if (i.mountpoints.size() > 1)
            {
                emit error(tr("Error removing existing partitions"));
                return false;
            }
            else
            {
                qDebug() << "Device no longer has any volumes. Nothing to lock.";
            }
        }
    }

    qDebug() << "sleeping to let Windows discovers there are no partitions";
    sleep(2);
    qDebug() << "done sleeping";

    if (!driveLetter.isEmpty())
    {
        _volumeFile.setFileName("\\\\.\\"+driveLetter);
        if (_volumeFile.open(QIODevice::ReadWrite))
            _volumeFile.lockVolume();
    }

#endif

#ifdef Q_OS_DARWIN
    _filename.replace("/dev/disk", "/dev/rdisk");

    if (!_file.authOpen(_filename)) {
        emit error(tr("Error running authopen to gain access to disk device '%1'").arg(QString(_filename)));
        return false;
    }
#else
    if (!_file.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
    {
#ifdef Q_OS_LINUX
        /* Opening device directly did not work, ask udisks2 to do it for us,
         * if necessary prompting for authorization */
        UDisks2Api udisks;
        int fd = udisks.authOpen(_filename);
        if (fd != -1)
        {
            _file.open(fd, QIODevice::ReadWrite | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle);
        }
        else
        {
#endif
            emit error(tr("Cannot open storage device '%1'.").arg(QString(_filename)));
            return false;
#ifdef Q_OS_LINUX
        }
#endif
    }
#endif

#ifndef Q_OS_WIN
    // Zero out MBR
    qint64 knownsize = _file.size();
    QByteArray emptyMB(1024*1024, 0);

    if (!_file.write(emptyMB.data(), emptyMB.size()) || !_file.flush())
    {
        emit error(tr("Write error while zero'ing out MBR"));
        return false;
    }

    // Zero out last part of card (may have GPT backup table)
    if (knownsize > emptyMB.size())
    {
        if (!_file.seek(knownsize-emptyMB.size())
                || !_file.write(emptyMB.data(), emptyMB.size())
                || !_file.flush()
                || !::fsync(_file.handle()))
        {
            emit error(tr("Write error while trying to zero out last part of card.\n"
                          "Card could be advertising wrong capacity (possible counterfeit)"));
            return false;
        }
    }
    emptyMB.clear();
    _file.seek(0);
#endif

#ifdef Q_OS_LINUX
    /* Optional optimizations for Linux */

    /* See if we can DISCARD/TRIM the SD card */
    uint64_t devsize, range[2];
    int fd = _file.handle();

    if (::ioctl(fd, BLKGETSIZE64, &devsize) == -1) {
        qDebug() << "Error getting device/sector size with BLKGETSIZE64 ioctl():" << strerror(errno);
    }
    else
    {
        int secsize;
        ::ioctl(fd, BLKSSZGET, &secsize);
        qDebug() << "Sector size:" << secsize << "Device size:" << devsize;

        qDebug() << "Try to perform TRIM/DISCARD on device";
        range[0] = 0;
        range[1] = devsize;
        if (::ioctl(fd, BLKDISCARD, &range) == -1)
        {
            qDebug() << "BLKDISCARD not supported";
        }
        else
        {
            qDebug() << "BLKDISCARD successful";
        }
    }
#endif

    return true;
}

void DownloadThread::run()
{
    if (isImage() && !_openAndPrepareDevice())
    {
        return;
    }

    char errorBuf[CURL_ERROR_SIZE] = {0};
    _c = curl_easy_init();
    curl_easy_setopt(_c, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(_c, CURLOPT_WRITEFUNCTION, &DownloadThread::_curl_write_callback);
    curl_easy_setopt(_c, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(_c, CURLOPT_XFERINFOFUNCTION, &DownloadThread::_curl_xferinfo_callback);
    curl_easy_setopt(_c, CURLOPT_PROGRESSDATA, this);
    curl_easy_setopt(_c, CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(_c, CURLOPT_URL, _url.constData());
    curl_easy_setopt(_c, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(_c, CURLOPT_MAXREDIRS, 10);
    curl_easy_setopt(_c, CURLOPT_ERRORBUFFER, errorBuf);
    curl_easy_setopt(_c, CURLOPT_FAILONERROR, 1);
    curl_easy_setopt(_c, CURLOPT_HEADERFUNCTION, &DownloadThread::_curl_header_callback);
    curl_easy_setopt(_c, CURLOPT_HEADERDATA, this);
    if (_inputBufferSize)
        curl_easy_setopt(_c, CURLOPT_BUFFERSIZE, _inputBufferSize);

    if (!_useragent.isEmpty())
        curl_easy_setopt(_c, CURLOPT_USERAGENT, _useragent.constData());
    if (!_proxy.isEmpty())
        curl_easy_setopt(_c, CURLOPT_PROXY, _proxy.constData());


    _timer.start();
    CURLcode ret = curl_easy_perform(_c);

    /* Deal with badly configured HTTP servers that terminate the connection quickly
       if connections stalls for some seconds while kernel commits buffers to slow SD card */
    while (ret == CURLE_PARTIAL_FILE)
    {
        time_t t = time(NULL);
        qDebug() << "HTTP connection lost. Time:" << t;

        /* If last failure happened less than 5 seconds ago, something else may
           be wrong. Sleep some time to prevent hammering server */
        if (t - _lastFailureTime < 5)
        {
            qDebug() << "Sleeping 5 seconds";
            ::sleep(5);
        }
        _lastFailureTime = t;

        _startOffset = _lastDlNow;
        curl_easy_setopt(_c, CURLOPT_RESUME_FROM_LARGE, _startOffset);

        ret = curl_easy_perform(_c);
    }

    curl_easy_cleanup(_c);

    switch (ret)
    {
        case CURLE_OK:
            _successful = true;
            qDebug() << "Download done in" << _timer.elapsed() / 1000 << "seconds";
            _onDownloadSuccess();
            break;
        case CURLE_WRITE_ERROR:
            deleteDownloadedFile();
            _onDownloadError("Error writing file to disk");
            break;
        case CURLE_ABORTED_BY_CALLBACK:
            deleteDownloadedFile();
            break;
        default:
            deleteDownloadedFile();
            _onDownloadError(errorBuf);
    }
}

size_t DownloadThread::_writeData(const char *buf, size_t len)
{
    _writeCache(buf, len);

    if (!_filename.isEmpty())
    {
        return _writeFile(buf, len);
    }
    else
    {
        _buf.append(buf, len);
        return len;
    }
}

void DownloadThread::_writeCache(const char *buf, size_t len)
{
    if (!_cacheEnabled || _cancelled)
        return;

    if (_cachefile.write(buf, len) != len)
    {
        qDebug() << "Error writing to cache file. Disabling caching.";
        _cacheEnabled = false;
        _cachefile.remove();
    }
}

void DownloadThread::setCacheFile(const QString &filename, qint64 filesize)
{
    _cachefile.setFileName(filename);
    if (_cachefile.open(QIODevice::WriteOnly))
    {
        _cacheEnabled = true;
        if (filesize)
        {
            /* Pre-allocate space */
            _cachefile.resize(filesize);
        }
    }
    else
    {
        qDebug() << "Error opening cache file for writing. Disabling caching.";
    }
}

void DownloadThread::_hashData(const char *buf, size_t len)
{
    _writehash.addData(buf, len);
}

size_t DownloadThread::_writeFile(const char *buf, size_t len)
{
    if (_cancelled)
        return len;

    if (!_firstBlock)
    {
        _writehash.addData(buf, len);
        _firstBlock = (char *) qMallocAligned(len, 4096);
        _firstBlockSize = len;
        ::memcpy(_firstBlock, buf, len);

        return _file.seek(len) ? len : 0;
    }
    QFuture<void> wh = QtConcurrent::run(this, &DownloadThread::_hashData, buf, len);

    qint64 written = _file.write(buf, len);
    _bytesWritten += written;

    if ((size_t) written != len)
    {
        qDebug() << "Write error:" << _file.errorString() << "while writing len:" << len;
    }

    wh.waitForFinished();
    return (written < 0) ? 0 : written;
}

bool DownloadThread::_progress(curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    if (dltotal)
        _lastDlTotal = _startOffset + dltotal;
    _lastDlNow   = _startOffset + dlnow;

    return !_cancelled;
}

void DownloadThread::_header(const string &header)
{
    if (header.compare(0, 6, "Date: ") == 0)
    {
        _serverTime = curl_getdate(header.data()+6, NULL);
    }
    else if (header.compare(0, 15, "Last-Modified: ") == 0)
    {
        _lastModified = curl_getdate(header.data()+15, NULL);
    }
    qDebug() << "Received header:" << header.c_str();
}

void DownloadThread::cancelDownload()
{
    _cancelled = true;
    //deleteDownloadedFile();
}

QByteArray DownloadThread::data()
{
    return _buf;
}

bool DownloadThread::successfull()
{
    return _successful;
}

time_t DownloadThread::lastModified()
{
    return _lastModified;
}

time_t DownloadThread::serverTime()
{
    return _serverTime;
}

void DownloadThread::deleteDownloadedFile()
{
    if (!_filename.isEmpty())
    {
        _file.close();
        if (_cachefile.isOpen())
            _cachefile.remove();
#ifdef Q_OS_WIN
        _volumeFile.close();
#endif

        if (!_filename.startsWith("/dev/") && !_filename.startsWith("\\\\.\\"))
        {
            //_file.remove();
        }
    }
}

uint64_t DownloadThread::dlNow()
{
    return _lastDlNow;
}

uint64_t DownloadThread::dlTotal()
{
    return _lastDlTotal;
}

uint64_t DownloadThread::verifyNow()
{
    return _lastVerifyNow;
}

uint64_t DownloadThread::verifyTotal()
{
    return _verifyTotal;
}

uint64_t DownloadThread::bytesWritten()
{
    return _bytesWritten;
}

void DownloadThread::_onDownloadSuccess()
{
    _writeComplete();
}

void DownloadThread::_onDownloadError(const QString &msg)
{
    emit error(msg);
}

void DownloadThread::_writeComplete()
{
    if (!_file.flush())
    {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while flushing)"));
        return;
    }

#ifndef Q_OS_WIN
    if (::fsync(_file.handle()) != 0) {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while fsync)"));
        return;
    }
#endif

    qDebug() << "Write done in" << _timer.elapsed() / 1000 << "seconds";
    QByteArray computedHash = _writehash.result().toHex();
    qDebug() << "Hash of uncompressed image:" << computedHash;
    if (!_expectedHash.isEmpty() && _expectedHash != computedHash)
    {
        qDebug() << "Mismatch with expected hash:" << _expectedHash;
        _file.close();
        if (_cachefile.isOpen())
            _cachefile.remove();
#ifdef Q_OS_WIN
        _volumeFile.close();
#endif
        DownloadThread::_onDownloadError(tr("Download corrupt. Hash does not match"));
        return;
    }
    if (_cacheEnabled && _expectedHash == computedHash)
    {
        _cachefile.close();
        emit cacheFileUpdated(computedHash);
    }

    /* Verify */
    if (_verifyEnabled && !_verify())
    {
        _file.close();
#ifdef Q_OS_WIN
        _volumeFile.close();
#endif
        return;
    }

    emit finalizing();

    if (_firstBlock)
    {
        qDebug() << "Writing first block (which we skipped at first)";
        _file.seek(0);
        if (!_file.write(_firstBlock, _firstBlockSize) || !_file.flush())
        {
            qFreeAligned(_firstBlock);
            _firstBlock = nullptr;

            DownloadThread::_onDownloadError(tr("Error writing first block (partition table)"));
            return;
        }
        _bytesWritten += _firstBlockSize;
        qFreeAligned(_firstBlock);
        _firstBlock = nullptr;
    }

    _file.close();
#ifdef Q_OS_WIN
    _volumeFile.close();
#endif

#ifdef Q_OS_DARWIN
    QThread::sleep(1);
    _filename.replace("/dev/rdisk", "/dev/disk");
#endif
    eject_disk(_filename.constData());

    emit success();
}

bool DownloadThread::_verify()
{
    char *verifyBuf = (char *) qMallocAligned(IMAGEWRITER_VERIFY_BLOCKSIZE, 4096);
    _lastVerifyNow = 0;
    _verifyTotal = _file.pos();
    QElapsedTimer t1;
    t1.start();

#ifdef Q_OS_LINUX
    /* Make sure we are reading from the drive and not from cache */
    fcntl(_file.handle(), F_SETFL, O_DIRECT | fcntl(_file.handle(), F_GETFL));
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
        qint64 lenRead = _file.read(verifyBuf, qMin((qint64) IMAGEWRITER_VERIFY_BLOCKSIZE, (qint64) (_verifyTotal-_lastVerifyNow) ));
        if (lenRead == -1)
        {
            DownloadThread::_onDownloadError(tr("Error reading from storage.\n"
                                                "SD card may be broken."));
            return false;
        }

        _verifyhash.addData(verifyBuf, lenRead);
        _lastVerifyNow += lenRead;
    }
    qFreeAligned(verifyBuf);

    qDebug() << "Verify done in" << t1.elapsed() / 1000.0 << "seconds";
    qDebug() << "Verify hash:" << _verifyhash.result().toHex();

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

void DownloadThread::setVerifyEnabled(bool verify)
{
    _verifyEnabled = verify;
}

bool DownloadThread::isImage()
{
    return true;
}

void DownloadThread::setInputBufferSize(int len)
{
    _inputBufferSize = len;
}
