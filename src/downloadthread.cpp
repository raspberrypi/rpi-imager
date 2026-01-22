/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadthread.h"
#include "aligned_buffer.h"
#include "config.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "systemmemorymanager.h"
#include "timeout_utils.h"
#include "platformquirks.h"
#include "drivelist/drivelist.h"
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
#include <future>
#include <chrono>
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
#include "windows/file_operations_windows.h"
#endif

#include "imageadvancedoptions.h"
#include "secureboot.h"
#include "curlnetworkconfig.h"
#include <QTemporaryDir>

#ifdef Q_OS_LINUX
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

using namespace std;

// Using the timeout utility for unblocking stuck syscalls
using rpi_imager::TimeoutResult;
using rpi_imager::TimeoutConfig;
using rpi_imager::runWithTimeout;
using rpi_imager::TimeoutDefaults::kHardTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kMemoryCheckIntervalMs;
using rpi_imager::TimeoutDefaults::kCriticalMemoryMB;

QByteArray DownloadThread::_proxy;

DownloadThread::DownloadThread(const QByteArray &url, const QByteArray &localfilename, const QByteArray &expectedHash, QObject *parent) :
    QThread(parent), _startOffset(0), _lastDlTotal(0), _lastDlNow(0), _extractTotal(0), _verifyTotal(0), _lastVerifyNow(0), _bytesWritten(0), _lastFailureOffset(0), _sectorsStart(-1), _url(url), _filename(localfilename), _expectedHash(expectedHash),
    _firstBlock(nullptr), _cancelled(false), _successful(false), _verifyEnabled(false), _cacheEnabled(false), _lastModified(0), _serverTime(0),  _lastFailureTime(0),
    _inputBufferSize(SystemMemoryManager::instance().getOptimalInputBufferSize()), _writehash(OSLIST_HASH_ALGORITHM), _verifyhash(OSLIST_HASH_ALGORITHM),
    _hasPendingHash(false)
{
    // Ensure libcurl is initialized (handled centrally by CurlNetworkConfig)
    CurlNetworkConfig::ensureInitialized();

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
    
    // Initialize write timing tracking
    _writeTimingStats.reset();
    _lastWriteTimer.start();
    _lastWriteBytes = 0;
    
    // Initialize debug options with defaults
    _debugDirectIO = true;      // Match current behavior
    _debugPeriodicSync = true;  // Match current behavior
    _debugVerboseLogging = false;
    _debugAsyncIO = true;       // Enable async I/O by default for performance
    _debugAsyncQueueDepth = 16; // Default queue depth
    _debugIPv4Only = false;     // Use both IPv4 and IPv6 by default
    _debugSkipEndOfDevice = false; // For counterfeit cards with fake capacity
    
    // Initialize bottleneck detection
    _currentBottleneck = BottleneckState::None;
    _bottleneckTimer.start();
}

DownloadThread::~DownloadThread()
{
    _cancelled = true;
    wait();
    
    // Wait for any pending hash computation to complete before destroying
    if (_hasPendingHash) {
        _pendingHashFuture.waitForFinished();
        _hasPendingHash = false;
    }
    
    // Close unified file operations
    if (_file && _file->IsOpen()) {
        _file->Close();
    }
#ifdef Q_OS_WIN
    if (_volumeFile && _volumeFile->IsOpen()) {
        _volumeFile->Close();
    }
#endif
    
    // Cancel async cache writer if still running
    if (_asyncCacheWriter) {
        _asyncCacheWriter->cancel();
        _asyncCacheWriter.reset();
    }
    
    // Use _closeFiles() to ensure cache file is properly closed
    _closeFiles();

    if (_firstBlock)
        qFreeAligned(_firstBlock);
    
    // Note: curl_global_cleanup() is not called here - it happens at process exit.
    // This is safe and avoids issues with multiple DownloadThread instances.
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
    QElapsedTimer unmountTimer;
    QElapsedTimer openTimer;
    
    if (_filename.startsWith("/dev/"))
    {
        emit preparationStatusUpdate(tr("Unmounting drive..."));
        unmountTimer.start();
        // Use block device path for unmount (e.g., /dev/disk on macOS, not /dev/rdisk)
        // DADiskUnmount with kDADiskUnmountOptionWhole automatically unmounts 
        // all child volumes (APFS containers, partitions, etc.)
        QString unmountPath = PlatformQuirks::getEjectDevicePath(_filename);
        qDebug() << "Unmounting:" << unmountPath;
        PlatformQuirks::DiskResult unmountResult = PlatformQuirks::unmountDisk(unmountPath);
        bool unmountSuccess = (unmountResult == PlatformQuirks::DiskResult::Success);
        emit eventDriveUnmount(static_cast<quint32>(unmountTimer.elapsed()), unmountSuccess);
        
        if (!unmountSuccess) {
            qDebug() << "Unmount failed with result:" << static_cast<int>(unmountResult);
#ifdef Q_OS_DARWIN
            emit error(tr("Failed to unmount disk '%1'. Please close any applications using the disk and try again.").arg(unmountPath));
#else
            emit error(tr("Failed to unmount disk '%1'.").arg(unmountPath));
#endif
            return false;
        }
    }
    emit preparationStatusUpdate(tr("Opening drive..."));
    openTimer.start();
    QElapsedTimer authTimer;  // For timing authorization separately
    authTimer.start();

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

            // Create timing callback to emit performance events
            auto timingCallback = [this](const QString &eventName, quint32 durationMs, bool success) {
                if (eventName == "driveUnmountVolumes") {
                    emit eventDriveUnmountVolumes(durationMs, success);
                } else if (eventName == "driveDiskClean") {
                    emit eventDriveDiskClean(durationMs, success);
                } else if (eventName == "driveRescan") {
                    emit eventDriveRescan(durationMs, success);
                }
            };

            // Unmount volumes first (with performance instrumentation)
            emit preparationStatusUpdate(tr("Unmounting volumes..."));
            auto unmountResult = DiskpartUtil::unmountVolumes(_filename, timingCallback);
            if (!unmountResult.success)
            {
                qDebug() << "Warning: Volume unmount had issues:" << unmountResult.errorMessage;
                // Continue anyway - cleanDiskFast may still succeed
            }

            // Clean disk using fast direct IOCTL method (replaces diskpart)
            emit preparationStatusUpdate(tr("Cleaning disk..."));
            auto cleanResult = DiskpartUtil::cleanDiskFast(_filename, timingCallback);
            if (!cleanResult.success)
            {
                // Fall back to legacy diskpart method if direct IOCTLs fail
                qDebug() << "Fast disk clean failed, falling back to diskpart:" << cleanResult.errorMessage;
                emit preparationStatusUpdate(tr("Cleaning disk (legacy method)..."));
                cleanResult = DiskpartUtil::cleanDisk(_filename, std::chrono::seconds(60), 3, DiskpartUtil::VolumeHandling::SkipUnmounting);
                if (!cleanResult.success)
                {
                    emit error(cleanResult.errorMessage);
                    return false;
                }
            }
        }
    }

    // Re-scan for drive letters after diskpart
    auto l = Drivelist::ListStorageDevices();
    QByteArray canonicalDevice = PlatformQuirks::getEjectDevicePath(_filename).toLower().toUtf8();
    QByteArray driveLetter;
    for (auto i : l)
    {
        if (QByteArray::fromStdString(i.device).toLower() == canonicalDevice)
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

    // Device path is already platform-optimized by caller (e.g., rdisk on macOS)
    rpi_imager::FileError result = _file->OpenDevice(filename_str);
    qint64 authOpenMs = authTimer.elapsed();
    qDebug() << "Device authorization and open took" << authOpenMs << "ms";
    
    if (result != rpi_imager::FileError::kSuccess)
    {
#ifdef Q_OS_DARWIN
        // On macOS, this might be due to permission issues
        QString msg = tr("Error opening disk device '%1'").arg(QString(_filename));
        msg += "<br>"+tr("Please verify if 'Raspberry Pi Imager' is allowed access to 'removable volumes' in privacy settings (under 'files and folders' or alternatively give it 'full disk access').");
        QStringList args("x-apple.systempreferences:com.apple.preference.security?Privacy_RemovableVolume");
        QProcess::execute("open", args);
        emit error(msg);
#elif defined(Q_OS_LINUX)
        emit error(tr("Cannot open storage device '%1'. Please run with elevated privileges (sudo).").arg(QString(_filename)));
#else
        emit error(tr("Cannot open storage device '%1'.").arg(QString(_filename)));
#endif
        emit eventDriveAuthorization(static_cast<quint32>(authOpenMs), false);
        return false;
    }
    
    // Emit authorization timing event (success case)
    emit eventDriveAuthorization(static_cast<quint32>(authOpenMs), true);
    
    // Apply debug option for direct I/O after opening
    // By default, OpenDevice enables direct I/O for block devices
    // This allows toggling it off via the secret debug menu
    if (!_debugDirectIO && _file->IsDirectIOEnabled()) {
        qDebug() << "Debug: Disabling direct I/O as per debug options";
        _file->SetDirectIOEnabled(false);
    } else if (_debugDirectIO && !_file->IsDirectIOEnabled()) {
        qDebug() << "Debug: Enabling direct I/O as per debug options";
        _file->SetDirectIOEnabled(true);
    }
    
    // Configure async I/O if enabled
    if (_debugAsyncIO && _file->IsAsyncIOSupported()) {
        bool asyncConfigured = _file->SetAsyncQueueDepth(_debugAsyncQueueDepth);
        qDebug() << "Async I/O:" << (asyncConfigured ? "configured" : "failed to configure")
                 << "with queue depth" << _debugAsyncQueueDepth;
    } else if (_debugAsyncIO) {
        qDebug() << "Async I/O requested but not supported on this platform";
    }

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

        if (_debugSkipEndOfDevice)
        {
            qDebug() << "Skipping BLKDISCARD (debug: skip end-of-device operations for counterfeit card support)";
        }
        else if (discardmax.isEmpty() || discardmax == "0")
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
                
                // BLKDISCARD can take a long time on large/slow devices, and can hang 
                // on counterfeit cards. Use timeout to unblock if it hangs.
                // Note: User-facing warnings are handled by WriteProgressWatchdog
                int discardResult = -1;
                auto timeoutResult = runWithTimeout(
                    [fd, &range]() { return ::ioctl(fd, BLKDISCARD, &range); },
                    discardResult,
                    TimeoutConfig(kHardTimeoutSeconds).withCancelFlag(&_cancelled)
                );
                
                switch (timeoutResult) {
                    case TimeoutResult::Completed:
                        if (discardResult == -1) {
                            qDebug() << "BLKDISCARD failed:" << strerror(errno);
                        } else {
                            qDebug() << "BLKDISCARD successful in" << _timer.elapsed() / 1000 << "s";
                        }
                        break;
                    case TimeoutResult::TimedOut:
                        qDebug() << "BLKDISCARD timed out after" << kHardTimeoutSeconds << "s";
                        // Continue anyway - BLKDISCARD is optional
                        break;
                    case TimeoutResult::Cancelled:
                        qDebug() << "BLKDISCARD cancelled";
                        return false;
                }
            }
        }
    }
#endif

#ifndef Q_OS_WIN
    // Zero out MBR using unified FileOperations
    QElapsedTimer mbrTimer;
    mbrTimer.start();
    
    std::uint64_t knownsize = 0;
    rpi_imager::FileError sizeResult = _file->GetSize(knownsize);
    if (sizeResult != rpi_imager::FileError::kSuccess) {
        emit error(_fileErrorToString(sizeResult, tr("getting device size")));
        return false;
    }
    
    // Use aligned buffer for O_DIRECT compatibility on Linux
    // O_DIRECT requires buffers to be aligned to the filesystem's logical block size
    constexpr size_t emptyMBSize = 1024 * 1024;
    rpi_imager::AlignedBuffer emptyMB(emptyMBSize);
    if (!emptyMB) {
        emit error(tr("Failed to allocate buffer for MBR zeroing.\n\n"
                     "The system may be low on memory."));
        return false;
    }
    
    emit preparationStatusUpdate(tr("Zero'ing out first and last MB of drive..."));
    qDebug() << "Zeroing out first and last MB of drive";
    _timer.start();

    rpi_imager::FileError mbrWriteResult = _file->WriteSequential(emptyMB.data(), emptyMBSize);
    rpi_imager::FileError mbrFlushResult = (mbrWriteResult == rpi_imager::FileError::kSuccess) ? _file->Flush() : mbrWriteResult;
    if (mbrWriteResult != rpi_imager::FileError::kSuccess || mbrFlushResult != rpi_imager::FileError::kSuccess)
    {
        rpi_imager::FileError errorToReport = (mbrWriteResult != rpi_imager::FileError::kSuccess) ? mbrWriteResult : mbrFlushResult;
        emit error(_fileErrorToString(errorToReport, tr("preparing storage device")));
        return false;
    }
    qint64 firstMBMs = _timer.elapsed();
    qDebug() << "  First MB + flush took" << firstMBMs << "ms";

    // Zero out last part of card (may have GPT backup table)
    // This operation can hang indefinitely on counterfeit SD cards that report
    // a fake capacity larger than their actual storage, so we use a timeout.
    // Can be skipped via debug option for users with counterfeit cards.
    if (_debugSkipEndOfDevice)
    {
        qDebug() << "Skipping last MB zeroing (debug: skip end-of-device operations for counterfeit card support)";
    }
    else if (knownsize > emptyMBSize)
    {
        _timer.restart();
        emit preparationStatusUpdate(tr("Zero'ing out end of drive..."));
        
        // Capture needed values for the lambda
        auto file = _file.get();
        const uint8_t* bufferData = emptyMB.data();
        uint64_t seekPosition = knownsize - emptyMBSize;
        
        // Write to end of device can hang on counterfeit cards with fake capacity
        // Use timeout to detect counterfeit cards with fake capacity
        int lastMBResultInt = 0;
        auto timeoutResult = runWithTimeout(
            [file, seekPosition, bufferData, emptyMBSize]() {
                if (file->Seek(seekPosition) != rpi_imager::FileError::kSuccess)
                    return static_cast<int>(rpi_imager::FileError::kSeekError);
                if (file->WriteSequential(bufferData, emptyMBSize) != rpi_imager::FileError::kSuccess)
                    return static_cast<int>(rpi_imager::FileError::kWriteError);
                if (file->Flush() != rpi_imager::FileError::kSuccess)
                    return static_cast<int>(rpi_imager::FileError::kFlushError);
                if (file->ForceSync() != rpi_imager::FileError::kSuccess)
                    return static_cast<int>(rpi_imager::FileError::kSyncError);
                return static_cast<int>(rpi_imager::FileError::kSuccess);
            },
            lastMBResultInt,
            TimeoutConfig(kHardTimeoutSeconds).withCancelFlag(&_cancelled)
        );
        
        if (timeoutResult == TimeoutResult::Cancelled) {
            qDebug() << "Last MB write cancelled";
            return false;
        }
        
        if (timeoutResult == TimeoutResult::TimedOut) {
            emit error(tr("Timeout writing to end of storage device.\n\n"
                          "This may indicate a counterfeit SD card with fake capacity.\n\n"
                          "Please try a different storage device."));
            return false;
        }
        
        auto lastMBResult = static_cast<rpi_imager::FileError>(lastMBResultInt);
        if (lastMBResult != rpi_imager::FileError::kSuccess) {
            emit error(tr("Write error while trying to zero out last part of card.\n\n"
                          "This could indicate the card is advertising wrong capacity "
                          "(possible counterfeit).\n\n"
                          "Please try a different storage device."));
            return false;
        }
        qDebug() << "  Last MB + flush + sync took" << _timer.elapsed() << "ms";
    }
    _file->Seek(0);
    qint64 mbrTotalMs = mbrTimer.elapsed();
    qDebug() << "Done zero'ing out start and end of drive. Total MBR prep:" << mbrTotalMs << "ms";
    
    // Emit MBR zeroing performance event with detailed breakdown
    QString mbrMetadata = QString("first_mb_ms: %1; last_mb_ms: %2; device_size_mb: %3")
        .arg(firstMBMs)
        .arg(_timer.elapsed())  // Last MB timing (from last _timer.restart)
        .arg(knownsize / (1024 * 1024));
    emit eventDriveMbrZeroing(static_cast<quint32>(mbrTotalMs), true, mbrMetadata);
#endif

#ifdef Q_OS_LINUX
    _sectorsStart = _sectorsWritten();
#endif

    // Include I/O mode in drive open event for diagnostics
    QString ioModeMetadata = QString("direct_io: %1; platform: %2")
        .arg(_file->IsDirectIOEnabled() ? "yes" : "no")
        .arg(SystemMemoryManager::instance().getPlatformName());
    emit eventDriveOpen(static_cast<quint32>(openTimer.elapsed()), true, ioModeMetadata);
    
    // Emit detailed direct I/O attempt info for performance analysis
    auto directIOInfo = _file->GetDirectIOInfo();
    emit eventDirectIOAttempt(
        directIOInfo.attempted,
        directIOInfo.succeeded,
        directIOInfo.currently_enabled,
        directIOInfo.error_code,
        QString::fromStdString(directIOInfo.error_message));
    
    return true;
}

void DownloadThread::run()
{
#ifdef Q_OS_WIN
    // Suppress Windows "Insert a disk" / "not accessible" system error dialogs
    // for this thread. Error mode is per-thread, so we set it once at thread start.
    DWORD oldMode;
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode)) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    }
#endif

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
    
    // Enable HTTP/2 for HTTPS connections (falls back to HTTP/1.1 for plain HTTP)
    // Benefits: header compression, better connection utilization, multiplexing
    // Note: We track HTTP/2 failures and fall back to HTTP/1.1 if needed (see retry loop below)
    curl_easy_setopt(_c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    
    // Enable TCP keepalive to detect dead connections faster
    curl_easy_setopt(_c, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(_c, CURLOPT_TCP_KEEPIDLE, 30L);   // Start keepalive after 30s idle
    curl_easy_setopt(_c, CURLOPT_TCP_KEEPINTVL, 15L);  // Send keepalive every 15s
    
    // IPv4-only mode for users with broken IPv6 routing
    // Check both local setting and shared config (shared config is updated via debug menu)
    if (_debugIPv4Only || CurlNetworkConfig::instance().ipv4Only())
    {
        curl_easy_setopt(_c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        qDebug() << "Using IPv4-only mode for download";
    }
    
    // Track HTTP/2 failures for graceful fallback
    int http2FailureCount = 0;
    const int MAX_HTTP2_FAILURES = 3;
    
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

#ifdef Q_OS_LINUX
    // Set CA certificate bundle path for AppImage compatibility.
    // AppImages bundle libcurl which may have a hardcoded CA path that doesn't
    // exist on the target system. Find and use the system's CA bundle.
    const char* caBundle = PlatformQuirks::findCACertBundle();
    if (caBundle)
    {
        curl_easy_setopt(_c, CURLOPT_CAINFO, caBundle);
        qDebug() << "Using CA certificate bundle:" << caBundle;
    }
#endif

    emit preparationStatusUpdate(tr("Starting download..."));
    // Minimal logging during normal operation
    _timer.start();
    CURLcode ret = curl_easy_perform(_c);

    /* Deal with badly configured HTTP servers that terminate the connection quickly
       if connections stalls for some seconds while kernel commits buffers to slow SD card.
       And also reconnect if we detect from our end that transfer stalled for more than one minute */
    while (ret == CURLE_PARTIAL_FILE || ret == CURLE_OPERATION_TIMEDOUT
           || (ret == CURLE_HTTP2_STREAM && _lastDlNow != _lastFailureOffset)
           || (ret == CURLE_HTTP2 && _lastDlNow != _lastFailureOffset)
           || (ret == CURLE_RECV_ERROR && _lastDlNow != _lastFailureOffset) )
    {
        time_t t = time(NULL);
        qDebug() << "HTTP connection lost. Error:" << curl_easy_strerror(ret) << "Time:" << t;

        // Track HTTP/2 specific failures for graceful fallback
        if (ret == CURLE_HTTP2_STREAM || ret == CURLE_HTTP2) {
            http2FailureCount++;
            qDebug() << "HTTP/2 failure count:" << http2FailureCount << "/" << MAX_HTTP2_FAILURES;
            
            if (http2FailureCount >= MAX_HTTP2_FAILURES) {
                qDebug() << "Too many HTTP/2 failures, falling back to HTTP/1.1";
                curl_easy_setopt(_c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
            }
        }

        /* If last failure happened less than 5 seconds ago, something else may
           be wrong. Sleep some time to prevent hammering server */
        quint32 sleepMs = 0;
        if (t - _lastFailureTime < 5)
        {
            qDebug() << "Sleeping 5 seconds";
            sleepMs = 5000;
            ::sleep(5);
        }
        
        // Emit network retry event for performance tracking
        QString retryMetadata = QString("error: %1; offset: %2 MB; http2_failures: %3")
            .arg(curl_easy_strerror(ret))
            .arg(_lastDlNow / (1024 * 1024))
            .arg(http2FailureCount);
        emit eventNetworkRetry(sleepMs, retryMetadata);
        
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
        {
            _successful = true;
            
            // Collect CURL connection timing metrics for performance analysis
            double dnsTime = 0, connectTime = 0, tlsTime = 0, startTransferTime = 0, totalTime = 0;
            curl_off_t downloadSpeed = 0;
            curl_off_t downloadSize = 0;
            long httpVersion = 0;
            
            curl_easy_getinfo(_c, CURLINFO_NAMELOOKUP_TIME, &dnsTime);
            curl_easy_getinfo(_c, CURLINFO_CONNECT_TIME, &connectTime);
            curl_easy_getinfo(_c, CURLINFO_APPCONNECT_TIME, &tlsTime);
            curl_easy_getinfo(_c, CURLINFO_STARTTRANSFER_TIME, &startTransferTime);
            curl_easy_getinfo(_c, CURLINFO_TOTAL_TIME, &totalTime);
            curl_easy_getinfo(_c, CURLINFO_SPEED_DOWNLOAD_T, &downloadSpeed);
            curl_easy_getinfo(_c, CURLINFO_SIZE_DOWNLOAD_T, &downloadSize);
            curl_easy_getinfo(_c, CURLINFO_HTTP_VERSION, &httpVersion);
            
            const char* versionStr = "unknown";
            switch (httpVersion) {
                case CURL_HTTP_VERSION_1_0: versionStr = "HTTP/1.0"; break;
                case CURL_HTTP_VERSION_1_1: versionStr = "HTTP/1.1"; break;
                case CURL_HTTP_VERSION_2_0: versionStr = "HTTP/2"; break;
                case CURL_HTTP_VERSION_3: versionStr = "HTTP/3"; break;
                default: break;
            }
            
            qDebug() << "Download done in" << _timer.elapsed() / 1000 << "seconds using" << versionStr;
            
            // Emit connection stats for performance tracking
            // Times are in seconds from CURL, convert to ms for consistency
            QString statsMetadata = QString("dns_ms: %1; connect_ms: %2; tls_ms: %3; ttfb_ms: %4; total_ms: %5; speed_kbps: %6; size_bytes: %7; http: %8")
                .arg(static_cast<int>(dnsTime * 1000))
                .arg(static_cast<int>(connectTime * 1000))
                .arg(static_cast<int>(tlsTime * 1000))
                .arg(static_cast<int>(startTransferTime * 1000))
                .arg(static_cast<int>(totalTime * 1000))
                .arg(static_cast<qint64>(downloadSpeed / 1024))
                .arg(static_cast<qint64>(downloadSize))
                .arg(versionStr);
            emit eventNetworkConnectionStats(statsMetadata);
            
            _onDownloadSuccess();
            break;
        }
        case CURLE_WRITE_ERROR:
            deleteDownloadedFile();
            // If cancelled, treat write error as clean abort (user-initiated cancellation
            // triggers CURLE_WRITE_ERROR because _writeData returns 0)
            if (!_cancelled)
                _onWriteError();
            break;
        case CURLE_ABORTED_BY_CALLBACK:
            // Clean cancellation via progress callback
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
    // Abort CURL cleanly if cancelled - returning 0 triggers CURLE_WRITE_ERROR
    // which we handle by checking _cancelled before showing error dialogs
    if (_cancelled)
        return 0;

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

    // Check if async writer exists and is still healthy
    if (!_asyncCacheWriter) {
        _cacheEnabled = false;
        return;
    }
    
    // Check for async errors that may have occurred in the writer thread
    if (_asyncCacheWriter->hasError()) {
        qDebug() << "Async cache writer has error state. Disabling caching.";
        _cacheEnabled = false;
        // Don't call cancel() here - it can block for 5+ seconds waiting for the
        // writer thread to stop, which would stall the curl download callback.
        // Cleanup will happen in _closeFiles() or destructor.
        return;
    }

    // Use async cache writer for non-blocking I/O
    if (_asyncCacheWriter->isActive()) {
        if (!_asyncCacheWriter->write(buf, len)) {
            // write() returns false on backpressure timeout or error
            if (_asyncCacheWriter->wasDisabledDueToBackpressure()) {
                qDebug() << "Cache I/O too slow (backpressure). Disabling caching to avoid blocking download.";
            } else {
                qDebug() << "Async cache writer failed. Disabling caching.";
            }
            _cacheEnabled = false;
            // Don't call cancel() here - it can block for 5+ seconds waiting for the
            // writer thread to stop, which would stall the curl download callback.
            // The cache writer will detect _hasError and clean up on its own, or
            // cleanup will happen in _closeFiles() when the download completes.
        }
    }
}

void DownloadThread::setCacheFile(const QString &filename, qint64 filesize)
{
    _cacheFilename = filename;
    
    // Create async cache writer
    _asyncCacheWriter = std::make_unique<AsyncCacheWriter>(this);
    
    // Connect error signal for async error propagation from writer thread
    // Using Qt::QueuedConnection to ensure thread-safe signal delivery
    connect(_asyncCacheWriter.get(), &AsyncCacheWriter::error, 
            this, [this](const QString &msg) {
                qDebug() << "AsyncCacheWriter error (async):" << msg;
                _cacheEnabled = false;
                // Note: Don't cancel here - we're in a different thread context
                // The writer thread will clean up on its own
            }, Qt::QueuedConnection);
    
    if (_asyncCacheWriter->open(filename, filesize))
    {
        _cacheEnabled = true;
        qDebug() << "Async cache writer initialized for" << filename;
    }
    else
    {
        qDebug() << "Error opening cache file for writing. Disabling caching.";
        _asyncCacheWriter.reset();
    }
}

void DownloadThread::_hashData(const char *buf, size_t len)
{
    _writehash.addData(buf, len);
}

size_t DownloadThread::_writeFile(const char *buf, size_t len, WriteCompleteCallback onComplete)
{
    if (_cancelled) {
        if (onComplete) onComplete();
        return len;
    }

    if (!_firstBlock)
    {
        _writehash.addData(buf, len);
        _firstBlock = (char *) qMallocAligned(len, 4096);
        _firstBlockSize = len;
        ::memcpy(_firstBlock, buf, len);
        qDebug() << "_writeFile: captured first block (" << len << ") and advanced file offset via seek";
        if (onComplete) onComplete();
        return (_file->Seek(len) == rpi_imager::FileError::kSuccess) ? len : 0;
    }

    QElapsedTimer opTimer;
    quint64 preHashWaitMs = 0;
    quint64 syscallMs = 0;
    quint64 postHashWaitMs = 0;
    
    // Track write size statistics
    quint32 lenU32 = static_cast<quint32>(len);
    _writeTimingStats.writeCount.fetch_add(1);
    _writeTimingStats.totalBytesWritten.fetch_add(len);
    
    // Update min/max write size (lock-free compare-and-swap)
    quint32 currentMin = _writeTimingStats.minWriteSizeBytes.load();
    while (lenU32 < currentMin && !_writeTimingStats.minWriteSizeBytes.compare_exchange_weak(currentMin, lenU32)) {}
    quint32 currentMax = _writeTimingStats.maxWriteSizeBytes.load();
    while (lenU32 > currentMax && !_writeTimingStats.maxWriteSizeBytes.compare_exchange_weak(currentMax, lenU32)) {}

    // Pipelined hash computation: wait for PREVIOUS hash before starting current one
    if (_hasPendingHash) {
        if (!_pendingHashFuture.isFinished()) {
            opTimer.start();
            _pendingHashFuture.waitForFinished();
            preHashWaitMs = static_cast<quint64>(opTimer.elapsed());
            _writeTimingStats.totalPreHashWaitMs.fetch_add(preHashWaitMs);
            
            if (preHashWaitMs > 10) {
                qDebug() << "Hash pipeline stall: waited" << preHashWaitMs << "ms for previous hash";
            }
        }
    }

    // Start hash computation for current buffer
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    _pendingHashFuture = QtConcurrent::run(&DownloadThread::_hashData, this, buf, len);
#else
    _pendingHashFuture = QtConcurrent::run(this, &DownloadThread::_hashData, buf, len);
#endif
    _hasPendingHash = true;

    // Determine if we can use zero-copy async I/O
    opTimer.start();
    size_t bytes_written = 0;
    rpi_imager::FileError write_result;
    
    bool useAsync = _debugAsyncIO && _file->IsAsyncIOSupported() && _file->GetAsyncQueueDepth() > 1;
    bool useZeroCopy = useAsync && onComplete;  // Zero-copy requires completion callback
    
    if (useZeroCopy) {
        // ZERO-COPY ASYNC: Use caller's buffer directly, release via callback
        // This is the optimal path when using ring buffer slots as async I/O buffers
        // 
        // IMPORTANT: The buffer is used by both the async write AND the hash computation.
        // We must wait for BOTH to complete before releasing the buffer.
        // Capture the hash future so the completion callback can wait for it.
        size_t writeLen = len;
        QFuture<void> hashFuture = _pendingHashFuture;
        
        // Capture pointer to _bytesWritten for callback to update on completion
        // This ensures progress reflects COMPLETED writes, not just queued writes
        std::atomic<std::uint64_t>* bytesWrittenPtr = &_bytesWritten;
        
        // Capture 'this' and total for event-driven progress notification
        // The signal emission is thread-safe via Qt::QueuedConnection
        DownloadThread* self = this;
        quint64 totalBytes = _extractTotal.load();
        
        write_result = _file->AsyncWriteSequential(
            reinterpret_cast<const std::uint8_t*>(buf), len,
            [onComplete, writeLen, hashFuture, bytesWrittenPtr, self, totalBytes](rpi_imager::FileError result, std::size_t written) mutable {
                // Wait for hash computation to complete before releasing buffer
                // This ensures the buffer isn't reused while still being hashed
                if (!hashFuture.isFinished()) {
                    hashFuture.waitForFinished();
                }
                
                // Update progress when write actually completes (not when queued)
                if (result == rpi_imager::FileError::kSuccess) {
                    quint64 newTotal = bytesWrittenPtr->fetch_add(written) + written;
                    // Emit progress signal - thread-safe via queued connection to main thread
                    emit self->asyncWriteProgress(newTotal, totalBytes);
                }
                
                // Now safe to release the buffer
                if (onComplete) onComplete();
                if (result != rpi_imager::FileError::kSuccess) {
                    qDebug() << "Async write (zero-copy): error" << static_cast<int>(result)
                             << "expected" << writeLen << "wrote" << written;
                }
            });
        
        if (write_result == rpi_imager::FileError::kSuccess) {
            bytes_written = len;
            // Don't increment _bytesWritten here - callback will do it on completion
        } else {
            qDebug() << "Async write queue failed, falling back to sync";
            // Fallback to sync - wait for hash before releasing
            if (!_pendingHashFuture.isFinished()) {
                _pendingHashFuture.waitForFinished();
            }
            write_result = _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(buf), len);
            if (write_result == rpi_imager::FileError::kSuccess) {
                bytes_written = len;
                _bytesWritten += bytes_written;
            }
            // Call completion callback since we're done synchronously
            if (onComplete) onComplete();
        }
    } else if (useAsync) {
        // ASYNC WITH COPY: No completion callback, must copy buffer for safety
        // This path is used when caller expects buffer to be free after return
        std::uint8_t* asyncBuf = static_cast<std::uint8_t*>(qMallocAligned(len, 4096));
        if (asyncBuf) {
            ::memcpy(asyncBuf, buf, len);
            
            size_t writeLen = len;
            // Capture pointer to _bytesWritten for callback to update on completion
            std::atomic<std::uint64_t>* bytesWrittenPtr = &_bytesWritten;
            
            // Capture 'this' and total for event-driven progress notification
            DownloadThread* self = this;
            quint64 totalBytes = _extractTotal.load();
            
            write_result = _file->AsyncWriteSequential(asyncBuf, len, 
                [asyncBuf, writeLen, bytesWrittenPtr, self, totalBytes](rpi_imager::FileError result, std::size_t written) {
                    // Update progress when write actually completes (not when queued)
                    if (result == rpi_imager::FileError::kSuccess) {
                        quint64 newTotal = bytesWrittenPtr->fetch_add(written) + written;
                        // Emit progress signal - thread-safe via queued connection to main thread
                        emit self->asyncWriteProgress(newTotal, totalBytes);
                    }
                    qFreeAligned(asyncBuf);
                    if (result != rpi_imager::FileError::kSuccess) {
                        qDebug() << "Async write callback: error" << static_cast<int>(result) 
                                 << "expected" << writeLen << "wrote" << written;
                    }
                });
            
            if (write_result == rpi_imager::FileError::kSuccess) {
                bytes_written = len;
                // Don't increment _bytesWritten here - callback will do it on completion
            } else {
                qFreeAligned(asyncBuf);
                qDebug() << "Async write queue failed with error" << static_cast<int>(write_result);
            }
        } else {
            qDebug() << "Async buffer allocation failed, falling back to sync";
            write_result = _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(buf), len);
            if (write_result == rpi_imager::FileError::kSuccess) {
                bytes_written = len;
                _bytesWritten += bytes_written;
            }
        }
    } else {
        // SYNCHRONOUS WRITE: Original path
        write_result = _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(buf), len);
        if (write_result == rpi_imager::FileError::kSuccess) {
            bytes_written = len;
            _bytesWritten += bytes_written;
        } else {
            qDebug() << "Write error: FileOperations write failed with error code" << static_cast<int>(write_result) << "while writing len:" << len;
        }
        // For sync writes, call completion immediately
        if (onComplete) onComplete();
    }
    
    syscallMs = static_cast<quint64>(opTimer.elapsed());
    _writeTimingStats.totalSyscallMs.fetch_add(syscallMs);

    qint64 written = static_cast<qint64>(bytes_written);

    // Wait for current hash to complete before returning
    // For zero-copy async, the buffer stays valid until onComplete is called
    // For sync/copy-async, this ensures buffer safety for callers without callback
    if (!useZeroCopy && _hasPendingHash && !_pendingHashFuture.isFinished()) {
        opTimer.start();
        _pendingHashFuture.waitForFinished();
        postHashWaitMs = static_cast<quint64>(opTimer.elapsed());
        _writeTimingStats.totalPostHashWaitMs.fetch_add(postHashWaitMs);
    }

    // Calculate instantaneous throughput for sync impact analysis
    qint64 timeSinceLastWriteMs = _lastWriteTimer.elapsed();
    if (timeSinceLastWriteMs > 0 && bytes_written > 0) {
        quint32 throughputKBps = static_cast<quint32>((bytes_written * 1000) / (timeSinceLastWriteMs * 1024));
        
        quint32 writesUntilSync = _writeTimingStats.writesUntilNextSync.load();
        if (writesUntilSync > 0) {
            _writeTimingStats.throughputSamplesAfterSync.fetch_add(throughputKBps);
            _writeTimingStats.throughputCountAfterSync.fetch_add(1);
            _writeTimingStats.writesUntilNextSync.fetch_sub(1);
        } else {
            _writeTimingStats.throughputSamplesBeforeSync.fetch_add(throughputKBps);
            _writeTimingStats.throughputCountBeforeSync.fetch_add(1);
        }
    }
    _lastWriteTimer.start();
    _lastWriteBytes = _bytesWritten.load();

    // Cross-platform periodic sync
    _periodicSync();
    
    // Update bottleneck state for UI feedback
    _updateBottleneckState();

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
    
    // Cancel any pending async I/O to unblock waiting operations
    if (_file) {
        _file->CancelAsyncIO();
    }
    
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
        // Cancel async cache writer (this will remove the cache file)
        if (_asyncCacheWriter) {
            _asyncCacheWriter->cancel();
        }
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

uint64_t DownloadThread::extractTotal()
{
    return _extractTotal;
}

void DownloadThread::setExtractTotal(uint64_t total)
{
    _extractTotal = total;
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

int DownloadThread::pendingAsyncWrites() const
{
    if (_file && _file->IsAsyncIOSupported()) {
        return _file->GetPendingWriteCount();
    }
    return 0;
}

void DownloadThread::forcePollAsyncCompletions()
{
    // Called from progress watchdog when stall detected
    // This can unstick deadlocks where the download thread is blocked
    // in the ring buffer and not polling for IOCP completions
    if (_file && _file->IsAsyncIOSupported()) {
        int beforeCount = _file->GetPendingWriteCount();
        _file->PollAsyncCompletions();
        int afterCount = _file->GetPendingWriteCount();
        
        if (afterCount < beforeCount) {
            qDebug() << "forcePollAsyncCompletions: Retrieved" << (beforeCount - afterCount) 
                     << "completions! (was:" << beforeCount << "now:" << afterCount << ")";
        } else {
            qDebug() << "forcePollAsyncCompletions: No completions available, still" 
                     << afterCount << "pending";
#ifdef Q_OS_WIN
            // On Windows, run diagnostics to check actual I/O state
            static int diagCount = 0;
            if (++diagCount <= 3) {  // Only first few times to avoid spam
                auto* winFile = dynamic_cast<rpi_imager::WindowsFileOperations*>(_file.get());
                if (winFile) {
                    winFile->DiagnoseStuckWrites();
                }
            }
#endif
        }
    }
}

void DownloadThread::forceAsyncRecovery()
{
    // Request restart - used by thread if it detects async I/O issues
    // The watchdog will detect the stopped thread and restart in sync mode
    if (_file && _file->IsAsyncIOSupported()) {
        qDebug() << "Thread requesting restart due to async I/O issues";
        emit requestWriteRestart(tr("Storage device responding slowly. Restarting in compatibility mode..."));
        _file->CancelAsyncIO();
        _cancelled = true;
    }
}

bool DownloadThread::reduceAsyncQueueDepth(int newDepth)
{
    if (_file && _file->IsAsyncIOSupported()) {
        int currentDepth = _file->GetAsyncQueueDepth();
        int pendingWrites = _file->GetPendingWriteCount();
        if (newDepth < currentDepth && newDepth >= 2) {
            qDebug() << "Reducing async queue depth from" << currentDepth << "to" << newDepth;
            _file->ReduceQueueDepthForRecovery(newDepth);
            emit eventQueueDepthReduction(currentDepth, newDepth, pendingWrites);
            return true;
        }
    }
    return false;
}

int DownloadThread::getAsyncQueueDepth() const
{
    if (_file && _file->IsAsyncIOSupported()) {
        return _file->GetAsyncQueueDepth();
    }
    return 0;
}

bool DownloadThread::drainAndSwitchToSync(int timeoutSeconds)
{
    if (_file && _file->IsAsyncIOSupported()) {
        int pendingBefore = _file->GetPendingWriteCount();
        qDebug() << "Draining" << pendingBefore << "pending writes before switching to sync...";
        
        QElapsedTimer timer;
        timer.start();
        
        bool success = _file->DrainAndSwitchToSync(timeoutSeconds);
        quint32 durationMs = static_cast<quint32>(timer.elapsed());
        
        emit eventDrainAndHotSwap(durationMs, pendingBefore, success);
        
        if (success) {
            qDebug() << "Drain successful in" << durationMs << "ms - continuing in sync mode (hot-swap)";
            return true;
        }
        
        qDebug() << "Drain timeout after" << durationMs << "ms -" << _file->GetPendingWriteCount() << "writes still pending";
        return false;
    }
    return true;  // No async to drain
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

QString DownloadThread::_fileErrorToString(rpi_imager::FileError error, const QString &operation)
{
    QString context = operation.isEmpty() ? tr("storage operation") : operation;
    
    switch (error) {
        case rpi_imager::FileError::kSuccess:
            return QString();
            
        case rpi_imager::FileError::kOpenError:
            return tr("Failed to open storage device.\n\n"
                     "The device may be in use by another application, "
                     "or you may not have permission to access it.");
                     
        case rpi_imager::FileError::kWriteError:
            return tr("Error writing to storage device during %1.\n\n"
                     "This could be caused by:\n"
                     "• Device disconnected or became unresponsive\n"
                     "• Device is full or write-protected\n"
                     "• Hardware failure or bad sectors\n\n"
                     "Please check the device and try again.").arg(context);
                     
        case rpi_imager::FileError::kReadError:
            return tr("Error reading from storage device during %1.\n\n"
                     "The device may have been disconnected or is malfunctioning.")
                     .arg(context);
                     
        case rpi_imager::FileError::kSeekError:
            return tr("Error seeking on storage device during %1.\n\n"
                     "The device may be malfunctioning or disconnected.")
                     .arg(context);
                     
        case rpi_imager::FileError::kSizeError:
            return tr("Error getting storage device size.\n\n"
                     "The device may not be properly recognized.");
                     
        case rpi_imager::FileError::kCloseError:
            return tr("Error closing storage device.\n\n"
                     "The device may have been disconnected.");
                     
        case rpi_imager::FileError::kLockError:
            return tr("Failed to lock storage device.\n\n"
                     "The device may be in use by another application. "
                     "Please close any applications using this device and try again.");
                     
        case rpi_imager::FileError::kSyncError:
            return tr("Error syncing data to storage device.\n\n"
                     "The device may have been disconnected or is not responding. "
                     "Data may not have been fully written.");
                     
        case rpi_imager::FileError::kFlushError:
            return tr("Error flushing data to storage device.\n\n"
                     "The device may have been disconnected or is not responding.");
                     
        case rpi_imager::FileError::kCancelled:
            return QString();  // Cancellation is not an error to report
            
        case rpi_imager::FileError::kTimeout:
            return tr("Storage device timed out during %1.\n\n"
                     "The device is not responding. This may indicate:\n"
                     "• Device was disconnected\n"
                     "• Device has failed\n"
                     "• Driver or system issue\n\n"
                     "Please disconnect and reconnect the device, then try again.")
                     .arg(context);
                     
        default:
            return tr("Unknown storage error during %1.\n\n"
                     "Please try again or use a different storage device.")
                     .arg(context);
    }
}

void DownloadThread::_onWriteError()
{
    // Don't report write errors if the operation was cancelled
    // (cancellation can cause writes to fail, which is expected)
    if (_cancelled)
        return;

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
}

void DownloadThread::_closeFiles()
{
    QElapsedTimer closeTimer;
    closeTimer.start();
    
    // Close unified file operations
    if (_file && _file->IsOpen()) {
        _file->Close();
    }
#ifdef Q_OS_WIN
    if (_volumeFile && _volumeFile->IsOpen()) {
        _volumeFile->Close();
    }
#endif
    // Cancel async cache writer if still running (regardless of error state)
    // isActive() returns false when hasError() is true, but the thread may still
    // be running and needs proper cleanup. cancel() checks isRunning() internally.
    if (_asyncCacheWriter) {
        _asyncCacheWriter->cancel();
    }
    
    quint32 closeDurationMs = static_cast<quint32>(closeTimer.elapsed());
    if (closeDurationMs > 0) {
        emit eventDeviceClose(closeDurationMs, true);
    }
}

void DownloadThread::_writeComplete()
{
    // Wait for all async writes to complete before proceeding
    // This is critical for data integrity before verification
    if (_file && _file->IsAsyncIOSupported() && _file->GetAsyncQueueDepth() > 1) {
        QElapsedTimer asyncWaitTimer;
        asyncWaitTimer.start();
        int pendingBefore = _file->GetPendingWriteCount();
        
        rpi_imager::FileError asyncResult = _file->WaitForPendingWrites();
        
        quint32 asyncWaitMs = static_cast<quint32>(asyncWaitTimer.elapsed());
        qDebug() << "Async I/O drain:" << pendingBefore << "pending writes completed in" << asyncWaitMs << "ms";
        
        // Emit async I/O stats for performance logging
        emit eventAsyncIOConfig(_debugAsyncIO, _file->IsAsyncIOSupported(), 
                               _file->GetAsyncQueueDepth(), static_cast<quint32>(pendingBefore));
        
        // Get and emit detailed async I/O timing stats
        uint32_t wallClockMs, writeCount, minLatencyUs, maxLatencyUs, avgLatencyUs;
        _file->GetAsyncIOStats(wallClockMs, writeCount, minLatencyUs, maxLatencyUs, avgLatencyUs);
        
        qDebug() << "Async I/O timing: wall-clock" << wallClockMs << "ms,"
                 << writeCount << "writes, latency (us): min" << minLatencyUs 
                 << "max" << maxLatencyUs << "avg" << avgLatencyUs;
        
        emit eventAsyncIOTiming(wallClockMs, _bytesWritten.load(), writeCount);
        
        // Check if we fell back to sync mode (async I/O stalled but sync fallback succeeded)
        if (_file->IsInSyncFallbackMode()) {
            qDebug() << "Note: Operation completed after falling back to synchronous I/O";
            // Record the event - this is notable but not an error since fallback succeeded
            // Note: We don't emit syncFallbackActivated here because the write completed
            // successfully and we're about to start verification - showing a UI warning
            // that immediately disappears would just confuse users
            emit eventDeviceIOTimeout(static_cast<quint32>(pendingBefore),
                QString("Async stall recovered: %1 writes replayed synchronously").arg(pendingBefore));
        }
        
        if (asyncResult != rpi_imager::FileError::kSuccess) {
            qDebug() << "Async I/O error during drain:" << static_cast<int>(asyncResult);
            
            if (asyncResult == rpi_imager::FileError::kTimeout) {
                // Device/OS failed to complete writes AND sync fallback failed
                // Record the event for diagnostics
                emit eventDeviceIOTimeout(static_cast<quint32>(pendingBefore),
                    QString("Timeout waiting for %1 async writes; sync fallback also failed").arg(pendingBefore));
                emit error(tr("The storage device is not responding. This may indicate:\n"
                             "• The device was disconnected\n"
                             "• The device has failed\n"
                             "• A driver or system issue\n\n"
                             "Please disconnect and reconnect the device, then try again."));
                _closeFiles();
                return;
            } else if (asyncResult != rpi_imager::FileError::kCancelled) {
                // Some other async write error occurred
                emit error(tr("Error writing to storage device.\n\n"
                             "Some writes failed to complete. This could be caused by:\n"
                             "• Storage device disconnected during write\n"
                             "• Device is full or write-protected\n"
                             "• Hardware failure\n\n"
                             "Please check the device and try again."));
                _closeFiles();
                return;
            }
        }
    }
    
    // Emit write timing statistics before any cleanup
    _emitWriteTimingStats();
    
    // Don't report errors if the operation was cancelled
    if (_cancelled)
    {
        // Still need to wait for pending hash to avoid dangling futures
        if (_hasPendingHash) {
            _pendingHashFuture.waitForFinished();
            _hasPendingHash = false;
        }
        _closeFiles();
        return;
    }

    // Wait for final pending hash computation to complete before getting result
    if (_hasPendingHash) {
        if (!_pendingHashFuture.isFinished()) {
            QElapsedTimer waitTimer;
            waitTimer.start();
            _pendingHashFuture.waitForFinished();
            qDebug() << "Final hash wait:" << waitTimer.elapsed() << "ms";
        }
        _hasPendingHash = false;
    }

    QByteArray computedHash = _writehash.result().toHex();
    qDebug() << "Hash of uncompressed image:" << computedHash;
    if (!_expectedHash.isEmpty() && _expectedHash != computedHash)
    {
        qDebug() << "Mismatch with expected hash:" << _expectedHash;
        
        // Cancel async cache writer (this will remove the cache file)
        if (_asyncCacheWriter) {
            _asyncCacheWriter->cancel();
        }
        
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
        // Finish async cache writer (waits for all pending writes to complete)
        if (_asyncCacheWriter && _asyncCacheWriter->isActive()) {
            _asyncCacheWriter->finish();
            
            // Get cache file hash from async writer
            QByteArray cacheFileHash = _asyncCacheWriter->hash();
            
            // Emit both hashes for proper cache verification
            emit cacheFileHashUpdated(cacheFileHash, computedHash);
            // Keep old signal for backward compatibility
            emit cacheFileUpdated(computedHash);
        }
    }

    rpi_imager::FileError flushResult = _file->Flush();
    if (flushResult != rpi_imager::FileError::kSuccess)
    {
        DownloadThread::_onDownloadError(_fileErrorToString(flushResult, tr("flush")));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    rpi_imager::FileError syncResult = _file->ForceSync();
    if (syncResult != rpi_imager::FileError::kSuccess) {
        DownloadThread::_onDownloadError(_fileErrorToString(syncResult, tr("sync")));
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

    qDebug() << "Checking customization: config=" << !_config.isEmpty() << "cmdline=" << !_cmdline.isEmpty() 
             << "firstrun=" << !_firstrun.isEmpty() << "cloudinit=" << !_cloudinit.isEmpty() 
             << "initFormat=" << _initFormat << "isEmpty=" << _initFormat.isEmpty();
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
        rpi_imager::FileError writeResult = _file->WriteSequential(reinterpret_cast<const std::uint8_t*>(_firstBlock), _firstBlockSize);
        rpi_imager::FileError flushResult = (writeResult == rpi_imager::FileError::kSuccess) ? _file->Flush() : writeResult;
        
        if (writeResult != rpi_imager::FileError::kSuccess || flushResult != rpi_imager::FileError::kSuccess)
        {
            qFreeAligned(_firstBlock);
            _firstBlock = nullptr;
            
            rpi_imager::FileError errorToReport = (writeResult != rpi_imager::FileError::kSuccess) ? writeResult : flushResult;
            DownloadThread::_onDownloadError(_fileErrorToString(errorToReport, tr("writing partition table")));
            return;
        }
        _bytesWritten += _firstBlockSize;
        qFreeAligned(_firstBlock);
        _firstBlock = nullptr;
    }

    QElapsedTimer syncTimer;
    syncTimer.start();
    
    rpi_imager::FileError finalFlushResult = _file->Flush();
    if (finalFlushResult != rpi_imager::FileError::kSuccess)
    {
        emit eventFinalSync(static_cast<quint32>(syncTimer.elapsed()), false);
        DownloadThread::_onDownloadError(_fileErrorToString(finalFlushResult, tr("final flush")));
        _closeFiles();
        return;
    }

#ifndef Q_OS_WIN
    rpi_imager::FileError finalSyncResult = _file->ForceSync();
    if (finalSyncResult != rpi_imager::FileError::kSuccess) {
        emit eventFinalSync(static_cast<quint32>(syncTimer.elapsed()), false);
        DownloadThread::_onDownloadError(_fileErrorToString(finalSyncResult, tr("final sync")));
        _closeFiles();
        return;
    }
#endif

    emit eventFinalSync(static_cast<quint32>(syncTimer.elapsed()), true);
    _closeFiles();

#ifdef Q_OS_DARWIN
    // Give macOS a moment to settle after closing the device
    QThread::sleep(1);
#endif

    emit success();

    if (_ejectEnabled)
    {
        // Use canonical device path for eject (e.g., /dev/disk on macOS, not rdisk)
        QString ejectPath = PlatformQuirks::getEjectDevicePath(_filename);
        PlatformQuirks::ejectDisk(ejectPath);
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

    // Platform-specific optimization for sequential read verification
    // Invalidates cache and enables read-ahead hints
    _file->PrepareForSequentialRead(0, _verifyTotal);

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
        
        // Allow subclasses to emit progress updates
        _onVerifyProgress();
    }
    qFreeAligned(verifyBuf);

    qDebug() << "Verify hash:" << _verifyhash.result().toHex();
    qDebug() << "Verify done in" << t1.elapsed() / 1000.0 << "seconds";

    if (_verifyhash.result() == _writehash.result() || !_verifyEnabled || _cancelled)
    {
        emit eventVerify(static_cast<quint32>(t1.elapsed()), true, 
                         _writehash.result().toHex(), _verifyhash.result().toHex());
        return true;
    }
    else
    {
        emit eventVerify(static_cast<quint32>(t1.elapsed()), false,
                         _writehash.result().toHex(), _verifyhash.result().toHex());
        DownloadThread::_onDownloadError(tr("Verifying write failed. Contents of SD card is different from what was written to it."));
    }

    return false;
}

void DownloadThread::_updateBottleneckState()
{
    // Poll for async completions to ensure callbacks fire promptly
    // This prevents progress from jumping when completions batch up
    if (_file) {
        _file->PollAsyncCompletions();
    }
    
    // Adaptive recovery: periodically check if memory conditions have changed
    // If memory is now critically low, reduce queue depth to recover
    // Uses instance members (not static) for thread safety across multiple DownloadThread instances
    if (!_memoryCheckStarted) {
        _memoryCheckTimer.start();
        _memoryCheckStarted = true;
    } else if (_memoryCheckTimer.elapsed() >= kMemoryCheckIntervalMs) {
        _memoryCheckTimer.restart();
        
        if (_file && _file->IsAsyncIOSupported() && _file->GetAsyncQueueDepth() > 8) {
            qint64 availableMemMB = SystemMemoryManager::instance().getAvailableMemoryMB();
            if (availableMemMB < kCriticalMemoryMB) {
                qDebug() << "DownloadThread: Memory critically low (" << availableMemMB 
                         << "MB) - requesting queue depth reduction for recovery";
                _file->ReduceQueueDepthForRecovery(_file->GetAsyncQueueDepth() / 2);
            }
        }
    }
    
    // Detect current bottleneck based on pipeline state
    BottleneckState newState = BottleneckState::None;
    quint32 throughputKBps = 0;
    
    if (_file && _file->IsAsyncIOSupported() && _file->GetAsyncQueueDepth() > 1) {
        int pendingWrites = _file->GetPendingWriteCount();
        int queueDepth = _file->GetAsyncQueueDepth();
        
        // If async queue is more than 75% full, storage is the bottleneck
        if (pendingWrites > (queueDepth * 3 / 4)) {
            newState = BottleneckState::Storage;
        }
    }
    
    // Calculate current write throughput for display
    qint64 currentBytes = _bytesWritten.load();
    static qint64 lastThroughputBytes = 0;
    static QElapsedTimer throughputTimer;
    static bool throughputTimerStarted = false;
    
    if (!throughputTimerStarted) {
        throughputTimer.start();
        throughputTimerStarted = true;
        lastThroughputBytes = currentBytes;
    } else {
        qint64 elapsed = throughputTimer.elapsed();
        if (elapsed >= 500) {  // Update throughput every 500ms
            qint64 bytesDelta = currentBytes - lastThroughputBytes;
            if (bytesDelta > 0 && elapsed > 0) {
                throughputKBps = static_cast<quint32>((bytesDelta * 1000) / (elapsed * 1024));
            }
            lastThroughputBytes = currentBytes;
            throughputTimer.restart();
        }
    }
    
    // Apply hysteresis - only change state if it's been stable for a while
    if (newState != _currentBottleneck) {
        if (_bottleneckTimer.elapsed() >= BOTTLENECK_HYSTERESIS_MS) {
            _currentBottleneck = newState;
            _bottleneckTimer.restart();
            emit bottleneckStateChanged(_currentBottleneck, throughputKBps);
        }
    } else {
        // Same state, reset timer and emit periodic throughput updates
        _bottleneckTimer.restart();
        // Emit throughput updates even when state hasn't changed (every 500ms)
        static QElapsedTimer updateTimer;
        static bool updateTimerStarted = false;
        if (!updateTimerStarted) {
            updateTimer.start();
            updateTimerStarted = true;
        } else if (updateTimer.elapsed() >= 500) {
            emit bottleneckStateChanged(_currentBottleneck, throughputKBps);
            updateTimer.restart();
        }
    }
}

void DownloadThread::_initializeSyncConfiguration()
{
    _syncConfig = SystemMemoryManager::instance().calculateSyncConfiguration();
}

void DownloadThread::_periodicSync()
{
    // Skip periodic sync if disabled via debug options
    if (!_debugPeriodicSync) {
        return;
    }
    
    // Skip periodic sync when direct I/O is enabled (F_NOCACHE on macOS, O_DIRECT on Linux).
    // With direct I/O, data bypasses the page cache entirely, so periodic fsync() provides
    // no benefit - the data is already going directly to the device. This avoids the severe
    // performance penalty of frequent fsync() calls on slow media like SD cards over USB.
    // The final sync at the end of the write is still performed to ensure data integrity.
    if (_file && _file->IsDirectIOEnabled()) {
        return;
    }
    
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
        QElapsedTimer syncTimer;
        syncTimer.start();
        
        qDebug() << "Performing periodic sync at" << currentBytes << "bytes written"
                 << "(" << bytesSinceLastSync << "bytes since last sync,"
                 << timeSinceLastSync << "ms elapsed)"
                 << "on" << SystemMemoryManager::instance().getPlatformName();
        
        // Use unified FileOperations for flushing and syncing
        if (_file->Flush() != rpi_imager::FileError::kSuccess) {
            quint64 syncMs = static_cast<quint64>(syncTimer.elapsed());
            _writeTimingStats.totalSyncMs.fetch_add(syncMs);
            _writeTimingStats.syncCount.fetch_add(1);
            emit eventPeriodicSync(static_cast<quint32>(syncMs), false, currentBytes);
            qDebug() << "Warning: Flush() failed during periodic sync";
            return;
        }
        
        // Force filesystem sync using unified FileOperations
        if (_file->ForceSync() != rpi_imager::FileError::kSuccess) {
            quint64 syncMs = static_cast<quint64>(syncTimer.elapsed());
            _writeTimingStats.totalSyncMs.fetch_add(syncMs);
            _writeTimingStats.syncCount.fetch_add(1);
            emit eventPeriodicSync(static_cast<quint32>(syncMs), false, currentBytes);
            qDebug() << "Warning: ForceSync() failed during periodic sync";
            return;
        }
        
        quint64 syncMs = static_cast<quint64>(syncTimer.elapsed());
        _writeTimingStats.totalSyncMs.fetch_add(syncMs);
        _writeTimingStats.syncCount.fetch_add(1);
        
        // Track the next 5 writes after this sync to measure post-sync throughput impact
        _writeTimingStats.writesUntilNextSync.store(5);
        
        emit eventPeriodicSync(static_cast<quint32>(syncMs), true, currentBytes);
        
        // Update tracking variables
        _lastSyncBytes = currentBytes;
        _lastSyncTime.restart();
        
        qDebug() << "Periodic sync completed successfully in" << syncMs << "ms";
    }
}

void DownloadThread::_emitWriteTimingStats()
{
    quint32 writeCount = _writeTimingStats.writeCount.load();
    if (writeCount == 0) {
        return;  // No writes performed, nothing to report
    }
    
    // Emit write timing breakdown
    emit eventWriteTimingBreakdown(
        writeCount,
        _writeTimingStats.totalSyscallMs.load(),
        _writeTimingStats.totalPreHashWaitMs.load(),
        _writeTimingStats.totalPostHashWaitMs.load(),
        _writeTimingStats.totalSyncMs.load(),
        _writeTimingStats.syncCount.load()
    );
    
    // Emit write size distribution
    quint64 totalBytes = _writeTimingStats.totalBytesWritten.load();
    quint32 minSize = _writeTimingStats.minWriteSizeBytes.load();
    quint32 maxSize = _writeTimingStats.maxWriteSizeBytes.load();
    quint32 avgSize = writeCount > 0 ? static_cast<quint32>(totalBytes / writeCount) : 0;
    
    emit eventWriteSizeDistribution(
        minSize / 1024,  // Convert to KB
        maxSize / 1024,
        avgSize / 1024,
        totalBytes,
        writeCount
    );
    
    // Emit sync impact analysis (if we have data from both before and after syncs)
    quint32 countBefore = _writeTimingStats.throughputCountBeforeSync.load();
    quint32 countAfter = _writeTimingStats.throughputCountAfterSync.load();
    
    if (countBefore > 0 && countAfter > 0) {
        quint32 avgBefore = static_cast<quint32>(_writeTimingStats.throughputSamplesBeforeSync.load() / countBefore);
        quint32 avgAfter = static_cast<quint32>(_writeTimingStats.throughputSamplesAfterSync.load() / countAfter);
        
        emit eventWriteAfterSyncImpact(avgBefore, avgAfter, countBefore + countAfter);
    }
    
    qDebug() << "Write timing stats:"
             << "writes=" << writeCount
             << "syscall=" << _writeTimingStats.totalSyscallMs.load() << "ms"
             << "preHashWait=" << _writeTimingStats.totalPreHashWaitMs.load() << "ms"
             << "postHashWait=" << _writeTimingStats.totalPostHashWaitMs.load() << "ms"
             << "sync=" << _writeTimingStats.totalSyncMs.load() << "ms"
             << "syncCount=" << _writeTimingStats.syncCount.load()
             << "avgSize=" << avgSize / 1024 << "KB";
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
    qDebug() << "DownloadThread::setImageCustomisation - initFormat:" << initFormat << "cloudinit empty:" << cloudinit.isEmpty() << "cloudinitNetwork empty:" << cloudInitNetwork.isEmpty();
}

void DownloadThread::setDebugDirectIO(bool enabled)
{
    _debugDirectIO = enabled;
    qDebug() << "DownloadThread: Direct I/O" << (enabled ? "enabled" : "disabled");
}

void DownloadThread::setDebugPeriodicSync(bool enabled)
{
    _debugPeriodicSync = enabled;
    qDebug() << "DownloadThread: Periodic sync" << (enabled ? "enabled" : "disabled");
}

void DownloadThread::setDebugVerboseLogging(bool enabled)
{
    _debugVerboseLogging = enabled;
    qDebug() << "DownloadThread: Verbose logging" << (enabled ? "enabled" : "disabled");
}

void DownloadThread::setDebugAsyncIO(bool enabled)
{
    _debugAsyncIO = enabled;
    qDebug() << "DownloadThread: Async I/O" << (enabled ? "enabled" : "disabled");
}

void DownloadThread::setDebugAsyncQueueDepth(int depth)
{
    // Hard cap at 256 to prevent resource exhaustion
    const int maxDepth = 256;
    int requestedDepth = (depth < 1) ? 1 : ((depth > maxDepth) ? maxDepth : depth);
    
    // Memory-based sanity check: warn if queue depth would use excessive memory
    // With 8MB write blocks, each pending write uses 8MB of ring buffer memory
    qint64 availableMemMB = SystemMemoryManager::instance().getAvailableMemoryMB();
    size_t writeBlockSize = SystemMemoryManager::instance().getOptimalWriteBufferSize();
    size_t estimatedMemoryMB = (requestedDepth * writeBlockSize) / (1024 * 1024);
    
    if (estimatedMemoryMB > static_cast<size_t>(availableMemMB / 2)) {
        // Queue depth would use more than 50% of available RAM - cap it
        int safeDepth = static_cast<int>((availableMemMB * 1024 * 1024 / 2) / writeBlockSize);
        safeDepth = qMax(4, qMin(safeDepth, requestedDepth));
        
        qDebug() << "DownloadThread: WARNING - Requested queue depth" << requestedDepth
                 << "would use" << estimatedMemoryMB << "MB (only" << availableMemMB << "MB available)."
                 << "Capping to" << safeDepth;
        requestedDepth = safeDepth;
    }
    
    _debugAsyncQueueDepth = requestedDepth;
    qDebug() << "DownloadThread: Async queue depth set to" << _debugAsyncQueueDepth
             << "(estimated memory:" << estimatedMemoryMB << "MB)";
}

void DownloadThread::setDebugIPv4Only(bool enabled)
{
    _debugIPv4Only = enabled;
    qDebug() << "DownloadThread: IPv4-only mode" << (enabled ? "enabled" : "disabled");
}

void DownloadThread::setDebugSkipEndOfDevice(bool enabled)
{
    _debugSkipEndOfDevice = enabled;
    qDebug() << "DownloadThread: Skip end-of-device operations" << (enabled ? "enabled (for counterfeit cards)" : "disabled");
}

bool DownloadThread::_customizeImage()
{
    emit preparationStatusUpdate(tr("Customising OS..."));
    QElapsedTimer customTimer;
    customTimer.start();
    
    // Build metadata for performance tracking (what was configured, not values)
    QStringList configuredItems;
    if (!_config.isEmpty()) configuredItems << "config: set";
    if (!_cmdline.isEmpty()) configuredItems << "cmdline: set";
    if (!_firstrun.isEmpty()) configuredItems << "firstrun: set";
    if (!_cloudinit.isEmpty()) configuredItems << "cloudinit: set";
    if (!_cloudinitNetwork.isEmpty()) configuredItems << "network: set";
    if (_advancedOptions.testFlag(ImageOptions::EnableSecureBoot)) configuredItems << "secureboot: enabled";
    QString metadata = configuredItems.join("; ");

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
        
        // Parse FAT partition (can be slow for large partitions)
        QElapsedTimer fatTimer;
        fatTimer.start();
        DeviceWrapperFatPartition *fat = dw.fatPartition(1);
        emit eventFatPartitionSetup(static_cast<quint32>(fatTimer.elapsed()), fat != nullptr);

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
        qDebug() << "_customizeImage: _initFormat=" << _initFormat << "initCloud=" << initCloud << "_cloudinit.isEmpty()=" << _cloudinit.isEmpty();
        if (initCloud) {
            // Write meta-data file for NoCloud datasource
            // cloud-init requires meta-data to be present for proper datasource detection
            // instance-id should be unique per imaging to ensure cloud-init processes user-data
            QByteArray metadata = "instance-id: rpi-imager-" + 
                QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + "\n";
            fat->writeFile("meta-data", metadata);

            if (!_cloudinit.isEmpty())
            {
                _cloudinit = "#cloud-config\n"+_cloudinit;
                fat->writeFile("user-data", _cloudinit);
            }

            if (!_cloudinitNetwork.isEmpty())
            {
                fat->writeFile("network-config", _cloudinitNetwork);
            }
        }

        if (!_cmdline.isEmpty())
        {
            QByteArray cmdline = fat->readFile("cmdline.txt").trimmed();

            cmdline += _cmdline;

            fat->writeFile("cmdline.txt", cmdline);
        }
        
        // Sync before secure boot processing (writes partition table/MBR)
        QElapsedTimer syncTimer;
        syncTimer.start();
        dw.sync();
        emit eventPartitionTableWrite(static_cast<quint32>(syncTimer.elapsed()), true);
        
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
        emit eventCustomisation(static_cast<quint32>(customTimer.elapsed()), false, metadata);
        emit error(err.what());
        return false;
    }

    emit eventCustomisation(static_cast<quint32>(customTimer.elapsed()), true, metadata);
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
    
    // Build a case-insensitive lookup for customization files
    // FAT filesystems may store filenames in uppercase, so we need case-insensitive matching
    QMap<QString, QString> customFileMap; // maps lowercase -> original case
    for (const QString &customFile : customizationFiles) {
        customFileMap[customFile.toLower()] = customFile;
    }
    
    for (auto it = allFiles.constBegin(); it != allFiles.constEnd(); ++it) {
        QString keyLower = it.key().toLower();
        if (customFileMap.contains(keyLower)) {
            // Use the original case from the map to preserve filename casing
            QString originalKey = customFileMap[keyLower];
            customFiles[originalKey] = it.value();
            qDebug() << "DownloadThread: preserving customization file:" << it.key() << "(mapped to" << originalKey << ")";
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
