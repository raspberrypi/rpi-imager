/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadthread.h"
#include "config.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
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
#include <QSettings>
#include <QtConcurrent/QtConcurrent>
#include <QtNetwork/QNetworkProxy>

#ifdef Q_OS_LINUX
#include <sys/ioctl.h>
#include <linux/fs.h>
#include "linux/udisks2api.h"
#endif

using namespace std;

QByteArray DownloadThread::_proxy;
int DownloadThread::_curlCount = 0;

DownloadThread::DownloadThread(const QByteArray &url, const QByteArray &localfilename, const QByteArray &expectedHash, QObject *parent) :
    QThread(parent), _startOffset(0), _lastDlTotal(0), _lastDlNow(0), _verifyTotal(0), _lastVerifyNow(0), _bytesWritten(0), _lastFailureOffset(0), _sectorsStart(-1), _url(url), _filename(localfilename), _expectedHash(expectedHash),
    _firstBlock(nullptr), _cancelled(false), _successful(false), _verifyEnabled(false), _cacheEnabled(false), _lastModified(0), _serverTime(0),  _lastFailureTime(0),
    _inputBufferSize(0), _file(NULL), _writehash(OSLIST_HASH_ALGORITHM), _verifyhash(OSLIST_HASH_ALGORITHM)
{
    if (!_curlCount)
        curl_global_init(CURL_GLOBAL_DEFAULT);
    _curlCount++;

    QSettings settings;
    _ejectEnabled = settings.value("eject", true).toBool();
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

QByteArray DownloadThread::_fileGetContentsTrimmed(const QString &filename)
{
    QByteArray result;
    QFile f(filename);

    if (f.exists() && f.open(f.ReadOnly))
    {
        result = f.readAll().trimmed();
        f.close();
    }

    return result;
}

bool DownloadThread::_openAndPrepareDevice()
{
    if (_filename.startsWith("/dev/"))
    {
        emit preparationStatusUpdate(tr("unmounting drive"));
#ifdef Q_OS_DARWIN
        /* Also unmount any APFS volumes using this physical disk */
        auto l = Drivelist::ListStorageDevices();
        for (const auto &i : l)
        {
            if (QByteArray::fromStdString(i.device) == _filename)
            {
                for (const auto &j : i.childDevices)
                {
                    qDebug() << "Unmounting APFS volume:" << j.c_str();
                    unmount_disk(j.c_str());
                }
                break;
            }
        }
#endif
        qDebug() << "Unmounting:" << _filename;
        unmount_disk(_filename.constData());
    }
    emit preparationStatusUpdate(tr("opening drive"));

    _file.setFileName(_filename);

#ifdef Q_OS_WIN
    qDebug() << "device" << _filename;

    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (std::regex_match(_filename.constData(), m, windriveregex))
    {
        _nr = QByteArray::fromStdString(m[1]);

        if (!_nr.isEmpty()) {
            qDebug() << "Removing partition table from Windows drive #" << _nr << "(" << _filename << ")";

            QProcess proc;
            proc.start("diskpart");
            proc.waitForStarted();
            proc.write("select disk "+_nr+"\r\n"
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

    if (!driveLetter.isEmpty())
    {
        _volumeFile.setFileName("\\\\.\\"+driveLetter);
        if (_volumeFile.open(QIODevice::ReadWrite))
            _volumeFile.lockVolume();
    }

#endif

#ifdef Q_OS_DARWIN
    _filename.replace("/dev/disk", "/dev/rdisk");

    auto authopenresult = _file.authOpen(_filename);

    if (authopenresult == _file.authOpenCancelled) {
        /* User cancelled authentication */
        emit error(tr("Authentication cancelled"));
        return false;
    } else if (authopenresult == _file.authOpenError) {
        QString msg = tr("Error running authopen to gain access to disk device '%1'").arg(QString(_filename));
        msg += "<br>"+tr("Please verify if 'Raspberry Pi Imager' is allowed access to 'removable volumes' in privacy settings (under 'files and folders' or alternatively give it 'full disk access').");
        QStringList args("x-apple.systempreferences:com.apple.preference.security?Privacy_RemovableVolume");
        QProcess::execute("open", args);
        emit error(msg);
        return false;
    }
#else
    if (!_file.open(QIODevice::ReadWrite | QIODevice::Unbuffered))
    {
#ifdef Q_OS_LINUX
#ifndef QT_NO_DBUS
        /* Opening device directly did not work, ask udisks2 to do it for us,
         * if necessary prompting for authorization */
        UDisks2Api udisks;
        int fd = udisks.authOpen(_filename);
        if (fd != -1)
        {
            _file.open(fd, QIODevice::ReadWrite | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle);
        }
        else
#endif
        {
            emit error(tr("Cannot open storage device '%1'.").arg(QString(_filename)));
            return false;
        }
#endif
    }
#endif

#ifdef Q_OS_LINUX
    /* Optional optimizations for Linux */

    if (_filename.startsWith("/dev/"))
    {
        QString devname = _filename.mid(5);

        /* On some internal SD card readers CID/CSD is available, print it for debugging purposes */
        QByteArray cid = _fileGetContentsTrimmed("/sys/block/"+devname+"/device/cid");
        QByteArray csd = _fileGetContentsTrimmed("/sys/block/"+devname+"/device/csd");
        if (!cid.isEmpty())
            qDebug() << "SD card CID:" << cid;
        if (!csd.isEmpty())
            qDebug() << "SD card CSD:" << csd;

        QByteArray discardmax = _fileGetContentsTrimmed("/sys/block/"+devname+"/queue/discard_max_bytes");

        if (discardmax.isEmpty() || discardmax == "0")
        {
            qDebug() << "BLKDISCARD not supported";
        }
        else
        {
            /* DISCARD/TRIM the SD card */
            uint64_t devsize, range[2];
            int fd = _file.handle();

            if (::ioctl(fd, BLKGETSIZE64, &devsize) == -1) {
                qDebug() << "Error getting device/sector size with BLKGETSIZE64 ioctl():" << strerror(errno);
            }
            else
            {
                qDebug() << "Try to perform TRIM/DISCARD on device";
                range[0] = 0;
                range[1] = devsize;
                emit preparationStatusUpdate(tr("discarding existing data on drive"));
                _timer.start();
                if (::ioctl(fd, BLKDISCARD, &range) == -1)
                {
                    qDebug() << "BLKDISCARD failed.";
                }
                else
                {
                    qDebug() << "BLKDISCARD successful. Discarding took" << _timer.elapsed() / 1000 << "seconds";
                }
            }
        }
    }
#endif

#ifndef Q_OS_WIN
    // Zero out MBR
    qint64 knownsize = _file.size();
    QByteArray emptyMB(1024*1024, 0);

    emit preparationStatusUpdate(tr("zeroing out first and last MB of drive"));
    qDebug() << "Zeroing out first and last MB of drive";
    _timer.start();

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
            emit error(tr("Write error while trying to zero out last part of card.<br>"
                          "Card could be advertising wrong capacity (possible counterfeit)."));
            return false;
        }
    }
    emptyMB.clear();
    _file.seek(0);
    qDebug() << "Done zeroing out start and end of drive. Took" << _timer.elapsed() / 1000 << "seconds";
#endif

#ifdef Q_OS_LINUX
    _sectorsStart = _sectorsWritten();
#endif

    return true;
}

void DownloadThread::run()
{
    if (isImage() && !_openAndPrepareDevice())
    {
        return;
    }

    qDebug() << "Image URL:" << _url;
    if (_url.startsWith("file://") && _url.at(7) != '/')
    {
        /* libcurl does not like UNC paths in the form of file://1.2.3.4/share */
        _url.replace("file://", "file:////");
        qDebug() << "Corrected UNC URL to:" << _url;
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
    curl_easy_setopt(_c, CURLOPT_CONNECTTIMEOUT, 30);
    curl_easy_setopt(_c, CURLOPT_LOW_SPEED_TIME, 60);
    curl_easy_setopt(_c, CURLOPT_LOW_SPEED_LIMIT, 100);
    if (_inputBufferSize)
        curl_easy_setopt(_c, CURLOPT_BUFFERSIZE, _inputBufferSize);

    if (!_useragent.isEmpty())
        curl_easy_setopt(_c, CURLOPT_USERAGENT, _useragent.constData());

    if (_proxy.isEmpty())
    {
#ifndef QT_NO_NETWORKPROXY
        /* Ask OS for proxy information. */
        QNetworkProxyQuery npq{QUrl{_url}};
        QList<QNetworkProxy> proxyList = QNetworkProxyFactory::systemProxyForQuery(npq);
        if (!proxyList.isEmpty())
        {
            QNetworkProxy proxy = proxyList.first();
            if (proxy.type() != proxy.NoProxy)
            {
                QUrl proxyUrl;

                proxyUrl.setScheme(proxy.type() == proxy.Socks5Proxy ? "socks5h" : "http");
                proxyUrl.setHost(proxy.hostName());
                proxyUrl.setPort(proxy.port());
                qDebug() << "Using proxy server:" << proxyUrl;

                if (!proxy.user().isEmpty())
                {
                    proxyUrl.setUserName(proxy.user());
                    proxyUrl.setPassword(proxy.password());
                }

                _proxy = proxyUrl.toEncoded();
            }
        }
#endif
    }

    if (!_proxy.isEmpty())
        curl_easy_setopt(_c, CURLOPT_PROXY, _proxy.constData());

    emit preparationStatusUpdate(tr("starting download"));
    _timer.start();
    CURLcode ret = curl_easy_perform(_c);

    /* Deal with badly configured HTTP servers that terminate the connection quickly
       if connections stalls for some seconds while kernel commits buffers to slow SD card.
       And also reconnect if we detect from our end that transfer stalled for more than one minute */
    while (ret == CURLE_PARTIAL_FILE || ret == CURLE_OPERATION_TIMEDOUT || (ret == CURLE_RECV_ERROR && _lastDlNow != _lastFailureOffset) )
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
        _lastFailureOffset = _lastDlNow;
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

#ifdef Q_OS_WIN
            if (_file.errorCode() == ERROR_ACCESS_DENIED)
            {
                QString msg = tr("Access denied error while writing file to disk.");
                QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Defender\\Windows Defender Exploit Guard\\Controlled Folder Access",
                                   QSettings::Registry64Format);
                if (registry.value("EnableControlledFolderAccess").toInt() == 1)
                {
                    msg += "<br>"+tr("Controlled Folder Access seems to be enabled. Please add both rpi-imager.exe and fat32format.exe to the list of allowed apps and try again.");
                }
                _onDownloadError(msg);
            }
            else
#endif
            if (!_cancelled)
                _onDownloadError(tr("Error writing file to disk"));
            break;
        case CURLE_ABORTED_BY_CALLBACK:
            deleteDownloadedFile();
            break;
        default:
            deleteDownloadedFile();
            QString errorMsg;

            if (!errorBuf[0])
                /* No detailed error message text provided, use standard text for libcurl result code */
                errorMsg += curl_easy_strerror(ret);
            else
                errorMsg += errorBuf;

            char *ipstr;
            if (curl_easy_getinfo(_c, CURLINFO_PRIMARY_IP, &ipstr) == CURLE_OK && ipstr && ipstr[0])
                errorMsg += QString(" - Server IP: ")+ipstr;

            _onDownloadError(tr("Error downloading: %1").arg(errorMsg));
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QFuture<void> wh = QtConcurrent::run(&DownloadThread::_hashData, this, buf, len);
#else
    QFuture<void> wh = QtConcurrent::run(this, &DownloadThread::_hashData, buf, len);
#endif

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
    qDebug() << "Received header:" << QByteArray(header.c_str()).trimmed();
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
    if (_sectorsStart != -1)
        return qMin((uint64_t) (_sectorsWritten()-_sectorsStart)*512, (uint64_t) _bytesWritten);
    else
        return _bytesWritten;
}

void DownloadThread::_onDownloadSuccess()
{
    _writeComplete();
}

void DownloadThread::_onDownloadError(const QString &msg)
{
    _cancelled = true;
    emit error(msg);
}

void DownloadThread::_closeFiles()
{
    _file.close();
#ifdef Q_OS_WIN
    _volumeFile.close();
#endif
    if (_cachefile.isOpen())
        _cachefile.close();
}

void DownloadThread::_writeComplete()
{
    QByteArray computedHash = _writehash.result().toHex();
    qDebug() << "Hash of uncompressed image:" << computedHash;
    if (!_expectedHash.isEmpty() && _expectedHash != computedHash)
    {
        qDebug() << "Mismatch with expected hash:" << _expectedHash;
        if (_cachefile.isOpen())
            _cachefile.remove();
        DownloadThread::_onDownloadError(tr("Download corrupt. Hash does not match"));
        _closeFiles();
        return;
    }
    if (_cacheEnabled && _expectedHash == computedHash)
    {
        _cachefile.close();
        emit cacheFileUpdated(computedHash);
    }

    if (!_file.flush())
    {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while flushing)"));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    if (::fsync(_file.handle()) != 0) {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while fsync)"));
        _closeFiles();
        return;
    }
#endif

    qDebug() << "Write done in" << _timer.elapsed() / 1000 << "seconds";

    /* Verify */
    if (_verifyEnabled && !_verify())
    {
        _closeFiles();
        return;
    }

    emit finalizing();

    if (!_config.isEmpty() || !_cmdline.isEmpty() || !_firstrun.isEmpty())
    {
        if (!_customizeImage())
        {
            _closeFiles();
            return;
        }
    }

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

    if (!_file.flush())
    {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while flushing)"));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    if (::fsync(_file.handle()) != 0) {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while fsync)"));
        _closeFiles();
        return;
    }
#endif

    _closeFiles();

#ifdef Q_OS_DARWIN
    QThread::sleep(1);
    _filename.replace("/dev/rdisk", "/dev/disk");
#endif

    if (_ejectEnabled)
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
    //fcntl(_file.handle(), F_SETFL, O_DIRECT | fcntl(_file.handle(), F_GETFL));
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
        qint64 lenRead = _file.read(verifyBuf, qMin((qint64) IMAGEWRITER_VERIFY_BLOCKSIZE, (qint64) (_verifyTotal-_lastVerifyNow) ));
        if (lenRead == -1)
        {
            DownloadThread::_onDownloadError(tr("Error reading from storage.<br>"
                                                "SD card may be broken."));
            return false;
        }

        _verifyhash.addData(verifyBuf, lenRead);
        _lastVerifyNow += lenRead;
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

qint64 DownloadThread::_sectorsWritten()
{
#ifdef Q_OS_LINUX
    if (!_filename.startsWith("/dev/"))
        return -1;

    QFile f("/sys/class/block/"+_filename.mid(5)+"/stat");
    if (!f.open(f.ReadOnly))
        return -1;
    QByteArray ioline = f.readAll().simplified();
    f.close();

    QList<QByteArray> stats = ioline.split(' ');

    if (stats.count() >= 6)
        return stats.at(6).toLongLong(); /* write sectors */
#endif
    return -1;
}

void DownloadThread::setImageCustomization(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudInitNetwork, const QByteArray &initFormat)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudInitNetwork;
    _initFormat = initFormat;
}

bool DownloadThread::_customizeImage()
{
    emit preparationStatusUpdate(tr("Customizing image"));

    try
    {
        DeviceWrapper dw(&_file);
        if (_firstBlock)
        {
            /* Outsource first block handling to DeviceWrapper.
               It will still not actually be written out yet,
               until we call sync(), and then it will
               save the first 4k sector with MBR for last */
            dw.pwrite(_firstBlock, _firstBlockSize, 0);
            _bytesWritten += _firstBlockSize;
            qFreeAligned(_firstBlock);
            _firstBlock = nullptr;
        }
        DeviceWrapperFatPartition *fat = dw.fatPartition(1);

        if (!_config.isEmpty())
        {
            auto configItems = _config.split('\n');
            configItems.removeAll("");
            QByteArray config = fat->readFile("config.txt");

            for (const QByteArray& item : qAsConst(configItems))
            {
                if (config.contains("#"+item)) {
                    /* Uncomment existing line */
                    config.replace("#"+item, item);
                } else if (config.contains("\n"+item)) {
                    /* config.txt already contains the line */
                } else {
                    /* Append new line to config.txt */
                    if (config.right(1) != "\n")
                        config += "\n"+item+"\n";
                    else
                        config += item+"\n";
                }
            }

            fat->writeFile("config.txt", config);
        }

        if (_initFormat == "auto")
        {
            /* Do an attempt at auto-detecting what customization format a custom
               image provided by the user supports */
            QByteArray issue = fat->readFile("issue.txt");

            if (fat->fileExists("user-data"))
            {
                /* If we have user-data file on FAT partition, then it must be cloudinit */
                _initFormat = "cloudinit";
                qDebug() << "user-data found on FAT partition. Assuming cloudinit support";
            }
            else if (issue.contains("pi-gen"))
            {
                /* If issue.txt mentions pi-gen, and there is no user-data file assume
                 * it is a RPI OS flavor, and use the old systemd unit firstrun script stuff */
                _initFormat = "systemd";
                qDebug() << "using firstrun script invoked by systemd customization method";
            }
            else
            {
                /* Fallback to writing cloudinit file, as it does not hurt having one
                 * Will just have no customization if OS does not support it */
                _initFormat = "cloudinit";
                qDebug() << "Unknown what customization method image supports. Falling back to cloudinit";
            }
        }

        if (!_firstrun.isEmpty() && _initFormat == "systemd")
        {
            fat->writeFile("firstrun.sh", _firstrun);
            _cmdline += " systemd.run=/boot/firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target";
        }

        if (!_cloudinit.isEmpty() && _initFormat == "cloudinit")
        {
            _cloudinit = "#cloud-config\n"+_cloudinit;
            fat->writeFile("user-data", _cloudinit);
        }

        if (!_cloudinitNetwork.isEmpty() && _initFormat == "cloudinit")
        {
            fat->writeFile("network-config", _cloudinitNetwork);
        }

        if (!_cmdline.isEmpty())
        {
            QByteArray cmdline = fat->readFile("cmdline.txt").trimmed();

            cmdline += _cmdline;

            fat->writeFile("cmdline.txt", cmdline);
        }
        dw.sync();
    }
    catch (std::runtime_error &err)
    {
        emit error(err.what());
        return false;
    }

    emit finalizing();

    return true;
}
