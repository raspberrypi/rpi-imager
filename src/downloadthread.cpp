/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadthread.h"
#include "config.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "systemmemorymanager.h"
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
#include <errno.h>
#include <regex>
#include <QDebug>
#include <QProcess>
#include <QSettings>
#include <QFuture>
#include <QtConcurrent/qtconcurrentrun.h>
#include <QtNetwork/QNetworkProxy>
#include <QTextStream>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <chrono>
#include "windows/winfile.h"
#include "windows/diskpart_util.h"
#endif

#include "imageadvancedoptions.h"
#include "secureboot.h"
#include <QTemporaryDir>

#ifdef Q_OS_LINUX
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

using namespace std;

QByteArray DownloadThread::_proxy;
int DownloadThread::_curlCount = 0;

DownloadThread::DownloadThread(const QByteArray &url, const QByteArray &localfilename, const QByteArray &expectedHash, QObject *parent) :
    QThread(parent), _startOffset(0), _lastDlTotal(0), _lastDlNow(0), _verifyTotal(0), _lastVerifyNow(0), _bytesWritten(0), _lastFailureOffset(0), _sectorsStart(-1), _url(url), _filename(localfilename), _expectedHash(expectedHash),
    _firstBlock(nullptr), _cancelled(false), _successful(false), _verifyEnabled(false), _cacheEnabled(false), _lastModified(0), _serverTime(0),  _lastFailureTime(0),
    _inputBufferSize(SystemMemoryManager::instance().getOptimalInputBufferSize()), _writehash(OSLIST_HASH_ALGORITHM), _verifyhash(OSLIST_HASH_ALGORITHM), _cachehash(OSLIST_HASH_ALGORITHM)
{
    if (!_curlCount)
        curl_global_init(CURL_GLOBAL_DEFAULT);
    _curlCount++;

    QSettings settings;
    _ejectEnabled = settings.value("eject", true).toBool();

    // Initialize unified file operations
    _file = rpi_imager::FileOperations::Create();
#ifdef Q_OS_WIN
    _volumeFile = rpi_imager::FileOperations::Create();
#endif

    // Initialize cross-platform adaptive sync configuration
    _lastSyncBytes = 0;
    _lastSyncTime.start();
    _initializeSyncConfiguration();
}

DownloadThread::~DownloadThread()
{
    _cancelled = true;
    wait();
    
    // Close unified file operations
    if (_file && _file->IsOpen()) {
        _file->Close();
    }
#ifdef Q_OS_WIN
    if (_volumeFile && _volumeFile->IsOpen()) {
        _volumeFile->Close();
    }
#endif
    
    // Use _closeFiles() to ensure cache file is properly closed
    _closeFiles();

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
        emit preparationStatusUpdate(tr("Unmounting drive..."));
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
    emit preparationStatusUpdate(tr("Opening drive..."));

    // Convert QByteArray filename to std::string for FileOperations
    std::string filename_str = _filename.toStdString();

#ifdef Q_OS_WIN
    qDebug() << "device" << _filename;

    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (std::regex_match(_filename.constData(), m, windriveregex))
    {
        _nr = QByteArray::fromStdString(m[1]);

        if (!_nr.isEmpty()) {
            qDebug() << "Removing partition table from Windows drive #" << _nr << "(" << _filename << ")";

            // First, try to unmount any volumes on this disk
            auto l = Drivelist::ListStorageDevices();
            QByteArray devlower = _filename.toLower();
            for (auto i : l)
            {
                if (QByteArray::fromStdString(i.device).toLower() == devlower)
                {
                    for (const auto& mountpoint : i.mountpoints)
                    {
                        QString driveLetter = QString::fromStdString(mountpoint);
                        if (driveLetter.endsWith("\\"))
                            driveLetter.chop(1);
                        
                        qDebug() << "Attempting to unmount drive" << driveLetter;
                        
                        // Try to lock and unlock the volume to force unmount
                        WinFile tempFile;
                        tempFile.setFileName("\\\\.\\" + driveLetter);
                        if (tempFile.open(QIODevice::ReadWrite))
                        {
                            if (tempFile.lockVolume())
                            {
                                tempFile.unlockVolume();
                                qDebug() << "Successfully unlocked volume" << driveLetter;
                            }
                            tempFile.close();
                        }
                        
                        // Give the system time to process the unmount
                        QThread::msleep(500);
                    }
                    break;
                }
            }

            // Clean disk with diskpart utility (standardized behavior: 60s timeout, 3 retries, always unmount volumes)
            auto result = DiskpartUtil::cleanDisk(_filename, std::chrono::seconds(60), 3, DiskpartUtil::VolumeHandling::UnmountFirst);
            if (!result.success)
            {
                emit error(result.errorMessage);
                return false;
            }
        }
    }

    // Re-scan for drive letters after diskpart
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
                emit error(tr("Error: Multiple partitions found on disk. Please ensure the disk is completely clean."));
                return false;
            }
            else
            {
                qDebug() << "Device no longer has any volumes. Nothing to lock.";
            }
        }
    }

    // Only try to lock volume if there's a drive letter (which shouldn't happen after clean)
    if (!driveLetter.isEmpty())
    {
        qDebug() << "Warning: Drive letter still present after clean:" << driveLetter;
        std::string volumePath = ("\\\\.\\"+driveLetter).toStdString();
        if (_volumeFile->OpenDevice(volumePath) == rpi_imager::FileError::kSuccess)
        {
            // Note: lockVolume functionality would need to be added to FileOperations if needed
            // For now, we'll skip the lock/unlock logic as it's Windows-specific
            qDebug() << "Opened volume file for" << driveLetter;
            // Original logic: if (!_volumeFile.lockVolume()) { ... }
            // For now, just log a warning since we can't lock volumes with FileOperations yet
            qDebug() << "Volume lock/unlock functionality not yet implemented in unified FileOperations";
        }
    }

#endif

#ifdef Q_OS_DARWIN
    _filename.replace("/dev/disk", "/dev/rdisk");

    // Note: authOpen functionality needs to be handled differently with FileOperations
    // For now, we'll try direct opening and provide better error messages
    rpi_imager::FileError result = _file->OpenDevice(filename_str);
    
    if (result != rpi_imager::FileError::kSuccess) {
        // On macOS, this might be due to permission issues that authOpen would have handled
        QString msg = tr("Error opening disk device '%1'").arg(QString(_filename));
        msg += "<br>"+tr("Please verify if 'Raspberry Pi Imager' is allowed access to 'removable volumes' in privacy settings (under 'files and folders' or alternatively give it 'full disk access').");
        QStringList args("x-apple.systempreferences:com.apple.preference.security?Privacy_RemovableVolume");
        QProcess::execute("open", args);
        emit error(msg);
        return false;
    }
#else
    // Use unified FileOperations to open device for non-macOS platforms
    rpi_imager::FileError result = _file->OpenDevice(filename_str);
    if (result != rpi_imager::FileError::kSuccess)
    {
#ifdef Q_OS_LINUX
        emit error(tr("Cannot open storage device '%1'. Please run with elevated privileges (sudo).").arg(QString(_filename)));
#else
        emit error(tr("Cannot open storage device '%1'.").arg(QString(_filename)));
#endif
        return false;
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
            int fd = _file->GetHandle();

            if (::ioctl(fd, BLKGETSIZE64, &devsize) == -1) {
                qDebug() << "Error getting device/sector size with BLKGETSIZE64 ioctl():" << strerror(errno);
            }
            else
            {
                qDebug() << "Try to perform TRIM/DISCARD on device";
                range[0] = 0;
                range[1] = devsize;
                emit preparationStatusUpdate(tr("Discarding existing data on drive..."));
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
    // Zero out MBR using unified FileOperations
    std::uint64_t knownsize = 0;
    if (_file->GetSize(knownsize) != rpi_imager::FileError::kSuccess) {
        emit error(tr("Error getting device size"));
        return false;
    }
    
    QByteArray emptyMB(1024*1024, 0);
    emit preparationStatusUpdate(tr("Zero'ing out first and last MB of drive..."));
    qDebug() << "Zeroing out first and last MB of drive";
    _timer.start();

    if (_file->WriteSequential(reinterpret_cast<const std::uint8_t*>(emptyMB.data()), emptyMB.size()) != rpi_imager::FileError::kSuccess ||
        _file->Flush() != rpi_imager::FileError::kSuccess)
    {
        emit error(tr("Write error while zero'ing out MBR"));
        return false;
    }

    // Zero out last part of card (may have GPT backup table)
    if (knownsize > static_cast<std::uint64_t>(emptyMB.size()))
    {
        if (_file->Seek(knownsize - emptyMB.size()) != rpi_imager::FileError::kSuccess ||
            _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(emptyMB.data()), emptyMB.size()) != rpi_imager::FileError::kSuccess ||
            _file->Flush() != rpi_imager::FileError::kSuccess ||
            _file->ForceSync() != rpi_imager::FileError::kSuccess)
        {
            emit error(tr("Write error while trying to zero out last part of card.<br>"
                          "Card could be advertising wrong capacity (possible counterfeit)."));
            return false;
        }
    }
    emptyMB.clear();
    _file->Seek(0);
    qDebug() << "Done zero'ing out start and end of drive. Took" << _timer.elapsed() / 1000 << "seconds";
#endif

#ifdef Q_OS_LINUX
    _sectorsStart = _sectorsWritten();
#endif

    return true;
}

void DownloadThread::run()
{
    qDebug() << "Download thread starting. isImage?" << isImage() << "filename:" << _filename;
    if (isImage() && !_openAndPrepareDevice())
    {
        return;
    }

    // URL logged only on error
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

    emit preparationStatusUpdate(tr("Starting download..."));
    // Minimal logging during normal operation
    _timer.start();
    CURLcode ret = curl_easy_perform(_c);

    /* Deal with badly configured HTTP servers that terminate the connection quickly
       if connections stalls for some seconds while kernel commits buffers to slow SD card.
       And also reconnect if we detect from our end that transfer stalled for more than one minute */
    while (ret == CURLE_PARTIAL_FILE || ret == CURLE_OPERATION_TIMEDOUT
           || (ret == CURLE_HTTP2_STREAM && _lastDlNow != _lastFailureOffset)
           || (ret == CURLE_RECV_ERROR && _lastDlNow != _lastFailureOffset) )
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
            _onWriteError();
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
    // no-op debug removed
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

    // Hash the data going into the cache file
    _cachehash.addData(buf, len);

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
        qDebug() << "_writeFile: captured first block (" << len << ") and advanced file offset via seek";
        return (_file->Seek(len) == rpi_imager::FileError::kSuccess) ? len : 0;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QFuture<void> wh = QtConcurrent::run(&DownloadThread::_hashData, this, buf, len);
#else
    QFuture<void> wh = QtConcurrent::run(this, &DownloadThread::_hashData, buf, len);
#endif

    // Use unified FileOperations for writing
    size_t bytes_written = 0;
    rpi_imager::FileError write_result = _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(buf), len);
    
    if (write_result == rpi_imager::FileError::kSuccess) {
        bytes_written = len;
        _bytesWritten += bytes_written;
    } else {
        qDebug() << "Write error: FileOperations write failed with error code" << static_cast<int>(write_result) << "while writing len:" << len;
    }

    qint64 written = static_cast<qint64>(bytes_written);

    wh.waitForFinished();

    // Cross-platform periodic sync to prevent page cache buildup
    _periodicSync();

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
        if (_file && _file->IsOpen()) {
            _file->Close();
        }
        if (_cachefile.isOpen())
            _cachefile.remove();
#ifdef Q_OS_WIN
        if (_volumeFile && _volumeFile->IsOpen()) {
            _volumeFile->Close();
        }
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
    // Emit a final progress update to guard against tiny downloads completing
    // before any regular progress callbacks were emitted.
    emit preparationStatusUpdate(tr("Writing image..."));
    _writeComplete();
}

void DownloadThread::_onDownloadError(const QString &msg)
{
    _cancelled = true;
    emit error(msg);
}

void DownloadThread::_onWriteError()
{
#ifdef Q_OS_WIN
    // TODO: Implement platform-specific error handling in FileOperations
    // For now, provide generic error message instead of: if (_file.errorCode() == ERROR_ACCESS_DENIED)
    if (false) // Temporarily disabled
    {
        QString msg = tr("Access denied error while writing file to disk.");
        QSettings registry("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Defender\\Windows Defender Exploit Guard\\Controlled Folder Access",
                           QSettings::Registry64Format);
        if (registry.value("EnableControlledFolderAccess").toInt() == 1)
        {
            msg += "<br>"+tr("Controlled Folder Access seems to be enabled. Please add rpi-imager.exe to the list of allowed apps and try again.");
        }
        else
        {
            msg += "<br>"+tr("The disk may be write-protected or in use by another application. Please ensure the disk is not mounted and try again.");
        }
        _onDownloadError(msg);
    }
    else if (_file->GetLastErrorCode() == ERROR_DISK_FULL)
    {
        _onDownloadError(tr("Disk is full. Please use a larger storage device."));
    }
    else if (_file->GetLastErrorCode() == ERROR_WRITE_PROTECT)
    {
        _onDownloadError(tr("The disk is write-protected. Please check if the disk has a physical write-protect switch or is read-only."));
    }
    else if (_file->GetLastErrorCode() == ERROR_SECTOR_NOT_FOUND || _file->GetLastErrorCode() == ERROR_CRC)
    {
        _onDownloadError(tr("Media error detected. The storage device may be damaged or counterfeit. Please try a different device."));
    }
    else if (_file->GetLastErrorCode() == ERROR_INVALID_PARAMETER)
    {
        _onDownloadError(tr("Invalid disk parameter. The storage device may not be properly recognized. Please try reconnecting the device."));
    }
    else if (_file->GetLastErrorCode() == ERROR_IO_DEVICE)
    {
        _onDownloadError(tr("I/O device error. The storage device may have been disconnected or is malfunctioning."));
    }
    else
    {
        _onDownloadError(tr("Error writing to storage device. Please check if the device is writable, has sufficient space, and is not write-protected."));
    }
#endif
    if (!_cancelled && false) // Disabled the old generic error
        _onDownloadError(tr("Error writing file to disk"));
}

void DownloadThread::_closeFiles()
{
    // Close unified file operations
    if (_file && _file->IsOpen()) {
        _file->Close();
    }
#ifdef Q_OS_WIN
    if (_volumeFile && _volumeFile->IsOpen()) {
        _volumeFile->Close();
    }
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
        
        // Provide more specific error message based on context
        QString errorMsg;
        if (_url.startsWith("file://") && _url.contains("lastdownload.cache"))
        {
            errorMsg = tr("Cached file is corrupt. SHA256 hash does not match expected value.<br>"
                         "The cache file will be removed and the download will restart.");
        }
        else if (_url.startsWith("file://"))
        {
            errorMsg = tr("Local file is corrupt or has incorrect SHA256 hash.<br>"
                         "Expected: %1<br>Actual: %2").arg(QString(_expectedHash), QString(computedHash));
        }
        else
        {
            errorMsg = tr("Download appears to be corrupt. SHA256 hash does not match.<br>"
                         "Expected: %1<br>Actual: %2<br>"
                         "Please check your network connection and try again.").arg(QString(_expectedHash), QString(computedHash));
        }
        
        DownloadThread::_onDownloadError(errorMsg);
        _closeFiles();
        return;
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

    if (_file->Flush() != rpi_imager::FileError::kSuccess)
    {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while flushing)"));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    if (_file->ForceSync() != rpi_imager::FileError::kSuccess) {
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

    if ((!_config.isEmpty() || !_cmdline.isEmpty() || !_firstrun.isEmpty() || !_cloudinit.isEmpty()) && !_initFormat.isEmpty())
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
        _file->Seek(0);
        if (_file->WriteSequential(reinterpret_cast<const std::uint8_t*>(_firstBlock), _firstBlockSize) != rpi_imager::FileError::kSuccess || _file->Flush() != rpi_imager::FileError::kSuccess)
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

    if (_file->Flush() != rpi_imager::FileError::kSuccess)
    {
        DownloadThread::_onDownloadError(tr("Error writing to storage (while flushing)"));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    if (_file->ForceSync() != rpi_imager::FileError::kSuccess) {
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

    emit success();

    if (_ejectEnabled)
    {
        eject_disk(_filename.constData());
    }
}

bool DownloadThread::_verify()
{
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
    //fcntl(_file->GetHandle(), F_SETFL, O_DIRECT | fcntl(_file->GetHandle(), F_GETFL));
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

void DownloadThread::_initializeSyncConfiguration()
{
    _syncConfig = SystemMemoryManager::instance().calculateSyncConfiguration();
}

void DownloadThread::_periodicSync()
{
    qint64 currentBytes = _bytesWritten;
    qint64 bytesSinceLastSync = currentBytes - _lastSyncBytes;
    qint64 timeSinceLastSync = _lastSyncTime.elapsed();
    
    // Sync if we've written more than the configured sync interval
    // OR if it's been more than the time interval since last sync
    // AND we've written at least some data since last sync
    if ((bytesSinceLastSync >= _syncConfig.syncIntervalBytes || 
         (timeSinceLastSync >= _syncConfig.syncIntervalMs && bytesSinceLastSync > 0)) &&
        !_cancelled)
    {
        qDebug() << "Performing periodic sync at" << currentBytes << "bytes written"
                 << "(" << bytesSinceLastSync << "bytes since last sync,"
                 << timeSinceLastSync << "ms elapsed)"
                 << "on" << SystemMemoryManager::instance().getPlatformName();
        
        // Use unified FileOperations for flushing and syncing
        if (_file->Flush() != rpi_imager::FileError::kSuccess) {
            qDebug() << "Warning: Flush() failed during periodic sync";
            return;
        }
        
        // Force filesystem sync using unified FileOperations
        if (_file->ForceSync() != rpi_imager::FileError::kSuccess) {
            qDebug() << "Warning: ForceSync() failed during periodic sync";
            return;
        }
        
        // Update tracking variables
        _lastSyncBytes = currentBytes;
        _lastSyncTime.restart();
        
        qDebug() << "Periodic sync completed successfully";
    }
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
    if (len <= 0) {
        // Use memory-aware optimal input buffer size
        _inputBufferSize = SystemMemoryManager::instance().getOptimalInputBufferSize();
        qDebug() << "Using adaptive input buffer size:" << _inputBufferSize << "bytes";
    } else {
        _inputBufferSize = len;
    }
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

void DownloadThread::setImageCustomisation(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudInitNetwork, const QByteArray &initFormat, const ImageOptions::AdvancedOptions opts)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudInitNetwork;
    _initFormat = initFormat;
    _advancedOptions = opts;
}

bool DownloadThread::_customizeImage()
{
    emit preparationStatusUpdate(tr("Customising OS..."));

    // Use DeviceWrapper with existing FileOperations for image customization
    try
    {
        // Create DeviceWrapper using our existing FileOperations instance directly
        // This avoids opening the device twice and triggering authorization dialogs
        DeviceWrapper dw(_file.get());
        if (_firstBlock)
        {
            // Outsource first block handling to DeviceWrapper.
            // It will still not actually be written out yet,
            // until we call sync(), and then it will
            // save the first 4k sector with MBR for last
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

            for (const QByteArray& item : std::as_const(configItems))
            {
                if (config.contains("#"+item)) {
                    // Uncomment existing line
                    config.replace("#"+item, item);
                } else if (config.contains("\n"+item)) {
                    // config.txt already contains the line
                } else {
                    // Append new line to config.txt
                    if (config.right(1) != QByteArray("\n"))
                        config += "\n"+item+"\n";
                    else
                        config += item+"\n";
                }
            }

            fat->writeFile("config.txt", config);
        }

        // init_format decision is owned by ImageWriter; no auto-detection here

        if (!_firstrun.isEmpty())
        {
            // CustomisationGenerator now creates complete scripts with header and footer
            // No need to add them here anymore
            if (_initFormat == "systemd") {
                fat->writeFile("firstrun.sh", _firstrun);
                _cmdline += " systemd.run=/boot/firstrun.sh systemd.run_success_action=reboot systemd.unit=kernel-command-line.target";
            }
        }

        auto initCloud = _initFormat == "cloudinit" || _initFormat == "cloudinit-rpi";
        if (!_cloudinit.isEmpty() && initCloud)
        {
            _cloudinit = "#cloud-config\n"+_cloudinit;
            fat->writeFile("user-data", _cloudinit);
        }

        if (!_cloudinitNetwork.isEmpty() && initCloud)
        {
            fat->writeFile("network-config", _cloudinitNetwork);
        }

        if (!_cmdline.isEmpty())
        {
            QByteArray cmdline = fat->readFile("cmdline.txt").trimmed();

            cmdline += _cmdline;

            fat->writeFile("cmdline.txt", cmdline);
        }
        
        // Sync before secure boot processing
        dw.sync();
        
        // Generate secure boot files if enabled
        if (_advancedOptions.testFlag(ImageOptions::EnableSecureBoot))
        {
            emit preparationStatusUpdate(tr("Creating signed boot image..."));
            if (!_createSecureBootFiles(fat))
            {
                emit error(tr("Failed to create secure boot files"));
                return false;
            }
        }
    }
    catch (std::runtime_error &err)
    {
        emit error(err.what());
        return false;
    }

    emit finalizing();

    return true;
}

bool DownloadThread::_createSecureBootFiles(DeviceWrapperFatPartition *fat)
{
    qDebug() << "DownloadThread: creating secure boot files";

    // Get RSA key path from settings
    QSettings settings;
    QString rsaKeyPath = settings.value("secureboot_rsa_key").toString();
    
    if (rsaKeyPath.isEmpty()) {
        qDebug() << "DownloadThread: no RSA key configured for secure boot";
        emit error(tr("No RSA key configured for secure boot. Please select a key in App Options."));
        return false;
    }

    // Verify key file exists
    if (!QFile::exists(rsaKeyPath)) {
        qDebug() << "DownloadThread: RSA key file not found:" << rsaKeyPath;
        emit error(tr("RSA key file not found: %1").arg(rsaKeyPath));
        return false;
    }

    // Extract all files from the FAT partition
    emit preparationStatusUpdate(tr("Extracting boot partition files..."));
    QMap<QString, QByteArray> allFiles = SecureBoot::extractFatPartitionFiles(fat);
    
    if (allFiles.isEmpty()) {
        qDebug() << "DownloadThread: no files extracted from boot partition";
        emit error(tr("Failed to extract boot partition files"));
        return false;
    }

    // Separate customization files from boot files
    // Customization files must remain alongside boot.img and boot.sig
    QStringList customizationFiles = {
        "firstrun.sh",      // systemd init customization
        "user-data",        // cloud-init customization
        "meta-data",        // cloud-init metadata
        "network-config"    // cloud-init network customization
    };
    
    QMap<QString, QByteArray> bootFiles;
    QMap<QString, QByteArray> customFiles;
    
    for (auto it = allFiles.constBegin(); it != allFiles.constEnd(); ++it) {
        if (customizationFiles.contains(it.key())) {
            customFiles[it.key()] = it.value();
            qDebug() << "DownloadThread: preserving customization file:" << it.key();
        } else {
            bootFiles[it.key()] = it.value();
        }
    }
    
    if (bootFiles.isEmpty()) {
        qDebug() << "DownloadThread: no boot files to package";
        emit error(tr("No boot files found to package"));
        return false;
    }
    
    qDebug() << "DownloadThread:" << bootFiles.size() << "boot files," 
             << customFiles.size() << "customization files";

    // Create temporary directory for boot.img and boot.sig
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "DownloadThread: failed to create temp directory";
        emit error(tr("Failed to create temporary directory"));
        return false;
    }

    QString bootImgPath = tempDir.path() + "/boot.img";
    QString bootSigPath = tempDir.path() + "/boot.sig";

    // Create boot.img (only with boot files, not customization files)
    emit preparationStatusUpdate(tr("Creating boot.img..."));
    if (!SecureBoot::createBootImg(bootFiles, bootImgPath)) {
        qDebug() << "DownloadThread: failed to create boot.img";
        emit error(tr("Failed to create boot.img"));
        return false;
    }

    // Generate boot.sig
    emit preparationStatusUpdate(tr("Signing boot image..."));
    if (!SecureBoot::generateBootSig(bootImgPath, rsaKeyPath, bootSigPath)) {
        qDebug() << "DownloadThread: failed to generate boot.sig";
        emit error(tr("Failed to generate boot.sig"));
        return false;
    }

    // Read boot.img and boot.sig
    QFile bootImgFile(bootImgPath);
    if (!bootImgFile.open(QIODevice::ReadOnly)) {
        qDebug() << "DownloadThread: failed to read boot.img";
        emit error(tr("Failed to read boot.img"));
        return false;
    }
    QByteArray bootImgData = bootImgFile.readAll();
    bootImgFile.close();

    QFile bootSigFile(bootSigPath);
    if (!bootSigFile.open(QIODevice::ReadOnly)) {
        qDebug() << "DownloadThread: failed to read boot.sig";
        emit error(tr("Failed to read boot.sig"));
        return false;
    }
    QByteArray bootSigData = bootSigFile.readAll();
    bootSigFile.close();

    // Delete ALL original files from the boot partition FIRST (before writing boot.img/boot.sig)
    // This ensures we use the original filenames from extraction, avoiding LFN issues
    // Boot files will be inside boot.img
    // Customization files will remain alongside boot.img and boot.sig
    emit preparationStatusUpdate(tr("Cleaning up boot partition..."));
    
    // Build a list of files to keep (only customization files)
    QSet<QString> filesToKeep;
    for (const QString &customFile : customizationFiles) {
        filesToKeep.insert(customFile.toLower());
    }
    
    // Delete all files except those we want to keep
    // Use the ORIGINAL allFiles map keys for accurate filenames
    int deletedCount = 0;
    int skippedCount = 0;
    QSet<QString> directories;  // Track directories for cleanup
    
    for (auto it = allFiles.constBegin(); it != allFiles.constEnd(); ++it) {
        const QString &filename = it.key();
        
        // Track directories
        if (filename.contains("/")) {
            QString dirName = filename.left(filename.indexOf("/"));
            directories.insert(dirName);
        }
        
        // Skip customization files
        if (filesToKeep.contains(filename.toLower())) {
            qDebug() << "DownloadThread: keeping customization file:" << filename;
            skippedCount++;
            continue;
        }
        
        // Delete everything else
        qDebug() << "DownloadThread: attempting to delete:" << filename;
        if (fat->deleteFile(filename)) {
            deletedCount++;
            if (deletedCount % 50 == 0) {  // Progress update every 50 files
                qDebug() << "DownloadThread: deleted" << deletedCount << "files so far...";
            }
        } else {
            qDebug() << "DownloadThread: WARNING - failed to delete:" << filename;
            // Don't fail the entire process if a file can't be deleted
        }
    }
    
    // Now delete empty directories
    int deletedDirs = 0;
    for (const QString &dirName : directories) {
        if (fat->deleteFile(dirName)) {
            deletedDirs++;
            qDebug() << "DownloadThread: deleted empty directory:" << dirName;
        } else {
            qDebug() << "DownloadThread: warning - failed to delete directory" << dirName;
        }
    }
    
    qDebug() << "DownloadThread: deleted" << deletedCount << "files and" << deletedDirs << "directories from partition";
    qDebug() << "DownloadThread: kept" << skippedCount << "customization files";

    // Sync deletions to disk before writing new files
    emit preparationStatusUpdate(tr("Syncing deletions to disk..."));
    qDebug() << "DownloadThread: syncing deletions to disk";
    fat->deviceWrapper()->sync();
    qDebug() << "DownloadThread: sync complete";

    // NOW write boot.img and boot.sig to the cleaned partition
    emit preparationStatusUpdate(tr("Writing signed boot files..."));
    try {
        fat->writeFile("boot.img", bootImgData);
        fat->writeFile("boot.sig", bootSigData);
        qDebug() << "DownloadThread: secure boot files written successfully";
    }
    catch (std::runtime_error &err) {
        qDebug() << "DownloadThread: failed to write secure boot files:" << err.what();
        emit error(tr("Failed to write secure boot files: %1").arg(err.what()));
        return false;
    }

    // Explicitly write customization files back to ensure they're not corrupted or lost
    // This is critical because we extracted them earlier and they may have been affected
    // by the file deletion process or file system operations
    if (!customFiles.isEmpty()) {
        emit preparationStatusUpdate(tr("Writing customization files..."));
        qDebug() << "DownloadThread: writing" << customFiles.size() << "customization files back";
        for (auto it = customFiles.constBegin(); it != customFiles.constEnd(); ++it) {
            try {
                fat->writeFile(it.key(), it.value());
                qDebug() << "DownloadThread: wrote customization file:" << it.key();
            }
            catch (std::runtime_error &err) {
                qDebug() << "DownloadThread: WARNING - failed to write customization file" << it.key() << ":" << err.what();
                // Don't fail the entire process, but log the warning
                // The file might still exist from before, but it could be corrupted
            }
        }
    }

    return true;
}
