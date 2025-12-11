#ifndef DOWNLOADTHREAD_H
#define DOWNLOADTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <QString>
#include <QThread>
#include <QFile>
#include <QElapsedTimer>
#include <QFuture>
#include <atomic>
#include <time.h>
#include <curl/curl.h>
#include "acceleratedcryptographichash.h"
#include "imageadvancedoptions.h"
#include "systemmemorymanager.h"
#include "file_operations.h"
#include "asynccachewriter.h"


class DownloadThread : public QThread
{
    Q_OBJECT
public:
    /*
     * Constructor
     *
     * - url: URL to download
     * - localfilename: File name to save downloaded file as. If empty, store data in memory buffer
     */
    explicit DownloadThread(const QByteArray &url, const QByteArray &localfilename = "", const QByteArray &expectedHash = "", QObject *parent = nullptr);

    /*
     * Destructor
     *
     * Waits until download is complete
     * If this is not desired, call cancelDownload() first
     */
    virtual ~DownloadThread();

    /*
     * Cancel download
     *
     * Async function. Returns immedeately, but can take a second before download actually stops
     */
    virtual void cancelDownload();

    /*
     * Set proxy server.
     * Specify a string like this: user:pass@proxyserver:8080/
     * Used globally, for all connections
     */
    static void setProxy(const QByteArray &proxy);

    /*
     * Returns proxy server used
     */
    static QByteArray proxy();

    /*
     * Set user-agent header string
     */
    void setUserAgent(const QByteArray &ua);

    /*
     * Returns true if download has been successful
     */
    bool successfull();

    /*
     * Returns the downloaded data if saved to memory buffer instead of file
     */
    QByteArray data();

    /*
     * Delete downloaded file
     */
    void deleteDownloadedFile();

    /*
     * Return last-modified date (if available) as unix timestamp
     * (seconds since 1970)
     */
    time_t lastModified();

    /*
     * Return current server time as unix timestamp
     */
    time_t serverTime();

    /*
     * Enable/disable verification
     */
    void setVerifyEnabled(bool verify);

    /*
     * Enable disk cache
     */
    void setCacheFile(const QString &filename, qint64 filesize = 0);

    /*
     * Set input buffer size
     */
    void setInputBufferSize(int len);

    /*
     * Enable image customization
     */
    void setImageCustomisation(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork, const QByteArray &initFormat, const ImageOptions::AdvancedOptions opts);

    /*
     * Set child devices to unmount (macOS APFS volumes)
     * This avoids re-scanning the drive list during unmount, saving ~1 second
     * Call with empty list if device has no child devices (still skips the scan)
     */
    void setChildDevices(const QStringList &devices);
    
    /*
     * Mark that child device info was provided (even if empty)
     * This allows skipping the scan when we know there are no child devices
     */
    void setChildDevicesProvided(bool provided);
    
    /*
     * Debug options (set before starting the thread)
     */
    void setDebugDirectIO(bool enabled);
    void setDebugPeriodicSync(bool enabled);
    void setDebugVerboseLogging(bool enabled);

    /*
     * Thread safe download progress query functions
     */
    uint64_t dlNow();
    uint64_t dlTotal();
    uint64_t verifyNow();
    uint64_t verifyTotal();
    uint64_t bytesWritten();

    virtual bool isImage();
    size_t _writeFile(const char *buf, size_t len);

signals:
    void success();
    void error(QString msg);
    void cacheFileUpdated(QByteArray sha256);
    void cacheFileHashUpdated(QByteArray cacheFileHash, QByteArray imageHash);
    void finalizing();
    void preparationStatusUpdate(QString msg);
    
    // Performance event signals (connected by ImageWriter to PerformanceStats)
    void eventDriveUnmount(quint32 durationMs, bool success);
    void eventDriveUnmountVolumes(quint32 durationMs, bool success);  // Windows volume unmounting
    void eventDriveDiskClean(quint32 durationMs, bool success);       // Windows disk cleaning
    void eventDriveRescan(quint32 durationMs, bool success);          // Windows disk rescan
    void eventDriveOpen(quint32 durationMs, bool success, QString metadata);
    void eventDirectIOAttempt(bool attempted, bool succeeded, bool currentlyEnabled, int errorCode, QString errorMessage);
    void eventCustomisation(quint32 durationMs, bool success, QString metadata);
    void eventFinalSync(quint32 durationMs, bool success);
    void eventVerify(quint32 durationMs, bool success);
    void eventDecompressInit(quint32 durationMs, bool success);
    void eventPeriodicSync(quint32 durationMs, bool success, quint64 bytesWritten);
    void eventImageExtraction(quint32 durationMs, bool success);      // Archive extraction setup
    void eventPartitionTableWrite(quint32 durationMs, bool success);  // MBR/partition table write
    void eventFatPartitionSetup(quint32 durationMs, bool success);    // FAT partition parsing
    void eventDeviceClose(quint32 durationMs, bool success);          // Device handle close
    void eventNetworkRetry(quint32 sleepMs, QString metadata);        // Network retry with reason
    void eventNetworkConnectionStats(QString metadata);               // CURL connection timing stats
    
    // Write timing breakdown signals (for hypothesis testing)
    void eventWriteTimingBreakdown(quint32 totalWriteOps, quint64 totalSyscallMs, quint64 totalPreHashWaitMs,
                                   quint64 totalPostHashWaitMs, quint64 totalSyncMs, quint32 syncCount);
    void eventWriteSizeDistribution(quint32 minSizeKB, quint32 maxSizeKB, quint32 avgSizeKB, quint64 totalBytes, quint32 writeCount);
    void eventWriteAfterSyncImpact(quint32 avgThroughputBeforeSyncKBps, quint32 avgThroughputAfterSyncKBps, quint32 sampleCount);

protected:
    virtual void run();
    virtual void _onDownloadSuccess();
    virtual void _onDownloadError(const QString &msg);
    virtual void _onWriteError();

    void _hashData(const char *buf, size_t len);
    void _writeComplete();
    virtual bool _verify();
    int _authopen(const QByteArray &filename);
    bool _openAndPrepareDevice();
    void _writeCache(const char *buf, size_t len);
    qint64 _sectorsWritten();
    void _closeFiles();
    QByteArray _fileGetContentsTrimmed(const QString &filename);
    bool _customizeImage();
    bool _createSecureBootFiles(class DeviceWrapperFatPartition *fat);
    void _periodicSync();

    /*
     * libcurl callbacks
     */
    virtual size_t _writeData(const char *buf, size_t len);
    bool _progress(curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    void _header(const std::string &header);

    static size_t _curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static int _curl_xferinfo_callback(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
    static size_t _curl_header_callback( void *ptr, size_t size, size_t nmemb, void *userdata);

    CURL *_c;
    curl_off_t _startOffset;
    std::atomic<std::uint64_t> _lastDlTotal, _lastDlNow, _verifyTotal, _lastVerifyNow, _bytesWritten;
    std::uint64_t _lastFailureOffset;
    qint64 _sectorsStart;
    QByteArray _url, _useragent, _buf, _filename, _lastError, _expectedHash, _config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat;
    QStringList _childDevices;  // macOS APFS child volumes to unmount
    bool _childDevicesProvided = false;  // true if child device info was provided (even if empty list)
    ImageOptions::AdvancedOptions _advancedOptions;
    char *_firstBlock;
    size_t _firstBlockSize;
    static QByteArray _proxy;
    static int _curlCount;
    bool _cancelled, _successful, _verifyEnabled, _cacheEnabled, _ejectEnabled;
    time_t _lastModified, _serverTime, _lastFailureTime;
    QElapsedTimer _timer;
    int _inputBufferSize;

    // Unified cross-platform file operations
    std::unique_ptr<rpi_imager::FileOperations> _file;
    
    // Async cache writer for non-blocking cache file I/O
    std::unique_ptr<AsyncCacheWriter> _asyncCacheWriter;
    QString _cacheFilename;  // Store filename for legacy signal emission

#ifdef Q_OS_WIN
    // Windows-specific volume file for legacy compatibility
    std::unique_ptr<rpi_imager::FileOperations> _volumeFile;
    QByteArray _nr;
#endif

    AcceleratedCryptographicHash _writehash, _verifyhash;

    // Pipelined hash computation - store future for previous hash operation
    QFuture<void> _pendingHashFuture;
    bool _hasPendingHash;

    // Cross-platform adaptive page cache flushing
    qint64 _lastSyncBytes;
    QElapsedTimer _lastSyncTime;
    SystemMemoryManager::SyncConfiguration _syncConfig;
    
    // Debug options
    bool _debugDirectIO;
    bool _debugPeriodicSync;
    bool _debugVerboseLogging;
    
    void _initializeSyncConfiguration();
    
    // Write timing breakdown tracking (for performance hypothesis testing)
    struct WriteTimingStats {
        std::atomic<quint64> totalSyscallMs{0};      // Time in actual write() syscalls
        std::atomic<quint64> totalPreHashWaitMs{0};  // Time waiting for previous hash before write
        std::atomic<quint64> totalPostHashWaitMs{0}; // Time waiting for current hash after write
        std::atomic<quint64> totalSyncMs{0};         // Time in fsync() calls
        std::atomic<quint32> syncCount{0};           // Number of sync operations performed
        std::atomic<quint32> writeCount{0};          // Total write operations
        std::atomic<quint64> totalBytesWritten{0};   // Total bytes written
        std::atomic<quint32> minWriteSizeBytes{UINT32_MAX}; // Minimum write size
        std::atomic<quint32> maxWriteSizeBytes{0};   // Maximum write size
        
        // For measuring write throughput impact after sync
        std::atomic<quint64> throughputSamplesBeforeSync{0};  // Sum of KB/s measurements before sync
        std::atomic<quint32> throughputCountBeforeSync{0};    // Count of measurements before sync
        std::atomic<quint64> throughputSamplesAfterSync{0};   // Sum of KB/s measurements after sync (first 5 writes after sync)
        std::atomic<quint32> throughputCountAfterSync{0};     // Count of measurements after sync
        std::atomic<quint32> writesUntilNextSync{0};          // Counter to track "after sync" writes
        
        void reset() {
            totalSyscallMs.store(0);
            totalPreHashWaitMs.store(0);
            totalPostHashWaitMs.store(0);
            totalSyncMs.store(0);
            syncCount.store(0);
            writeCount.store(0);
            totalBytesWritten.store(0);
            minWriteSizeBytes.store(UINT32_MAX);
            maxWriteSizeBytes.store(0);
            throughputSamplesBeforeSync.store(0);
            throughputCountBeforeSync.store(0);
            throughputSamplesAfterSync.store(0);
            throughputCountAfterSync.store(0);
            writesUntilNextSync.store(0);
        }
    };
    WriteTimingStats _writeTimingStats;
    QElapsedTimer _lastWriteTimer;  // For measuring inter-write throughput
    quint64 _lastWriteBytes{0};     // Bytes written at last measurement
    
    void _emitWriteTimingStats();   // Called at end of write phase
};

#endif // DOWNLOADTHREAD_H
