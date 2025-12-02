/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "imagewriter.h"
#include "embedded_config.h"
#include "config.h"
#include "file_operations.h"
#include "drivelistitem.h"
#include "customization_generator.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "dependencies/yescrypt/yescrypt_wrapper.h"
#include "driveformatthread.h"
#include "localfileextractthread.h"
#include "downloadstatstelemetry.h"
#include "wlancredentials.h"
#include "device_info.h"
#include "platformquirks.h"
#ifndef CLI_ONLY_BUILD
#include "iconimageprovider.h"
#include "nativefiledialog.h"
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#endif
#include <archive.h>
#include <archive_entry.h>
#include <lzma.h>
#include <qjsondocument.h>
#include <QJsonArray>
#include <random>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimeZone>
#include <QNetworkInterface>
#include <QCoreApplication>
#ifndef CLI_ONLY_BUILD
#include <QQmlContext>
#include <QWindow>
#include <QGuiApplication>
#include <QClipboard>
#endif
#include <QUrl>
#include <QUrlQuery>
#include <QString>
#include <QStringList>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QDebug>
#include <QJsonObject>
#include <QTranslator>
#include <QPasswordDigestor>
#include <QVersionNumber>
#include <QCryptographicHash>
#include <QRandomGenerator>
#ifndef CLI_ONLY_BUILD
#include <QDesktopServices>
#include <QAccessible>
#endif
#include <stdlib.h>
#include <QLocale>
#include <QMetaType>
#include "imageadvancedoptions.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <QProcessEnvironment>
#endif

#ifdef Q_OS_LINUX
#include "linux/stpanalyzer.h"
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

using namespace ImageOptions;

namespace {
    constexpr uint MAX_SUBITEMS_DEPTH = 16;
} // namespace anonymous

// Initialize static member for secure boot CLI override
bool ImageWriter::_forceSecureBootEnabled = false;

ImageWriter::ImageWriter(QObject *parent)
    : QObject(parent),
      _cacheManager(nullptr),
      _waitingForCacheVerification(false),
      _networkManager(this),
      _src(), _repo(QUrl(QString(OSLIST_URL))),
      _dst(), _parentCategory(), _osName(), _osReleaseDate(), _currentLang(), _currentLangcode(), _currentKeyboard(),
      _expectedHash(), _cmdline(), _config(), _firstrun(), _cloudinit(), _cloudinitNetwork(), _initFormat(),
      _downloadLen(0), _extrLen(0), _devLen(0), _dlnow(0), _verifynow(0),
      _drivelist(DriveListModel(this)), // explicitly parented, so QML doesn't delete it
      _selectedDeviceValid(false),
      _writeState(WriteState::Idle),

      _cancelledDueToDeviceRemoval(false),
      _hwlist(HWListModel(*this)),
      _oslist(OSListModel(*this)),
      _engine(nullptr),
      _networkchecktimer(),
      _osListRefreshTimer(),
      _suspendInhibitor(nullptr),
      _thread(nullptr),
      _verifyEnabled(true), _multipleFilesInZip(false), _online(false),
      _settings(),
      _translations(),
      _trans(nullptr),
      _refreshIntervalOverrideMinutes(-1),
      _refreshJitterOverrideMinutes(-1),
#ifndef CLI_ONLY_BUILD
      _piConnectToken(),
      _mainWindow(nullptr)
#else
      _piConnectToken()
#endif
{
    // Initialise CacheManager
    _cacheManager = new CacheManager(this);
    
    // Initialise PerformanceStats
    _performanceStats = new PerformanceStats(this);
    
    // Set up file operations logging to use Qt's debug output
    rpi_imager::SetFileOperationsLogCallback([](const std::string& msg) {
        qDebug() << "[FileOps]" << msg.c_str();
    });
    qDebug() << "FileOperations log callback installed";

    QString platform;
#ifndef CLI_ONLY_BUILD
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) )
    {
        platform = QGuiApplication::platformName();
    }
    else
#endif
    {
        platform = "cli";
    }
    _device_info = std::make_unique<DeviceInfo>();

    if (::isEmbeddedMode())
    {
        connect(&_networkchecktimer, SIGNAL(timeout()), SLOT(pollNetwork()));
        _networkchecktimer.start(100);
        changeKeyboard(detectPiKeyboard());
        if (_currentKeyboard.isEmpty())
            _currentKeyboard = "us";
            _currentLang = "English";

        {
            QString nvmem_blconfig_path = {};
            QFile f("/sys/firmware/devicetree/base/aliases/blconfig");
            if (f.exists() && f.open(QIODevice::ReadOnly)) {
                QByteArray fileContent = f.readAll();
                nvmem_blconfig_path = QString::fromLatin1(fileContent.constData());
                f.close();
            }

            QString blconfig_link = {};
            if (!nvmem_blconfig_path.isEmpty()) {
                QString findCommand = "/usr/bin/find";
                QStringList findArguments = {
                    "-L",
                    "/sys/bus/nvmem",
                    "-maxdepth",
                    "3",
                    "-samefile",
                    "/sys/firmware/devicetree/base" + nvmem_blconfig_path
                };
                QProcess *findProcess = new QProcess(this);
                connect(findProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    [&blconfig_link, &findProcess](int exitCode, QProcess::ExitStatus exitStatus) { // clazy:exclude=lambda-in-connect
                        blconfig_link = findProcess->readAllStandardOutput();
                    });
                findProcess->start(findCommand, findArguments);
                findProcess->waitForFinished(); //Default timeout: 30s
                delete findProcess;
            }
            if (!blconfig_link.isEmpty()) {
                QDir blconfig_of_dir = QDir(blconfig_link);
                if (blconfig_of_dir.cdUp()) {
                    QFile blconfig_file = QFile(blconfig_of_dir.path() + QDir::separator() + "nvmem");
                    if (blconfig_file.exists() && blconfig_file.open(blconfig_file.ReadOnly)) {
                        const QByteArrayList eepromSettings = blconfig_file.readAll().split('\n');
                        blconfig_file.close();
                        for (const QByteArray &setting : eepromSettings)
                        {
                            if (setting.startsWith("IMAGER_REPO_URL="))
                            {
                                _repo = setting.mid(16).trimmed();
                                qDebug() << "Repository from EEPROM:" << _repo;
                            }
                        }
                    }
                }
            }
        }

        // The STP analyzer is only built for embedded mode, so
        // unlike the block above that can be selected at runtime,
        // this must be selected at build time.
#ifdef BUILD_EMBEDDED
        StpAnalyzer *stpAnalyzer = new StpAnalyzer(5, this);
        connect(stpAnalyzer, SIGNAL(detected()), SLOT(onSTPdetected()));
        stpAnalyzer->startListening("eth0");
#endif
    }

    if (!_settings.isWritable() && !_settings.fileName().isEmpty())
    {
        /* Settings file is not writable, probably run by root previously */
        QString settingsFile = _settings.fileName();
        qDebug() << "Settings file" << settingsFile << "not writable. Recreating it";
        QFile f(_settings.fileName());
        QByteArray oldsettings;

        if (f.open(f.ReadOnly))
        {
            oldsettings = f.readAll();
            f.close();
        }
        f.remove();
        if (f.open(f.WriteOnly))
        {
            f.write(oldsettings);
            f.close();
            _settings.sync();
        }
        else
        {
            qDebug() << "Error deleting and recreating settings file. Please remove manually.";
        }
    }

    // Cache management is now handled entirely by CacheManager

    QDir dir(":/i18n", "rpi-imager_*.qm");
    const QStringList transFiles = dir.entryList();
    QLocale currentLocale;
    QStringList localeComponents = currentLocale.name().split('_');
    QString currentlangcode;
    if (!localeComponents.isEmpty())
        currentlangcode = localeComponents.first();

    for (const QString &tf : transFiles)
    {
        QString langcode = tf.mid(11, tf.length()-14);

        QLocale loc(langcode);
        /* Use "English" for "en" and not "American English" */
        QString langname = (langcode == "en" ? "English" : loc.nativeLanguageName() );
        _translations.insert(langname, langcode);
        if (langcode == currentlangcode)
        {
            _currentLang = langname;
            _currentLangcode = currentlangcode;
        }
    }

    // Centralised network manager, for fetching OS lists
    connect(&_networkManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(handleNetworkRequestFinished(QNetworkReply *)));

    // Connect to CacheManager signals
    connect(_cacheManager, &CacheManager::cacheFileUpdated,
            this, [this](const QByteArray& hash) {
                qDebug() << "Received cacheFileUpdated signal - refreshing UI for hash:" << hash;
                emit cacheStatusChanged();
            });

    connect(_cacheManager, &CacheManager::cacheVerificationComplete,
            this, [this](bool isValid) {
                if (isValid) {
                    // Emit cache status changed signal for QML to react to
                    emit cacheStatusChanged();
                }
            });

    connect(_cacheManager, &CacheManager::cacheInvalidated,
            this, [this]() {
                // Emit cache status changed signal when cache is invalidated
                emit cacheStatusChanged();
            });

    // Connect to specific device removal events
    connect(&_drivelist, &DriveListModel::deviceRemoved,
            this, &ImageWriter::onSelectedDeviceRemoved);
    
    // Connect drive list poll timing events for performance tracking
    // Only record polls that take longer than 200ms to avoid noise from normal fast polls
    connect(&_drivelist, &DriveListModel::eventDriveListPoll,
            this, [this](quint32 durationMs){
                if (durationMs >= 200) {
                    _performanceStats->recordEvent(PerformanceStats::EventType::DriveListPoll, durationMs, true);
                }
            });
    
    // Connect OS list parse timing events for performance tracking
    connect(&_oslist, &OSListModel::eventOsListParse,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::OsListParse, durationMs, success);
            });

    // Start background cache operations early
    _cacheManager->startBackgroundOperations();

    // Start background drive list polling
    qDebug() << "Starting background drive list polling";
    _drivelist.startPolling();

    // Configure OS list refresh timer (single-shot; we reschedule after each fetch)
    _osListRefreshTimer.setSingleShot(true);
    connect(&_osListRefreshTimer, &QTimer::timeout, this, &ImageWriter::onOsListRefreshTimeout);

    // Belt-and-braces: ensure potentially dangerous flags are not persisted between runs
    // If found in settings, remove them immediately
    if (_settings.contains("disable_warnings")) {
        _settings.remove("disable_warnings");
        _settings.sync();
        qDebug() << "Removed persisted disable_warnings flag from settings";
    }
}

void ImageWriter::setMainWindow(QObject *window)
{
#ifndef CLI_ONLY_BUILD
    // Convert QObject to QWindow
    _mainWindow = qobject_cast<QWindow*>(window);
    if (!_mainWindow) {
        // If it's not a QWindow directly, try to get the window from a QQuickWindow
        QQuickWindow *quickWindow = qobject_cast<QQuickWindow*>(window);
        if (quickWindow) {
            _mainWindow = quickWindow;
        }
    }
#else
    Q_UNUSED(window);
#endif
}

void ImageWriter::bringWindowToForeground()
{
#ifndef CLI_ONLY_BUILD
    if (!_mainWindow) {
        return;
    }

    // Get the native window handle and pass it to the platform-specific implementation
    void* windowHandle = reinterpret_cast<void*>(_mainWindow->winId());
    if (windowHandle) {
        PlatformQuirks::bringWindowToForeground(windowHandle);
        qDebug() << "Requested window to be brought to foreground for rpi-connect token";
    }
#endif
}

QString ImageWriter::getNativeOpenFileName(const QString &title,
                                           const QString &initialDir,
                                           const QString &filter)
{
#ifndef CLI_ONLY_BUILD
    if (!NativeFileDialog::areNativeDialogsAvailable()) {
        return QString();
    }
    return NativeFileDialog::getOpenFileName(title, initialDir, filter, _mainWindow);
#else
    Q_UNUSED(title);
    Q_UNUSED(initialDir);
    Q_UNUSED(filter);
    return QString();
#endif
}

QString ImageWriter::readFileContents(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return QString();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file:" << filePath << "Error:" << file.errorString();
        return QString();
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    return content.trimmed();
}

ImageWriter::~ImageWriter()
{
    // Stop background drive list polling
    qDebug() << "Stopping background drive list polling";
    _drivelist.stopPolling();

    // Stop and cleanup CacheManager background thread before Qt's automatic cleanup
    // This ensures the background thread is properly terminated before ImageWriter is destroyed
    if (_cacheManager) {
        qDebug() << "Cleaning up CacheManager";
        delete _cacheManager;
        _cacheManager = nullptr;
    }

    // Ensure any running thread is properly cleaned up
    if (_thread) {
        if (_thread->isRunning()) {
            qDebug() << "Cancelling running thread in ImageWriter destructor";
            _thread->cancelDownload();
            if (!_thread->wait(10000)) {
                qDebug() << "Thread did not finish within 10 seconds, terminating it";
                _thread->terminate();
                _thread->wait(2000);
            }
        }
        delete _thread;
        _thread = nullptr;
    }

    // Cleanup suspend inhibitor if it's still active
    if (_suspendInhibitor) {
        delete _suspendInhibitor;
        _suspendInhibitor = nullptr;
    }

    if (_trans)
    {
        QCoreApplication::removeTranslator(_trans);
        delete _trans;
    }
}

void ImageWriter::setEngine(QQmlApplicationEngine *engine)
{
#ifndef CLI_ONLY_BUILD
    _engine = engine;
    if (_engine) {
        // Register icon provider for image://icons/<url>
        _engine->addImageProvider(QStringLiteral("icons"), new IconImageProvider());
    }
#else
    Q_UNUSED(engine);
#endif
}

/* Set URL to download from */
void ImageWriter::setSrc(const QUrl &url, quint64 downloadLen, quint64 extrLen, QByteArray expectedHash, bool multifilesinzip, QString parentcategory, QString osname, QByteArray initFormat, QString releaseDate)
{
    _src = url;
    _downloadLen = downloadLen;
    _extrLen = extrLen;
    _expectedHash = expectedHash;
    _multipleFilesInZip = multifilesinzip;
    _parentCategory = parentcategory;
    _osName = osname;
    _initFormat = (initFormat == "none") ? "" : initFormat;
    _osReleaseDate = releaseDate;

    if (!_downloadLen && url.isLocalFile())
    {
        QFileInfo fi(url.toLocalFile());
        _downloadLen = fi.size();
    }
}

/* Set device to write to */
void ImageWriter::setDst(const QString &device, quint64 deviceSize)
{
    _dst = device;
    _devLen = deviceSize;
    _selectedDeviceValid = !device.isEmpty();

    // Reset write completion state when device selection changes
    if (device.isEmpty()) {
        setWriteState(WriteState::Idle);
        _dstChildDevices.clear();
    } else {
#ifdef Q_OS_DARWIN
        // Cache child devices (APFS volumes on macOS) to avoid re-scanning during unmount
        _dstChildDevices = _drivelist.getChildDevices(device);
        if (!_dstChildDevices.isEmpty()) {
            qDebug() << "Cached" << _dstChildDevices.count() << "child devices for" << device;
        }
#endif
    }

    qDebug() << "Device selection changed to:" << device;
}

/* Returns true if src and dst are set and destination device is still valid */
bool ImageWriter::readyToWrite()
{
    return !_src.isEmpty() && !_dst.isEmpty() && _selectedDeviceValid;
}

/* Returns true if running on Raspberry Pi */
bool ImageWriter::isRaspberryPiDevice()
{
    return _device_info->isRaspberryPi();
}

bool ImageWriter::createHardwareTags()
{
    QJsonDocument doc = getFilteredOSlistDocument();
    QJsonObject root = doc.object();

    if (root.isEmpty() || !doc.isObject()) {
        qWarning() << Q_FUNC_INFO << "Invalid root";
        return false;
    }

    QJsonValue imager = root.value("imager");

    if (!imager.isObject()) {
        qWarning() << Q_FUNC_INFO << "missing imager";
        return false;
    }

    QJsonValue devices = imager.toObject().value("devices");

    const QJsonArray deviceArray = devices.toArray();

    _device_info->setHardwareTags(deviceArray);

    return true;
}

QString ImageWriter::getHardwareName()
{
    return _device_info->hardwareName();
}

/* Start writing */
void ImageWriter::startWrite()
{
    if (!readyToWrite())
    {
        // Provide a user-visible error rather than silently returning, so the UI can recover
        // Check all conditions and provide comprehensive error messages
        QStringList missingItems;
        
        if (_src.isEmpty())
            missingItems.append(tr("image"));
        if (_dst.isEmpty())
            missingItems.append(tr("storage device"));
        else if (!_selectedDeviceValid)
            missingItems.append(tr("valid storage device (device no longer available)"));
        
        QString reason;
        if (!missingItems.isEmpty())
        {
            if (missingItems.size() == 1)
                reason = tr("No %1 selected.").arg(missingItems.first());
            else
                reason = tr("No %1 selected.").arg(missingItems.join(tr(" or ")));
        }
        else
        {
            reason = tr("Unknown precondition failure.");
        }

        emit error(tr("Cannot start write. %1").arg(reason));
        return;
    }

    setWriteState(WriteState::Preparing);

    if (_src.toString() == "internal://format")
    {
        // For formatting operations, skip all cache operations since we don't need cached files
        qDebug() << "Starting format operation - skipping cache operations";
        DriveFormatThread *dft = new DriveFormatThread(_dst.toLatin1(), this);
        connect(dft, SIGNAL(success()), SLOT(onSuccess()));
        connect(dft, SIGNAL(error(QString)), SLOT(onError(QString)));
        connect(dft, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
        connect(dft, &DriveFormatThread::eventDriveFormat,
                this, [this](quint32 durationMs, bool success){
                    _performanceStats->recordEvent(PerformanceStats::EventType::DriveFormat, durationMs, success);
                });
        dft->start();
        return;
    }

    QByteArray urlstr = _src.toString(_src.FullyEncoded).toLatin1();
    QString lowercaseurl = urlstr.toLower();
    const bool compressed = lowercaseurl.endsWith(".zip") ||
                            lowercaseurl.endsWith(".xz") ||
                            lowercaseurl.endsWith(".bz2") ||
                            lowercaseurl.endsWith(".gz") ||
                            lowercaseurl.endsWith(".7z") ||
                            lowercaseurl.endsWith(".zst") ||
                            lowercaseurl.endsWith(".cache");

    // Proactive validation for local sources before spawning threads
    if (_src.isLocalFile())
    {
        const QString localPath = _src.toLocalFile();
        QFileInfo localFi(localPath);
        if (!localFi.exists())
        {
            onError(tr("Source file not found: %1").arg(localPath));
            return;
        }
        if (!localFi.isFile())
        {
            onError(tr("Source is not a regular file: %1").arg(localPath));
            return;
        }
        if (!localFi.isReadable())
        {
            onError(tr("Source file is not readable: %1").arg(localPath));
            return;
        }
    }

    if (!_extrLen && _src.isLocalFile())
    {
        if (!compressed)
            _extrLen = _downloadLen;
        else if (lowercaseurl.endsWith(".xz"))
            _parseXZFile();
        else
            _parseCompressedFile();
    }

    if (_devLen && _extrLen > _devLen)
    {
        emit error(tr("Storage capacity is not large enough.<br>Needs to be at least %1.")
                   .arg(formatSize(_extrLen)));
        return;
    }

    if (_extrLen && !_multipleFilesInZip && _extrLen % 512 != 0)
    {
        emit error(tr("Input file is not a valid disk image.<br>File size %1 bytes is not a multiple of 512 bytes.").arg(_extrLen));
        return;
    }

    // Start performance stats session early so cache lookup is captured
    _performanceStats->startSession(_osName.isEmpty() ? _src.fileName() : _osName, 
                                    _extrLen > 0 ? _extrLen : _downloadLen, 
                                    _dst);

    // Time cache lookup for performance tracking
    QElapsedTimer cacheLookupTimer;
    cacheLookupTimer.start();
    bool cacheHit = !_expectedHash.isEmpty() && _cacheManager->isCached(_expectedHash);
    _performanceStats->recordEvent(PerformanceStats::EventType::CacheLookup,
        static_cast<quint32>(cacheLookupTimer.elapsed()), true,
        cacheHit ? "hit" : (_expectedHash.isEmpty() ? "no_hash" : "miss"));
    
    if (cacheHit)
    {
        // Use background cache manager to check cache file integrity
        CacheManager::CacheStatus cacheStatus = _cacheManager->getCacheStatus();

        if (cacheStatus.verificationComplete && cacheStatus.isValid)
        {
            qDebug() << "Using verified cache file (background verified):" << cacheStatus.cacheFileName;
            // Use cached file
            urlstr = QUrl::fromLocalFile(cacheStatus.cacheFileName).toString(_src.FullyEncoded).toLatin1();
        }
        else if (cacheStatus.verificationComplete && !cacheStatus.isValid)
        {
            qDebug() << "Cache file failed background integrity check, invalidating and proceeding with download";
            _cacheManager->invalidateCache();
            // Continue with original URL - cache will be recreated during download
        }
        else
        {
            // Background verification not yet complete
            qDebug() << "Cache verification needed - waiting for completion";

            // Connect to cache verification progress and completion signals
            connect(_cacheManager, &CacheManager::cacheVerificationProgress,
                    this, &ImageWriter::onCacheVerificationProgress);
            connect(_cacheManager, &CacheManager::cacheVerificationComplete,
                    this, &ImageWriter::onCacheVerificationComplete);

            // Start timing cache verification
            _cacheVerificationTimer.start();
            
            if (!cacheStatus.verificationComplete)
            {
                qDebug() << "Starting cache verification";
                _cacheManager->startVerification(_expectedHash);
            }
            else
            {
                qDebug() << "Verification is already in progress. Waiting for it to complete.";
            }

            // Set flag to indicate we're waiting for cache verification
            _waitingForCacheVerification = true;

            // Emit signal to update UI for cache verification
            emit cacheVerificationStarted();

            // Don't proceed with write yet - wait for cache verification to complete
            return;
        }
    }

    if (QUrl(urlstr).isLocalFile())
    {
        _thread = new LocalFileExtractThread(urlstr, _dst.toLatin1(), _expectedHash, this);
    }
    else
    {
        _thread = new DownloadExtractThread(urlstr, _dst.toLatin1(), _expectedHash, this);
        if (_repo.toString() == OSLIST_URL)
        {
            DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr, _parentCategory.toLatin1(), _osName.toLatin1(), isEmbeddedMode(), _currentLangcode, this);
            connect(tele, SIGNAL(finished()), tele, SLOT(deleteLater()));
            tele->start();
        }
    }

    connect(_thread, SIGNAL(success()), SLOT(onSuccess()));
    connect(_thread, SIGNAL(error(QString)), SLOT(onError(QString)));
    connect(_thread, SIGNAL(finalizing()), SLOT(onFinalizing()));
    connect(_thread, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
    // Ensure cleanup of thread pointer on finish in all paths
    connect(_thread, &QThread::finished, this, [this]() {
        if (_thread)
        {
            _thread->deleteLater();
            _thread = nullptr;
        }
    });

    // Connect to progress signals if this is a DownloadExtractThread
    DownloadExtractThread *downloadThread = qobject_cast<DownloadExtractThread*>(_thread);
    if (downloadThread) {
        connect(downloadThread, &DownloadExtractThread::downloadProgressChanged,
                this, &ImageWriter::downloadProgress);
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, &ImageWriter::verifyProgress);
        
        // Capture progress for performance stats (lightweight - just stores raw samples)
        connect(downloadThread, &DownloadExtractThread::downloadProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordDownloadProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::decompressProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordDecompressProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::writeProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordWriteProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordVerifyProgress(now, total);
                });
        
        // Also transition state to Verifying when verify progress first arrives
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 /*now*/, quint64 /*total*/){
                    if (_writeState != WriteState::Verifying && _writeState != WriteState::Finalizing && _writeState != WriteState::Succeeded)
                        setWriteState(WriteState::Verifying);
                });
        
        // Capture ring buffer stall events for time-series correlation
        connect(downloadThread, &DownloadExtractThread::eventRingBufferStats,
                this, [this](qint64 timestampMs, quint32 durationMs, QString metadata){
                    // Record as an event with explicit startMs from the stall timestamp
                    PerformanceStats::TimedEvent event;
                    event.type = PerformanceStats::EventType::RingBufferStarvation;
                    event.startMs = static_cast<uint32_t>(timestampMs);
                    event.durationMs = durationMs;
                    event.metadata = metadata;
                    event.success = true;
                    event.bytesTransferred = 0;
                    _performanceStats->addEvent(event);
                });
        
        // Pipeline timing summary events (emitted at end of extraction)
        connect(downloadThread, &DownloadExtractThread::eventPipelineDecompressionTime,
                this, [this](quint32 totalMs, quint64 bytesDecompressed){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineDecompressionTime,
                        totalMs, bytesDecompressed, true,
                        QString("bytes: %1 MB").arg(bytesDecompressed / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventPipelineWriteWaitTime,
                this, [this](quint32 totalMs, quint64 bytesWritten){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineWriteWaitTime,
                        totalMs, bytesWritten, true,
                        QString("bytes: %1 MB").arg(bytesWritten / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventPipelineRingBufferWaitTime,
                this, [this](quint32 totalMs, quint64 bytesRead){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineRingBufferWaitTime,
                        totalMs, bytesRead, true,
                        QString("bytes: %1 MB").arg(bytesRead / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventWriteRingBufferStats,
                this, [this](quint64 producerStalls, quint64 consumerStalls, 
                             quint64 producerWaitMs, quint64 consumerWaitMs){
                    QString metadata = QString("producer_stalls: %1 (%2 ms); consumer_stalls: %3 (%4 ms)")
                        .arg(producerStalls).arg(producerWaitMs)
                        .arg(consumerStalls).arg(consumerWaitMs);
                    // Use combined wait time as duration for the event
                    quint32 totalWaitMs = static_cast<quint32>(producerWaitMs + consumerWaitMs);
                    _performanceStats->recordEvent(
                        PerformanceStats::EventType::WriteRingBufferStats,
                        totalWaitMs, true, metadata);
                });
    }
    
    // Connect performance event signals from DownloadThread
    connect(_thread, &DownloadThread::eventDriveUnmount,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveUnmount, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveUnmountVolumes,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveUnmountVolumes, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveDiskClean,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveDiskClean, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveRescan,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveRescan, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveOpen,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveOpen, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventDirectIOAttempt,
            this, [this](bool attempted, bool succeeded, bool currentlyEnabled, int errorCode, QString errorMessage){
                QString metadata = QString("attempted: %1; succeeded: %2; currently_enabled: %3; error_code: %4; error: %5")
                    .arg(attempted ? "yes" : "no")
                    .arg(succeeded ? "yes" : "no")
                    .arg(currentlyEnabled ? "yes" : "no")
                    .arg(errorCode)
                    .arg(errorMessage.isEmpty() ? "none" : errorMessage);
                _performanceStats->recordEvent(PerformanceStats::EventType::DirectIOAttempt, 0, currentlyEnabled, metadata);
            });
    connect(_thread, &DownloadThread::eventCustomisation,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::Customisation, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventFinalSync,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::FinalSync, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventVerify,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::HashComputation, durationMs, success, "Post-write verification");
            });
    connect(_thread, &DownloadThread::eventPeriodicSync,
            this, [this](quint32 durationMs, bool success, quint64 bytesWritten){
                QString metadata = QString("at %1 MB").arg(bytesWritten / (1024 * 1024));
                _performanceStats->recordEvent(PerformanceStats::EventType::PageCacheFlush, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventImageExtraction,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::ImageExtraction, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventPartitionTableWrite,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::PartitionTableWrite, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventFatPartitionSetup,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::FatPartitionSetup, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDeviceClose,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DeviceClose, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventNetworkRetry,
            this, [this](quint32 sleepMs, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkRetry, sleepMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventNetworkConnectionStats,
            this, [this](QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkConnectionStats, 0, true, metadata);
            });

    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(staticVersion()).toUtf8());
    _thread->setImageCustomisation(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);
#ifdef Q_OS_DARWIN
    // Pass cached child devices to avoid re-scanning during unmount (saves ~1 second on macOS)
    // Always call setChildDevices (even with empty list) so we skip the expensive scan
    _thread->setChildDevices(_dstChildDevices);
#endif

    // Only set up cache operations for remote downloads, not when using cached files as source
    if (!_expectedHash.isEmpty() && !QUrl(urlstr).isLocalFile())
    {
        // Use CacheManager to setup cache for download
        QString cacheFilePath;
        if (_cacheManager->setupCacheForDownload(_expectedHash, _downloadLen, cacheFilePath))
        {
            qDebug() << "Setting up cache file for download:" << cacheFilePath;
            _thread->setCacheFile(cacheFilePath, _downloadLen);
            // Connect to CacheManager for cache updates (extract uncompressed hash from signal)
            connect(_thread, &DownloadThread::cacheFileHashUpdated,
                    this, [this](const QByteArray& cacheFileHash, const QByteArray& imageHash) {
                        qDebug() << "DownloadThread cache update - cacheFileHash:" << cacheFileHash << "imageHash:" << imageHash;
                        // Update cache with both uncompressed hash (imageHash) and compressed hash (cacheFileHash)
                        _cacheManager->updateCacheFile(imageHash, cacheFileHash);
                    });
        }
        else
        {
            qDebug() << "Cache setup failed or disabled - proceeding without caching";
        }
    }
    else if (!_expectedHash.isEmpty() && QUrl(urlstr).isLocalFile())
    {
        qDebug() << "Using cached file as source - skipping cache setup";
    }

    if (_multipleFilesInZip)
    {
        static_cast<DownloadExtractThread *>(_thread)->enableMultipleFileExtraction();
        DriveFormatThread *dft = new DriveFormatThread(_dst.toLatin1(), this);
        connect(dft, SIGNAL(success()), _thread, SLOT(start()));
        connect(dft, SIGNAL(error(QString)), SLOT(onError(QString)));
        connect(dft, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
        connect(dft, &DriveFormatThread::eventDriveFormat,
                this, [this](quint32 durationMs, bool success){
                    _performanceStats->recordEvent(PerformanceStats::EventType::DriveFormat, durationMs, success);
                });
        dft->start();
        setWriteState(WriteState::Writing);
    }
    else
    {
        _thread->start();
        setWriteState(WriteState::Writing);
    }

    startProgressPolling();
}

// Cache file update methods removed - now handled by connecting DownloadThread directly to CacheManager

/* Cancel write */
void ImageWriter::cancelWrite()
{
    if (_waitingForCacheVerification)
    {
        // If we're waiting for cache verification, treat this as skip cache verification
        skipCacheVerification();
        return;
    }

    if (_thread)
    {
        connect(_thread, SIGNAL(finished()), SLOT(onCancelled()));
        _thread->cancelDownload();
    }

    if (!_thread || !_thread->isRunning())
    {
        emit cancelled();
    }
}

/* Skip only the current post-write verification pass */
void ImageWriter::skipCurrentVerification()
{
    if (_thread)
    {
        // Temporarily disable verification for the active write thread
        _thread->setVerifyEnabled(false);
        qDebug() << "User requested to skip current verification pass";
    }
}

/* Skip cache verification and proceed with download */
void ImageWriter::skipCacheVerification()
{
    if (!_waitingForCacheVerification)
    {
        qDebug() << "skipCacheVerification called but not waiting for cache verification";
        return;
    }

    qDebug() << "User skipped cache verification, proceeding with download";

    _waitingForCacheVerification = false;

    // Disconnect cache verification signals
    disconnect(_cacheManager, &CacheManager::cacheVerificationProgress,
               this, &ImageWriter::onCacheVerificationProgress);
    disconnect(_cacheManager, &CacheManager::cacheVerificationComplete,
               this, &ImageWriter::onCacheVerificationComplete);

    // Emit signal to update UI (cache verification finished)
    emit cacheVerificationFinished();

    // Proceed with download (not using cache)
    qDebug() << "Cache verification skipped, invalidating cache and proceeding with download";
    _cacheManager->invalidateCache();
    _continueStartWriteAfterCacheVerification(false); // false = cache not valid, use download
}

void ImageWriter::onCancelled()
{
    setWriteState(WriteState::Cancelled);
    sender()->deleteLater();
    if (sender() == _thread)
    {
        _thread = nullptr;
    }

    // End performance stats session
    _performanceStats->endSession(false, _cancelledDueToDeviceRemoval ? "Device removed" : "Cancelled by user");

    // If cancellation was due to device removal, emit a dedicated signal (localization-safe for QML routing)
    if (_cancelledDueToDeviceRemoval) {
        _cancelledDueToDeviceRemoval = false;
        emit writeCancelledDueToDeviceRemoval();
    } else {
        emit cancelled();
    }
}

/* Return true if url is in our local disk cache */
bool ImageWriter::isCached(const QUrl &, const QByteArray &sha256)
{
    return _cacheManager->isCached(sha256);
}

/* Utility function to return filename part from URL */
QString ImageWriter::fileNameFromUrl(const QUrl &url)
{
    //return QFileInfo(url.toLocalFile()).fileName();
    return url.fileName();
}

QString ImageWriter::srcFileName()
{
    return _src.isEmpty() ? "" : _src.fileName();
}

quint64 ImageWriter::getSelectedSourceSize()
{
    // If we know uncompressed length, prefer that; else return download length
    if (_extrLen > 0) return _extrLen;
    if (_downloadLen > 0) return _downloadLen;
    return 0;
}

QString ImageWriter::formatSize(quint64 bytes, int decimals)
{
    const quint64 KB = 1024ULL;
    const quint64 MB = KB * 1024ULL;
    const quint64 GB = MB * 1024ULL;
    const quint64 TB = GB * 1024ULL;

    quint64 unit = 1;
    QString unitStr = tr("B");
    if (bytes >= TB) { unit = TB; unitStr = tr("TB"); }
    else if (bytes >= GB) { unit = GB; unitStr = tr("GB"); }
    else if (bytes >= MB) { unit = MB; unitStr = tr("MB"); }
    else if (bytes >= KB) { unit = KB; unitStr = tr("KB"); }

    // Integer rounding using quotient/remainder, avoiding floating point
    if (decimals <= 0) {
        quint64 rounded = (bytes + unit/2) / unit; // round-to-nearest
        return tr("%1 %2").arg(QString::number(rounded)).arg(unitStr);
    }

    // Compute with 'decimals' fractional digits without overflow
    quint64 scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10ULL; // small decimals (e.g., 1) expected

    quint64 q = bytes / unit;
    quint64 r = bytes % unit;
    quint64 fracScaled = (r * scale + unit/2) / unit; // rounded fractional part

    // Normalize carry if rounding overflowed fractional part
    if (fracScaled >= scale) {
        q += 1;
        fracScaled -= scale;
    }

    if (fracScaled == 0) {
        return tr("%1 %2").arg(QString::number(q)).arg(unitStr);
    }

    QString fracStr = QString::number(fracScaled);
    if (fracStr.length() < decimals) {
        fracStr = QString(decimals - fracStr.length(), QChar('0')) + fracStr;
    }
    return tr("%1.%2 %3").arg(QString::number(q), fracStr, unitStr);
}

QString ImageWriter::osListUrlForDisplay() const {
    return _repo.toString(QUrl::PreferLocalFile | QUrl::NormalizePathSegments);
}

/* Function to return current OS list URL (may be customized) */
QUrl ImageWriter::osListUrl() const
{
    return _repo;
}

/* Static version - for use without creating an instance */
QString ImageWriter::staticVersion()
{
    return IMAGER_VERSION_STR;
}

/* Instance method - delegates to static version (needed for QML Q_INVOKABLE) */
QString ImageWriter::constantVersion() const
{
    return staticVersion();
}

/* Returns true if version argument is newer than current program */
bool ImageWriter::isVersionNewer(const QString &version)
{
    return QVersionNumber::fromString(version) > QVersionNumber::fromString(IMAGER_VERSION_STR);
}

void ImageWriter::setCustomOsListUrl(const QUrl &url)
{
    _repo = url;
}

namespace {
    QJsonArray findAndInsertJsonResult(QJsonArray parent_list, QJsonArray incomingBody, QUrl referenceUrl, uint8_t count) {
        if (count > MAX_SUBITEMS_DEPTH) {
            qDebug() << "Aborting insertion of subitems, exceeded maximum configured limit of " << MAX_SUBITEMS_DEPTH << " levels.";
            return {};
        }

        QJsonArray returnArray = {};

        for (auto ositem : parent_list) {
            auto ositemObject = ositem.toObject();

            if (ositemObject.contains("subitems")) {
                // Recurse!
                ositemObject["subitems"] = findAndInsertJsonResult(ositemObject["subitems"].toArray(), incomingBody, referenceUrl, count + 1);
            } else if (ositemObject.contains("subitems_url")) {
                if ( !ositemObject["subitems_url"].toString().compare(referenceUrl.toString())) {
                    ositemObject.insert("subitems", incomingBody);
                    ositemObject.remove("subitems_url");
                }
            }

            returnArray += ositemObject;
        }

        return returnArray;
    }

    // Centralized URL preflight validation for fetches
    bool preflightValidateUrl(const QUrl &url, const QString &context)
    {
        if (!url.isValid() || url.scheme().isEmpty()) {
            qWarning() << context << "invalid URL (missing/invalid scheme):" << url.toString();
            return false;
        }
        const QString scheme = url.scheme().toLower();
        if ((scheme == QLatin1String("http") || scheme == QLatin1String("https")) && url.host().isEmpty()) {
            qWarning() << context << "invalid URL (no host):" << url.toString();
            return false;
        }
        if (url.isLocalFile()) {
            QFileInfo fi(url.toLocalFile());
            if (!fi.exists() || !fi.isFile()) {
                qWarning() << context << "invalid URL (local file missing/not regular):" << url.toString();
                return false;
            }
        }
        return true;
    }

    // Custom attribute to store request start time for performance tracking
    static const QNetworkRequest::Attribute RequestStartTimeAttribute = 
        static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1);
    
    void findAndQueueUnresolvedSubitemsJson(QJsonArray incoming, QNetworkAccessManager &manager, uint8_t count) {
        if (count > MAX_SUBITEMS_DEPTH) {
            qDebug() << "Aborting fetch of subitems JSON, exceeded maximum configured limit of " << MAX_SUBITEMS_DEPTH << " levels.";
            return;
        }

        for (auto entry : incoming) {
            auto entryObject = entry.toObject();
            if (entryObject.contains("subitems")) {
                // No need to handle a return - this isn't processing a list, it's searching and queuing downloads.
                findAndQueueUnresolvedSubitemsJson(entryObject["subitems"].toArray(), manager, count + 1);
            } else if (entryObject.contains("subitems_url")) {
                const QString subUrlStr = entryObject["subitems_url"].toString();
                const QUrl subUrl(subUrlStr);
                if (!preflightValidateUrl(subUrl, QStringLiteral("subitems_url:"))) {
                    continue;
                }

                QNetworkRequest request(subUrl);
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                        QNetworkRequest::NoLessSafeRedirectPolicy);
                request.setMaximumRedirectsAllowed(3);
                // Store start time in request attribute for performance tracking
                request.setAttribute(RequestStartTimeAttribute, QDateTime::currentMSecsSinceEpoch());
                manager.get(request);
            }
        }
    }
} // namespace anonymous


void ImageWriter::setHWFilterList(const QJsonArray &tags, const bool &inclusive) {
    _deviceFilter = tags;
    _deviceFilterIsInclusive = inclusive;
    emit hwFilterChanged();
}

void ImageWriter::setHWCapabilitiesList(const QJsonArray &json) {
    // TODO: maybe also clear the sw capabilities as in the UI the OS is unselected when this changes
    _hwCapabilities = json;
}

static inline void pushLower(QStringList &dst, const QString &s) {
    const QString t = s.trimmed().toLower();
    if (!t.isEmpty()) dst.push_back(t);
}

void ImageWriter::setSWCapabilitiesList(const QString &json) {
    _swCapabilities = QJsonArray{};
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isArray()) {
        _swCapabilities = doc.array();
    } else {
        // optional CSV fallback ("a,b,c")
        for (const auto &p : json.split(',', Qt::SkipEmptyParts))
            _swCapabilities.append(p.trimmed().toLower());
    }
}

QJsonArray ImageWriter::getHWFilterList() {
    return _deviceFilter;
}

bool ImageWriter::getHWFilterListInclusive() {
    return _deviceFilterIsInclusive;
}

bool ImageWriter::checkHWAndSWCapability(const QString &cap, const QString &differentSWCap) {
    return this->checkHWCapability(cap) && this->checkSWCapability(differentSWCap.isEmpty() ? cap : differentSWCap);
}

bool ImageWriter::checkHWCapability(const QString &cap) {
    return _hwCapabilities.contains(cap.trimmed().toLower());
}

bool ImageWriter::checkSWCapability(const QString &cap) {
    const auto needle = cap.trimmed().toLower();
    for (const auto &v : _swCapabilities)
        if (v.toString() == needle) return true;
    return false;
}

void ImageWriter::handleNetworkRequestFinished(QNetworkReply *data) {
    // Defer deletion
    data->deleteLater();
    
    // Calculate request duration for performance tracking
    quint32 durationMs = 0;
    qint64 bytesReceived = data->bytesAvailable();
    
    // Check for start time in hash map (top-level requests)
    if (_networkRequestStartTimes.contains(data)) {
        qint64 startTime = _networkRequestStartTimes.take(data);
        durationMs = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() - startTime);
    } 
    // Check for start time in request attribute (sublist requests)
    else {
        QVariant startTimeAttr = data->request().attribute(
            static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1));
        if (startTimeAttr.isValid()) {
            qint64 startTime = startTimeAttr.toLongLong();
            durationMs = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() - startTime);
        }
    }
    
    // Track if this is the top-level OS list request
    bool isTopLevelRequest = (data->request().url() == osListUrl());

    if (data->error() == QNetworkReply::NoError) {
        auto httpStatusCode = data->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (httpStatusCode >= 200 && httpStatusCode < 300 || httpStatusCode == 0) {
            auto response_object = QJsonDocument::fromJson(data->readAll()).object();

            if (response_object.contains("os_list")) {
                // Step 1: Insert the items into the canonical JSON document.
                //         It doesn't matter that these may still contain subitems_url items
                //         As these will be fixed up as the subitems_url instances are blinked in
                if (_completeOsList.isEmpty()) {
                    _completeOsList = QJsonDocument(response_object);
                } else {
                    // Preserve latest top-level imager metadata if present in the top-level fetch
                    auto new_list = findAndInsertJsonResult(_completeOsList["os_list"].toArray(), response_object["os_list"].toArray(), data->request().url(), 1);
                    QJsonObject imager_meta = _completeOsList["imager"].toObject();
                    if (response_object.contains("imager") && isTopLevelRequest) {
                        // Update imager metadata when this reply is for the top-level OS list
                        imager_meta = response_object["imager"].toObject();
                    }
                    _completeOsList = QJsonDocument(QJsonObject({
                        {"imager", imager_meta},
                        {"os_list", new_list}
                    }));
                }

                findAndQueueUnresolvedSubitemsJson(response_object["os_list"].toArray(), _networkManager, 1);
                emit osListPrepared();
                
                // Record performance event for OS list fetch
                if (durationMs > 0) {
                    PerformanceStats::EventType eventType = isTopLevelRequest 
                        ? PerformanceStats::EventType::OsListFetch 
                        : PerformanceStats::EventType::SublistFetch;
                    QString metadata = data->request().url().toString();
                    _performanceStats->recordTransferEvent(eventType, durationMs, bytesReceived, true, metadata);
                }

                // After processing a top-level list fetch, (re)schedule the next refresh
                if (isTopLevelRequest) {
                    scheduleOsListRefresh();
                }
            } else {
                qDebug() << "Incorrectly formatted OS list at: " << data->url();
                // If this was the top-level fetch and it failed, treat as network failure
                if (isTopLevelRequest && _completeOsList.isEmpty()) {
                    qWarning() << "Top-level OS list fetch failed - malformed response. Operating in offline mode.";
                    emit osListFetchFailed();
                }
            }
        } else if (httpStatusCode >= 300 && httpStatusCode < 400) {
            // We should _never_ enter this branch. All requests are set to follow redirections
            // at their call sites - so the only way you got here was a logic defect.
            const QUrl redirUrl = data->url();
            if (!preflightValidateUrl(redirUrl, QStringLiteral("redirect:"))) {
                return;
            }

            auto request = QNetworkRequest(redirUrl);
            request.setAttribute(QNetworkRequest::RedirectionTargetAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            request.setMaximumRedirectsAllowed(3);
            data->manager()->get(request);

            // maintain manager
            return;
        } else if (httpStatusCode >= 400 && httpStatusCode < 600) {
            // HTTP Error
            qDebug() << "Failed to fetch URL [" << data->url() << "], got: " << httpStatusCode;
            // If this was the top-level fetch and it failed, treat as network failure
            if (isTopLevelRequest && _completeOsList.isEmpty()) {
                qWarning() << "Top-level OS list fetch failed with HTTP error" << httpStatusCode << ". Operating in offline mode.";
                emit osListFetchFailed();
            }
        } else {
            // Completely unknown error, worth logging separately
            qDebug() << "Failed to fetch URL [" << data->url() << "], got unknown response code: " << httpStatusCode;
            // If this was the top-level fetch and it failed, treat as network failure
            if (isTopLevelRequest && _completeOsList.isEmpty()) {
                qWarning() << "Top-level OS list fetch failed with unknown error. Operating in offline mode.";
                emit osListFetchFailed();
            }
        }
    } else {
        // QT Error - provide more actionable context when common misconfigurations are detected
        const QNetworkReply::NetworkError err = data->error();
        const QString errStr = data->errorString();

        // Detect scheme-less URL usage which commonly happens with local repo files
        const QUrl failedUrl = data->request().url();
        if ((err == QNetworkReply::ProtocolFailure || err == QNetworkReply::UnknownNetworkError || err == QNetworkReply::ProtocolUnknownError)
            && (failedUrl.scheme().isEmpty() || !failedUrl.isValid()))
        {
            qWarning() << "Failed to fetch subitems URL '" << failedUrl.toString()
                       << "' - the URL appears to be relative or missing a scheme."
                       << "When using a local --repo file, ensure all subitems_url entries are absolute"
                       << "(e.g., file:///path/to/sublist.json or https://...).";
        }

        qDebug() << "Unrecognised QT error: " << err << ", explainer: " << errStr;
        
        // If this was the top-level fetch and it failed, treat as network failure
        if (isTopLevelRequest && _completeOsList.isEmpty()) {
            qWarning() << "Top-level OS list fetch failed with network error:" << errStr << ". Operating in offline mode.";
            emit osListFetchFailed();
        }
    }
}

namespace {
    QJsonArray filterOsListWithHWTags(QJsonArray incoming_os_list, QJsonArray hw_filter, const bool inclusive, uint8_t count) {
        if (count > MAX_SUBITEMS_DEPTH) {
            qDebug() << "Aborting insertion of subitems, exceeded maximum configured limit of " << MAX_SUBITEMS_DEPTH << " levels.";
            return {};
        }

        QJsonArray returnArray = {};

        for (auto ositem : incoming_os_list) {
            auto ositemObject = ositem.toObject();

            if (ositemObject.contains("subitems")) {
                // Recurse!
                ositemObject["subitems"] = filterOsListWithHWTags(ositemObject["subitems"].toArray(), hw_filter, inclusive, count + 1);
                if (ositemObject["subitems"].toArray().count() > 0) {
                    returnArray += ositemObject;
                }
            } else {
                // Filter this one!
                if (ositemObject.contains("devices")) {
                    auto keep = false;
                    const auto ositem_devices = ositemObject["devices"].toArray();

                    for (auto compat_device : ositem_devices) {
                        if (hw_filter.contains(compat_device.toString())) {
                            keep = true;
                            break;
                        }
                    }

                    if (keep) {
                        returnArray.append(ositem);
                    }
                } else {
                    // No devices tags, so work out if we're exclusive or inclusive filtering!
                    if (inclusive) {
                        returnArray.append(ositem);
                    }
                }
            }
        }

        return returnArray;
    }
} // namespace anonymous

QByteArray ImageWriter::getFilteredOSlist()
{
    return getFilteredOSlistDocument().toJson();
}

QJsonDocument ImageWriter::getFilteredOSlistDocument() {
    QJsonArray reference_os_list_array = {};
    QJsonObject reference_imager_metadata = {};

    if (_device_info->hardwareTagsSet()) {
        _deviceFilter = _device_info->getHardwareTags();
    }

    {
        if (!_completeOsList.isEmpty()) {
            if (!_deviceFilter.isEmpty()) {
                reference_os_list_array = filterOsListWithHWTags(_completeOsList.object().value("os_list").toArray(), _deviceFilter, _deviceFilterIsInclusive, 1);
            } else {
                // The device filter can be an empty array when a device filter has not been selected, or has explicitly been selected as
                // "no filtering". In that case, avoid walking the tree and use the unfiltered list.
                reference_os_list_array = _completeOsList.object().value("os_list").toArray();
            }

            reference_imager_metadata = _completeOsList.object().value("imager").toObject();
        }
    }

    reference_os_list_array.append(QJsonObject({
            {"name", QCoreApplication::translate("main", "Erase")},
            {"description", QCoreApplication::translate("main", "Format card as FAT32")},
            {"icon", "../icons/erase.png"},
            {"url", "internal://format"},
        }));

    reference_os_list_array.append(QJsonObject({
            {"name", QCoreApplication::translate("main", "Use custom")},
            {"description", QCoreApplication::translate("main", "Select a custom .img from your computer")},
            {"icon", "../icons/use_custom.png"},
            {"url", "internal://custom"},
        }));

    return QJsonDocument(
        QJsonObject({
            {"imager", reference_imager_metadata},
            {"os_list", reference_os_list_array},
        }
    ));
}

void ImageWriter::beginOSListFetch() {
    const QUrl topUrl = osListUrl();
    if (!preflightValidateUrl(topUrl, QStringLiteral("repository:"))) {
        return;
    }

    QNetworkRequest request = QNetworkRequest(topUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    // This will set up a chain of requests that culiminate in the eventual fetch and assembly of
    // a complete cached OS list.
    QNetworkReply *reply = _networkManager.get(request);
    _networkRequestStartTimes[reply] = QDateTime::currentMSecsSinceEpoch();
}

void ImageWriter::refreshOsListFrom(const QUrl &url) {
    setCustomRepo(url);
    _completeOsList = QJsonDocument();
    _osListRefreshTimer.stop();
    beginOSListFetch();
}

void ImageWriter::refreshOsListFromDefaultUrl() {
    refreshOsListFrom(QUrl(QString(OSLIST_URL)));
}

void ImageWriter::onOsListRefreshTimeout()
{
    qDebug() << "OS list refresh timer fired - refetching";
    beginOSListFetch();
}

void ImageWriter::scheduleOsListRefresh()
{
    // Default: do not refresh if we cannot read settings from current _completeOsList
    int baseMinutes = 0;
    int jitterMinutes = 0;

    // CLI overrides take precedence when set (>= 0)
    if (_refreshIntervalOverrideMinutes >= 0) {
        baseMinutes = _refreshIntervalOverrideMinutes;
    }
    if (_refreshJitterOverrideMinutes >= 0) {
        jitterMinutes = _refreshJitterOverrideMinutes;
    }

    if (!_completeOsList.isEmpty()) {
        QJsonObject root = _completeOsList.object();
        if (root.contains("imager")) {
            QJsonObject imager = root.value("imager").toObject();
            // New optional fields
            if (baseMinutes <= 0 && imager.contains("refresh_interval_minutes")) {
                baseMinutes = imager.value("refresh_interval_minutes").toInt(0);
            }
            if (jitterMinutes <= 0 && imager.contains("refresh_jitter_minutes")) {
                jitterMinutes = imager.value("refresh_jitter_minutes").toInt(0);
            }
        }
    }

    if (baseMinutes <= 0) {
        // No refresh configured; stop timer
        _osListRefreshTimer.stop();
        qDebug() << "OS list refresh disabled (no interval provided)";
        return;
    }

    // Constrain jitter to non-negative
    if (jitterMinutes < 0) jitterMinutes = 0;

    // Compute randomized delay with second-level granularity
    // Base is in minutes; jitter is in minutes but applied as seconds
    const qint64 baseMs = static_cast<qint64>(baseMinutes) * 60 * 1000;
    const int jitterSeconds = jitterMinutes * 60;
    const int extraSeconds = (jitterSeconds > 0) ? QRandomGenerator::global()->bounded(jitterSeconds + 1) : 0;
    qint64 msec = baseMs + static_cast<qint64>(extraSeconds) * 1000;

    // Cap to a reasonable max to avoid overflow (e.g., ~30 days)
    const qint64 maxMs = static_cast<qint64>(30) * 24 * 60 * 60 * 1000;
    if (msec > maxMs) msec = maxMs;

    _osListRefreshTimer.start(msec);
    qDebug() << "Scheduled OS list refresh in" << (msec/1000) << "seconds (base" << (baseMs/1000) << "+ jitter" << extraSeconds << ")";
}

void ImageWriter::setOsListRefreshOverride(int intervalMinutes, int jitterMinutes)
{
    _refreshIntervalOverrideMinutes = intervalMinutes;
    _refreshJitterOverrideMinutes = jitterMinutes;
    // If we already have a list, reschedule now
    if (!_completeOsList.isEmpty()) {
        scheduleOsListRefresh();
    }
}

// TODO: duplicate
void ImageWriter::setCustomRepo(const QUrl &repo)
{
    _repo = repo;
}

void ImageWriter::setCustomCacheFile(const QString &cacheFile, const QByteArray &sha256)
{
    _cacheManager->setCustomCacheFile(cacheFile, sha256);
}

/* Drive list polling runs continuously in background - no explicit start/stop needed */

DriveListModel *ImageWriter::getDriveList()
{
    return &_drivelist;
}

HWListModel *ImageWriter::getHWList()
{
    return &_hwlist;
}

OSListModel *ImageWriter::getOSList()
{
    return &_oslist;
}

void ImageWriter::startProgressPolling()
{
    // Prevent system suspend and display sleep during imaging
    try
    {
        if (_suspendInhibitor == nullptr)
            _suspendInhibitor = CreateSuspendInhibitor();
    }
    catch (...)
    {
        // If we can't create the inhibitor, continue anyway
    }
    _dlnow = 0;
    _verifynow = 0;
}

void ImageWriter::stopProgressPolling()
{
    // Release the inhibition on system suspend and display sleep
    if (_suspendInhibitor != nullptr)
    {
        delete _suspendInhibitor;
        _suspendInhibitor = nullptr;
    }
}

void ImageWriter::setWriteState(WriteState state)
{
    if (_writeState == state)
        return;
    
    WriteState oldState = _writeState;
    _writeState = state;
    
    // Adaptive drive scanning based on write state
    // Pause scanning during write/verify to avoid I/O contention
    // Windows drive enumeration is expensive (~1.5-2 seconds per poll)
    switch (state) {
        case WriteState::Idle:
            // Back to device selection - resume normal scanning
            _drivelist.resumePolling();
            break;
            
        case WriteState::Preparing:
        case WriteState::Writing:
        case WriteState::Verifying:
        case WriteState::Finalizing:
            // Pause all scanning during write operations
            // Device removal will be detected by I/O errors in the write thread
            // This avoids 20+ seconds of overhead from drive enumeration
            if (oldState == WriteState::Idle) {
                qDebug() << "Pausing drive scanning during write operation";
                _drivelist.pausePolling();
            }
            break;
            
        case WriteState::Succeeded:
        case WriteState::Failed:
        case WriteState::Cancelled:
            // Operation complete - use slow polling until user navigates away
            // Full resume happens when returning to device selection (Idle state)
            qDebug() << "Write complete, switching to slow drive scanning";
            _drivelist.setSlowPolling();
            break;
    }
    
    emit writeStateChanged();
}

void ImageWriter::setVerifyEnabled(bool verify)
{
    _verifyEnabled = verify;
    if (_thread)
        _thread->setVerifyEnabled(verify);
}

/* Relay events from download thread to QML */
void ImageWriter::onSuccess()
{
    setWriteState(WriteState::Succeeded);
    stopProgressPolling();
    
    // End performance stats session
    _performanceStats->endSession(true);
    
    // Clear Pi Connect token on successful write completion
    clearConnectToken();
    
    emit success();

    if (_settings.value("beep").toBool()) {
        PlatformQuirks::beep();
    }
}

void ImageWriter::onError(QString msg)
{
    // Guard against duplicate errors - device removal disconnects error signal,
    // but this catches any already-queued signals
    if (_cancelledDueToDeviceRemoval || 
        _writeState == WriteState::Failed || 
        _writeState == WriteState::Cancelled) {
        qDebug() << "Ignoring duplicate/late error:" << msg;
        return;
    }
    
    setWriteState(WriteState::Failed);
    stopProgressPolling();
    
    // End performance stats session with error
    _performanceStats->endSession(false, msg);
    
    emit error(msg);

    if (_settings.value("beep").toBool()) {
        PlatformQuirks::beep();
    }
}

void ImageWriter::onFinalizing()
{
    setWriteState(WriteState::Finalizing);
    _performanceStats->recordFinalising();
    emit finalizing();
}

void ImageWriter::onPreparationStatusUpdate(QString msg)
{
    // heuristic: entering verifying is handled via progress elsewhere
    emit preparationStatusUpdate(msg);
}

void ImageWriter::openFileDialog(const QString &title, const QString &filter)
{
#ifndef CLI_ONLY_BUILD
    QSettings settings;
    QString path = settings.value("lastpath").toString();
    QFileInfo fi(path);

    if (path.isEmpty() || !fi.exists() || !fi.isReadable() )
        path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    // Use native file dialog with modal behavior to main window
    QString filename = NativeFileDialog::getOpenFileName(tr("Select image"),
                                                        path,
                                                        filter,
                                                        _mainWindow);

    // Process the selected file if one was chosen
    if (!filename.isEmpty())
    {
        onFileSelected(filename);
    }
#else
    Q_UNUSED(title);
    Q_UNUSED(filter);
#endif
}

void ImageWriter::onFileSelected(QString filename)
{
    QFileInfo fi(filename);
    QSettings settings;

    if (fi.isFile())
    {
        QString path = fi.path();
        if (path != settings.value("lastpath"))
        {
            settings.setValue("lastpath", path);
            settings.sync();
        }

        emit fileSelected(QUrl::fromLocalFile(filename));
    }
    else
    {
        qDebug() << "Item selected is not a regular file";
    }

    // Only delete sender if called from a signal/slot connection
    QObject *senderObj = sender();
    if (senderObj)
    {
        senderObj->deleteLater();
    }
}

void ImageWriter::_parseCompressedFile()
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    QByteArray fn = _src.toLocalFile().toLatin1();
    int numFiles = 0;
    _extrLen = 0;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, fn.data(), 10240) == ARCHIVE_OK)
    {
        while ( (archive_read_next_header(a, &entry)) == ARCHIVE_OK)
        {
            if (archive_entry_size(entry) > 0)
            {
              _extrLen += archive_entry_size(entry);
              numFiles++;
            }
        }
    }

    if (numFiles > 1)
        _multipleFilesInZip = true;

    qDebug() << "Parsed .zip file containing" << numFiles << "files, uncompressed size:" << _extrLen;
}

void ImageWriter::_parseXZFile()
{
    QFile f(_src.toLocalFile());
    lzma_stream_flags opts = { 0 };
    _extrLen = 0;

    if (f.size() > LZMA_STREAM_HEADER_SIZE && f.open(f.ReadOnly))
    {
        f.seek(f.size()-LZMA_STREAM_HEADER_SIZE);
        QByteArray footer = f.read(LZMA_STREAM_HEADER_SIZE);
        lzma_ret ret = lzma_stream_footer_decode(&opts, (const uint8_t *) footer.constData());

        if (ret == LZMA_OK && opts.backward_size < 1000000 && opts.backward_size < f.size()-LZMA_STREAM_HEADER_SIZE)
        {
            f.seek(f.size()-LZMA_STREAM_HEADER_SIZE-opts.backward_size);
            QByteArray buf = f.read(opts.backward_size+LZMA_STREAM_HEADER_SIZE);
            lzma_index *idx;
            uint64_t memlimit = UINT64_MAX;
            size_t pos = 0;

            ret = lzma_index_buffer_decode(&idx, &memlimit, NULL, (const uint8_t *) buf.constData(), &pos, buf.size());
            if (ret == LZMA_OK)
            {
                _extrLen = lzma_index_uncompressed_size(idx);
                qDebug() << "Parsed .xz file. Uncompressed size:" << _extrLen;
            }
            else
            {
                qDebug() << "Unable to parse index of .xz file";
            }
            lzma_index_end(idx, NULL);
        }
        else
        {
            qDebug() << "Unable to parse footer of .xz file";
        }

        f.close();
    }
}

bool ImageWriter::isOnline()
{
    // Use platform abstraction for network connectivity check
    bool hasBasicConnectivity = PlatformQuirks::hasNetworkConnectivity();
    
    if (hasBasicConnectivity) {
        /* Report detected IP addresses for embedded mode status display */
        QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
        foreach (QHostAddress a, addresses)
        {
            if (!a.isLoopback() && a.scopeId().isEmpty())
            {
                qDebug() << "IP DETECTED: " << a.toString();
                emit networkInfo(QString("IP: %1").arg(a.toString()));
                break;
            }
        }
    }
    
    // For embedded mode, check if network is truly ready (including time sync)
    if (hasBasicConnectivity && isEmbeddedMode()) {
        bool networkReady = PlatformQuirks::isNetworkReady();
        if (networkReady) {
            _networkchecktimer.stop();
            beginOSListFetch();
            emit networkOnline();
        }
        return networkReady;
    }
    
    return hasBasicConnectivity;
}

void ImageWriter::pollNetwork()
{
    isOnline();
}

void ImageWriter::onSTPdetected()
{
    // Only show STP warning if we don't already have an IP address
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    bool hasIP = false;
    foreach (QHostAddress a, addresses)
    {
        if (!a.isLoopback() && a.scopeId().isEmpty())
        {
            hasIP = true;
            break;
        }
    }
    
    if (!hasIP)
    {
        emit networkInfo(tr("STP is enabled on your Ethernet switch. Getting IP will take long time."));
    }
}

bool ImageWriter::isEmbeddedMode() const
{
    return ::isEmbeddedMode();
}

/* Mount any USB sticks that can contain source images under /media */
bool ImageWriter::mountUsbSourceMedia()
{
    int devices = 0;
#ifdef Q_OS_LINUX
    QDir dir("/sys/class/block");
    const QStringList list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    if (!dir.exists("/media"))
        dir.mkdir("/media");

    for (const QString &devname : list)
    {
        if (!devname.startsWith("mmcblk0") && !QFile::symLinkTarget("/sys/class/block/"+devname).contains("/devices/virtual/"))
        {
            QString mntdir = "/media/"+devname;

            if (dir.exists(mntdir))
            {
                devices++;
                continue;
            }

            dir.mkdir(mntdir);
            QStringList args = { "-o", "ro", QString("/dev/")+devname, mntdir };

            if ( QProcess::execute("mount", args) == 0 )
                devices++;
            else
                dir.rmdir(mntdir);
        }
    }
#endif
    return devices > 0;
}

QByteArray ImageWriter::getUsbSourceOSlist()
{
#ifdef Q_OS_LINUX
    QJsonArray oslist;
    QDir dir("/media");
    const QStringList medialist = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList namefilters = {"*.img", "*.zip", "*.gz", "*.xz", "*.zst", "*.wic"};

    for (const QString &devname : medialist)
    {
        QDir subdir("/media/"+devname);
        const QStringList files = subdir.entryList(namefilters, QDir::Files, QDir::Name);
        for (const QString &file : files)
        {
            QString path = "/media/"+devname+"/"+file;
            QFileInfo fi(path);

            QJsonObject f = {
                {"name", file},
                {"description", devname+"/"+file},
                {"url", QUrl::fromLocalFile(path).toString() },
                {"release_date", ""},
                {"image_download_size", fi.size()}
            };
            oslist.append(f);
        }
    }

    return QJsonDocument(oslist).toJson();
#else
    return QByteArray();
#endif
}

QString ImageWriter::_sshKeyDir()
{
    return QDir::homePath()+"/.ssh";
}

QString ImageWriter::_pubKeyFileName()
{
    return _sshKeyDir()+"/id_rsa.pub";
}

QString ImageWriter::_privKeyFileName()
{
    return _sshKeyDir()+"/id_rsa";
}

QString ImageWriter::getDefaultPubKey()
{
    QByteArray pubkey;
    QFile pubfile(_pubKeyFileName());

    if (pubfile.exists() && pubfile.open(QFile::ReadOnly))
    {
        pubkey = pubfile.readAll().trimmed();
        pubfile.close();
    }

    return pubkey;
}

bool ImageWriter::hasPubKey()
{
    return QFile::exists(_pubKeyFileName());
}

QString ImageWriter::_sshKeyGen()
{
#ifdef Q_OS_WIN
    QString windir = QProcessEnvironment::systemEnvironment().value("windir");
    return QDir::fromNativeSeparators(windir+"\\SysNative\\OpenSSH\\ssh-keygen.exe");
#else
    return "ssh-keygen";
#endif
}

bool ImageWriter::hasSshKeyGen()
{
#ifdef Q_OS_WIN
    return QFile::exists(_sshKeyGen());
#else
    return !isEmbeddedMode();
#endif
}

void ImageWriter::generatePubKey()
{
    if (!hasPubKey() && !QFile::exists(_privKeyFileName()))
    {
        QDir dir;
        QProcess proc;
        QString progName = _sshKeyGen();
        QStringList args;
        args << "-t" << "rsa" << "-f" << _privKeyFileName() << "-N" << "";

        if (!dir.exists(_sshKeyDir()))
        {
            qDebug() << "Creating" << _sshKeyDir();
            dir.mkdir(_sshKeyDir());
        }

        qDebug() << "Executing:" << progName << args;
        proc.start(progName, args);
        proc.waitForFinished();
        qDebug() << proc.readAll();
    }
}

QString ImageWriter::getTimezone()
{
    return QTimeZone::systemTimeZoneId();
}

QStringList ImageWriter::getTimezoneList()
{
    QStringList timezones;
    QFile f(":/timezones.txt");
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        timezones = QString::fromUtf8(f.readAll()).split('\n');
        for (QString &s : timezones)
            s = s.trimmed();
        f.close();
    }

    return timezones;
}

QStringList ImageWriter::getCountryList()
{
    QStringList countries;
    QFile f(":/countries.txt");
    if (f.open(QFile::ReadOnly | QFile::Text))
    {
        countries = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        for (QString &s : countries) s = s.trimmed();
        f.close();
    }

    return countries;
}

QStringList ImageWriter::getKeymapLayoutList()
{
    QFile f(":/keymap-layouts.txt");
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return {};

    QStringList list = QString::fromUtf8(f.readAll())
                           .split('\n', Qt::SkipEmptyParts);
    for (QString &s : list)
        s = s.trimmed();
    return list;
}

QStringList ImageWriter::getCapitalCitiesList()
{
    QStringList cities;
    QFile f(":/capital-cities.txt");
    if (f.open(QFile::ReadOnly | QFile::Text))
    {
        QTextStream in(&f);
        while (!in.atEnd())
        {
            QString line = in.readLine().trimmed();
            // Skip empty lines and comments
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            
            // Parse: CityName|CountryName|CountryCode|Timezone|Language|KeyboardLayout
            QStringList parts = line.split('|');
            if (parts.size() >= 2)
            {
                QString cityName = parts[0];
                QString countryName = parts[1];
                // Format as "City (Country)"
                QString displayName = cityName + " (" + countryName + ")";
                cities.append(displayName);
            }
        }
        f.close();
    }
    
    return cities;
}

QVariantMap ImageWriter::getLocaleDataForCapital(const QString &capitalCity)
{
    QVariantMap result;
    QFile f(":/capital-cities.txt");
    
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return result;
    
    // Extract just the city name from "City (Country)" format
    QString cityNameOnly = capitalCity;
    int parenIndex = capitalCity.indexOf(" (");
    if (parenIndex > 0)
    {
        cityNameOnly = capitalCity.left(parenIndex);
    }
    
    QTextStream in(&f);
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        // Skip empty lines and comments
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        
        // Parse: CityName|CountryName|CountryCode|Timezone|Language|KeyboardLayout
        QStringList parts = line.split('|');
        if (parts.size() >= 6 && parts[0] == cityNameOnly)
        {
            result["cityName"] = parts[0];
            result["countryName"] = parts[1];
            result["countryCode"] = parts[2];
            result["timezone"] = parts[3];
            result["language"] = parts[4];
            result["keyboard"] = parts[5];
            break;
        }
    }
    f.close();
    
    return result;
}

#ifdef Q_OS_DARWIN
#include "mac/location_helper.h"

// Static callback for location permission - called when permission is granted after timeout
static void locationPermissionCallback(int authorized, void *context)
{
    if (authorized && context) {
        ImageWriter *writer = static_cast<ImageWriter*>(context);
        qDebug() << "Location permission granted via callback, emitting signal";
        QMetaObject::invokeMethod(writer, "locationPermissionGranted", Qt::QueuedConnection);
    }
}
#endif

QString ImageWriter::getSSID()
{
#ifdef Q_OS_DARWIN
    // On macOS, set up a callback to be notified if location permission is granted
    // after the initial timeout. This handles the race condition where the user
    // takes longer than 5 seconds to click "Allow" in the permission dialog.
    // Pass 'this' as context so the callback can emit the signal.
    rpiimager_request_location_permission_async(locationPermissionCallback, this);
#endif
    return WlanCredentials::instance()->getSSID();
}

QString ImageWriter::getPSK()
{
#ifdef Q_OS_DARWIN
    /* On OSX the user is presented with a prompt for the admin password when opening the system key chain.
     * Request user permission through QML dialog instead of QtWidgets QMessageBox. */

    // Set up a flag to track if permission was granted
    _keychainPermissionGranted = false;
    _keychainPermissionReceived = false;

    // Emit signal to show QML permission dialog
    emit keychainPermissionRequested();

    // Wait for user response (with timeout)
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000); // 30 second timeout

    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &ImageWriter::keychainPermissionResponseReceived, &loop, &QEventLoop::quit);

    timeout.start();
    loop.exec();

    if (!_keychainPermissionReceived || !_keychainPermissionGranted) {
        qDebug() << "Keychain access denied or timed out";
        return QString();
    }

    qDebug() << "Keychain access granted by user";
#endif

    return WlanCredentials::instance()->getPSK();
}

QString ImageWriter::getPSKForSSID(const QString &ssid)
{
    if (ssid.isEmpty()) {
        qDebug() << "ImageWriter::getPSKForSSID(): Empty SSID provided, cannot retrieve PSK";
        return QString();
    }

#ifdef Q_OS_DARWIN
    /* On OSX the user is presented with a prompt for the admin password when opening the system key chain.
     * Request user permission through QML dialog instead of QtWidgets QMessageBox. */

    // Set up a flag to track if permission was granted
    _keychainPermissionGranted = false;
    _keychainPermissionReceived = false;

    // Emit signal to show QML permission dialog
    emit keychainPermissionRequested();

    // Wait for user response (with timeout)
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000); // 30 second timeout

    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &ImageWriter::keychainPermissionResponseReceived, &loop, &QEventLoop::quit);

    timeout.start();
    loop.exec();

    if (!_keychainPermissionReceived || !_keychainPermissionGranted) {
        qDebug() << "Keychain access denied or timed out";
        return QString();
    }

    qDebug() << "Keychain access granted by user";
#endif

    return WlanCredentials::instance()->getPSKForSSID(ssid.toUtf8());
}

void ImageWriter::keychainPermissionResponse(bool granted)
{
    _keychainPermissionGranted = granted;
    _keychainPermissionReceived = true;
    emit keychainPermissionResponseReceived();
}

bool ImageWriter::getBoolSetting(const QString &key)
{
    /* Some keys have defaults */
    if (key == "telemetry")
        return _settings.value(key, TELEMETRY_ENABLED_DEFAULT).toBool();
    else if (key == "eject")
        return _settings.value(key, true).toBool();
    else if (key == "check_version")
        return _settings.value(key, CHECK_VERSION_DEFAULT).toBool();
    else
        return _settings.value(key).toBool();
}

QString ImageWriter::getStringSetting(const QString &key)
{
    return _settings.value(key).toString();
}

// Platform-specific implementation (defined in platform-specific source files)
extern QString getRsaKeyFingerprint(const QString &keyPath);

QString ImageWriter::getRsaKeyFingerprint(const QString &keyPath)
{
    // Delegate to platform-specific implementation
    return ::getRsaKeyFingerprint(keyPath);
}

void ImageWriter::setSetting(const QString &key, const QVariant &value)
{
    _settings.setValue(key, value);
    _settings.sync();
}

void ImageWriter::setForceSecureBootEnabled(bool enabled)
{
    _forceSecureBootEnabled = enabled;
    qDebug() << "Secure boot force-enabled via CLI flag:" << enabled;
}

bool ImageWriter::isSecureBootForcedByCliFlag() const
{
    return _forceSecureBootEnabled;
}

void ImageWriter::setImageCustomisation(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork, const ImageOptions::AdvancedOptions opts)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudinitNetwork;
    _advancedOptions = opts;

    qDebug() << "Custom config.txt entries:" << config;
    qDebug() << "Custom cmdline.txt entries:" << cmdline;
    qDebug() << "Custom firstrun.sh:" << firstrun;
    qDebug() << "Cloudinit:" << cloudinit;
    qDebug() << "Advanced options:" << opts;
}

void ImageWriter::applyCustomisationFromSettings(const QVariantMap &settings)
{
    // Build customisation payloads from provided settings map
    // This method accepts settings directly (which may be a combination of
    // persistent and ephemeral/runtime settings from the wizard)

    // If the selected image does not support customisation, ensure nothing is staged
    if (_initFormat.isEmpty()) {
        setImageCustomisation(QByteArray(), QByteArray(), QByteArray(), QByteArray(), QByteArray(), NoAdvancedOptions);
        return;
    }

    if (_initFormat == "systemd") {
        _applySystemdCustomisationFromSettings(settings);
    } else {
        // customisation for cloudinit and cloudinit-rpi
        _applyCloudInitCustomisationFromSettings(settings);
    }
}

void ImageWriter::_applySystemdCustomisationFromSettings(const QVariantMap &s)
{
    // Use CustomisationGenerator for script generation
    QByteArray script = rpi_imager::CustomisationGenerator::generateSystemdScript(s, _piConnectToken);
    
    // Extract recommendedWifiCountry for cmdline append
    QByteArray cmdlineAppend;
    const QString wifiCountry = s.value("recommendedWifiCountry").toString().trimmed();
    if (!wifiCountry.isEmpty()) {
        cmdlineAppend = QByteArray(" ") + QByteArray("cfg80211.ieee80211_regdom=") + wifiCountry.toUtf8();
    }

    // Check if secure boot should be enabled
    ImageOptions::AdvancedOptions advOpts = NoAdvancedOptions;
    bool secureBootEnabled = s.value("secureBootEnabled").toBool();
    QString rsaKeyPath = _settings.value("secureboot_rsa_key").toString();
    if (secureBootEnabled && !rsaKeyPath.isEmpty() && QFile::exists(rsaKeyPath)) {
        advOpts |= ImageOptions::EnableSecureBoot;
        qDebug() << "Secure boot enabled with RSA key:" << rsaKeyPath;
    }

    setImageCustomisation(QByteArray(), cmdlineAppend, script, QByteArray(), QByteArray(), advOpts);
}

void ImageWriter::_applyCloudInitCustomisationFromSettings(const QVariantMap &s)
{
    // Use CustomisationGenerator for cloud-init YAML generation
    const bool sshEnabled = s.value("sshEnabled").toBool();
    const bool hasCcRpi = imageSupportsCcRpi();
    
    QByteArray cloud = rpi_imager::CustomisationGenerator::generateCloudInitUserData(
        s, _piConnectToken, hasCcRpi, sshEnabled, getCurrentUser());
    
    QByteArray netcfg = rpi_imager::CustomisationGenerator::generateCloudInitNetworkConfig(
        s, hasCcRpi);
    
    // Extract recommendedWifiCountry for cmdline append
    QByteArray cmdlineAppend;
    const QString wifiCountry = s.value("recommendedWifiCountry").toString().trimmed();
    if (!wifiCountry.isEmpty()) {
        cmdlineAppend = QByteArray(" ") + QByteArray("cfg80211.ieee80211_regdom=") + wifiCountry.toUtf8();
    }
    
    // Check if secure boot should be enabled
    ImageOptions::AdvancedOptions advOpts = NoAdvancedOptions;
    bool secureBootEnabled = s.value("secureBootEnabled").toBool();
    QString rsaKeyPath = _settings.value("secureboot_rsa_key").toString();
    if (secureBootEnabled && !rsaKeyPath.isEmpty() && QFile::exists(rsaKeyPath)) {
        advOpts |= ImageOptions::EnableSecureBoot;
        qDebug() << "Secure boot enabled with RSA key:" << rsaKeyPath;
    }
    
    setImageCustomisation(QByteArray(), cmdlineAppend, QByteArray(), cloud, netcfg, advOpts);
}

QString ImageWriter::crypt(const QByteArray &password)
{
    QByteArray saltchars =
      "./0123456789ABCDEFGHIJKLMNOPQRST"
      "UVWXYZabcdefghijklmnopqrstuvwxyz";
    std::mt19937 gen(static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch()));
    std::uniform_int_distribution<> uid(0, saltchars.length()-1);

    // Determine whether to use yescrypt (for OS released after Jan 1, 2023) or sha256crypt
    bool useYescrypt = false;
    if (!_osReleaseDate.isEmpty()) {
        // Parse release date in format "YYYY-MM-DD"
        QDate releaseDate = QDate::fromString(_osReleaseDate, "yyyy-MM-dd");
        QDate cutoffDate(2023, 1, 1);
        if (releaseDate.isValid() && releaseDate >= cutoffDate) {
            useYescrypt = true;
        }
    }

    if (useYescrypt) {
        // Use yescrypt for newer OS releases
        QByteArray salt;
        for (int i=0; i<16; i++)  // yescrypt uses longer salts
            salt += saltchars[uid(gen)];
        
        char *result = yescrypt_crypt(password.constData(), salt.constData());
        return result ? QString(result) : QString();
    } else {
        // Use sha256crypt for older OS releases
        QByteArray salt = "$5$";
        for (int i=0; i<10; i++)
            salt += saltchars[uid(gen)];
        
        return sha256_crypt(password.constData(), salt.constData());
    }
}

QString ImageWriter::pbkdf2(const QByteArray &psk, const QByteArray &ssid)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, psk, ssid, 4096, 32).toHex();
}

void ImageWriter::setSavedCustomisationSettings(const QVariantMap &map)
{
    _settings.beginGroup("imagecustomization");
    _settings.remove("");
    const QStringList keys = map.keys();
    for (const QString &key : keys) {
        _settings.setValue(key, map.value(key));
    }
    _settings.endGroup();
    _settings.sync();
}

QVariantMap ImageWriter::getSavedCustomisationSettings()
{
    QVariantMap result;

    _settings.beginGroup("imagecustomization");
    const QStringList keys = _settings.childKeys();
    for (const QString &key : keys) {
        result.insert(key, _settings.value(key));
    }
    _settings.endGroup();

    // Belt-and-braces: deduplicate SSH authorized keys if present.
    // Many UIs allow pasting multiple keys; ensure unique entries using a
    // collision-resistant hash (SHA-256) over each normalized line.
    if (result.contains("sshAuthorizedKeys")) {
        const QString raw = result.value("sshAuthorizedKeys").toString();
        const QStringList lines = raw.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
        QStringList uniqueOrdered;
        QList<QByteArray> seenHashes;
        uniqueOrdered.reserve(lines.size());

        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;
            const QByteArray hash = QCryptographicHash::hash(trimmed.toUtf8(), QCryptographicHash::Sha256);
            bool already = false;
            for (const QByteArray &h : seenHashes) { if (h == hash) { already = true; break; } }
            if (!already) {
                seenHashes.append(hash);
                uniqueOrdered.append(trimmed);
            }
        }

        result.insert("sshAuthorizedKeys", uniqueOrdered.join("\n"));
    }

    return result;
}

void ImageWriter::setPersistedCustomisationSetting(const QString &key, const QVariant &value)
{
    _settings.beginGroup("imagecustomization");
    _settings.setValue(key, value);
    _settings.endGroup();
    _settings.sync();
}

void ImageWriter::removePersistedCustomisationSetting(const QString &key)
{
    _settings.beginGroup("imagecustomization");
    _settings.remove(key);
    _settings.endGroup();
    _settings.sync();
}

bool ImageWriter::imageSupportsCustomization()
{
    return !_initFormat.isEmpty();
}

bool ImageWriter::imageSupportsCcRpi()
{
    return _initFormat == "cloudinit-rpi";
}

QStringList ImageWriter::getTranslations()
{
    QStringList t = _translations.keys();
    t.sort(Qt::CaseInsensitive);
    return t;
}

QString ImageWriter::getCurrentLanguage()
{
    return _currentLang;
}

QString ImageWriter::getCurrentKeyboard()
{
    return _currentKeyboard;
}

void ImageWriter::changeLanguage(const QString &newLanguageName)
{
    if (newLanguageName.isEmpty() || newLanguageName == _currentLang || !_translations.contains(newLanguageName))
        return;

    QString langcode = _translations[newLanguageName];
    qDebug() << "Changing language to" << langcode;

    QTranslator *trans = new QTranslator();
    if (trans->load(":/i18n/rpi-imager_"+langcode+".qm"))
    {
        replaceTranslator(trans);
        _currentLang = newLanguageName;
        _currentLangcode = langcode;
    }
    else
    {
        qDebug() << "Failed to load translation file";
        delete trans;
    }
}

void ImageWriter::changeKeyboard(const QString &newKeymapLayout)
{
    if (newKeymapLayout.isEmpty() || newKeymapLayout == _currentKeyboard)
        return;


    _currentKeyboard = newKeymapLayout;
}

void ImageWriter::replaceTranslator(QTranslator *trans)
{
    if (_trans)
    {
        QCoreApplication::removeTranslator(_trans);
        delete _trans;
    }

    _trans = trans;
    QCoreApplication::installTranslator(_trans);

#ifndef CLI_ONLY_BUILD
    if (_engine)
    {
        _engine->retranslate();
    }
#endif

    // Regenerate the OS list, because it has some localised items
    emit osListPrepared();
}

QString ImageWriter::detectPiKeyboard()
{
    unsigned int typenr = 0;
    QFile f("/proc/device-tree/chosen/rpi-country-code");
    if (f.exists() && f.open(f.ReadOnly))
    {
        QByteArray d = f.readAll();
        f.close();

        if (d.length() == 4)
        {
            typenr = d.at(2);
        }
    }

    if (!typenr)
    {
        QDir dir("/dev/input/by-id");
        static QRegularExpression rx("RPI_Wired_Keyboard_([0-9]+)");

        const QStringList entries = dir.entryList(QDir::Files);
        for (const QString &fn : entries)
        {
            QRegularExpressionMatch match = rx.match(fn);
            if (match.hasMatch())
            {
                typenr = match.captured(1).toUInt();
            }
        }
    }

    if (typenr)
    {
        QStringList kbcountries = {
            "",
            "gb",
            "fr",
            "es",
            "us",
            "de",
            "it",
            "jp",
            "pt",
            "no",
            "se",
            "dk",
            "ru",
            "tr",
            "il"
        };

        if (typenr < kbcountries.count())
        {
            return kbcountries.at(typenr);
        }
    }

    return QString();
}

QString ImageWriter::getCurrentUser()
{
    QString user = qgetenv("USER");

    if (user.isEmpty())
        user = qgetenv("USERNAME");

    user = user.toLower();
    if (user.contains(" "))
    {
        auto names = user.split(" ");
        user = names.first();
    }

    if (user.isEmpty() || user == "root")
        user = "pi";

    return user;
}

bool ImageWriter::hasMouse()
{
    return !isEmbeddedMode() || QFile::exists("/dev/input/mouse0");
}

bool ImageWriter::isScreenReaderActive() const
{
#ifndef CLI_ONLY_BUILD
    return QAccessible::isActive();
#else
    return false;
#endif
}

bool ImageWriter::customRepo()
{
    return _repo.toString() != OSLIST_URL;
}

// Cache-related methods removed - now handled by CacheManager

void ImageWriter::onCacheVerificationProgress(qint64 bytesProcessed, qint64 totalBytes)
{
    if (_waitingForCacheVerification) {
        // Show cache verification progress as "verify" progress
        // This reuses the existing verify progress UI
        emit verifyProgress(bytesProcessed, totalBytes);
        // The progress text will be updated by the QML onVerifyProgress handler
        // which shows "Verifying... X%" - this is perfect for cache verification too
    }
}

void ImageWriter::onCacheVerificationComplete(bool isValid)
{
    if (!_waitingForCacheVerification) {
        return; // Not waiting for this verification
    }

    // Record cache verification duration for performance tracking
    quint32 verificationDurationMs = static_cast<quint32>(_cacheVerificationTimer.elapsed());
    _performanceStats->recordEvent(PerformanceStats::EventType::CacheVerification,
        verificationDurationMs, isValid,
        isValid ? "valid" : "invalid");

    _waitingForCacheVerification = false;

    // Disconnect signals to avoid receiving future notifications
    disconnect(_cacheManager, &CacheManager::cacheVerificationProgress,
               this, &ImageWriter::onCacheVerificationProgress);
    disconnect(_cacheManager, &CacheManager::cacheVerificationComplete,
               this, &ImageWriter::onCacheVerificationComplete);

    qDebug() << "Cache verification completed - valid:" << isValid << "duration:" << verificationDurationMs << "ms";

    // Emit signal to update UI (cache verification finished)
    emit cacheVerificationFinished();

    // Continue with the rest of startWrite() logic
    _continueStartWriteAfterCacheVerification(isValid);
}

void ImageWriter::onSelectedDeviceRemoved(const QString &device)
{
    qDebug() << "Device removal detected:" << device << "Current selected:" << _dst << "State:" << static_cast<int>(_writeState);

    // Only react if this is the device we currently have selected
    if (!_dst.isEmpty() && _dst == device) {
        qDebug() << "Selected device" << device << "was removed - invalidating selection";
        _selectedDeviceValid = false;

        // If we're currently writing to this device, cancel the write immediately
        if (_writeState == WriteState::Preparing || _writeState == WriteState::Writing || _writeState == WriteState::Verifying || _writeState == WriteState::Finalizing) {
            _selectedDeviceValid = false;
            qDebug() << "Cancelling write operation due to device removal";
            
            // Disconnect error signal BEFORE setting flag and cancelling
            // This prevents race where thread emits error() after we decide to show device removal message
            if (_thread) {
                disconnect(_thread, SIGNAL(error(QString)), this, SLOT(onError(QString)));
            }
            
            _cancelledDueToDeviceRemoval = true;
            cancelWrite();
            // Will show specific error message in onCancelled()
        } else if (_writeState == WriteState::Succeeded) {
            // Write completed successfully and now drive was ejected - this is normal
            qDebug() << "Device removed after successful write - ignoring (likely ejected)";
            // Don't notify UI - this is expected behavior after successful write
        } else if (_writeState == WriteState::Failed || _writeState == WriteState::Cancelled) {
            // Write already failed or was cancelled - error message already shown
            qDebug() << "Device removed but write already failed/cancelled - suppressing duplicate notification";
            // Don't notify UI - error already displayed
        } else {
            // Idle state - device removed when not writing
            emit selectedDeviceRemoved();
        }
    }
}

void ImageWriter::_continueStartWriteAfterCacheVerification(bool cacheIsValid)
{
    QString urlstr = _src.toString(_src.FullyEncoded);

    if (cacheIsValid) {
        QString cacheFilePath = _cacheManager->getCacheFilePath(_expectedHash);
        qDebug() << "Using verified cache file:" << cacheFilePath;
        urlstr = QUrl::fromLocalFile(cacheFilePath).toString(_src.FullyEncoded);
    } else {
        qDebug() << "Cache file invalid, invalidating and using original URL";
        _cacheManager->invalidateCache();
    }

    // Proactive validation for local sources before spawning threads
    if (QUrl(urlstr).isLocalFile())
    {
        const QString localPath = QUrl(urlstr).toLocalFile();
        QFileInfo localFi(localPath);
        if (!localFi.exists())
        {
            onError(tr("Source file not found: %1").arg(localPath));
            return;
        }
        if (!localFi.isFile())
        {
            onError(tr("Source is not a regular file: %1").arg(localPath));
            return;
        }
        if (!localFi.isReadable())
        {
            onError(tr("Source file is not readable: %1").arg(localPath));
            return;
        }

        // Continue with the write operation (this is the code that was after cache verification)
        _thread = new LocalFileExtractThread(urlstr.toLatin1(), _dst.toLatin1(), _expectedHash, this);
    }
    else
    {
        _thread = new DownloadExtractThread(urlstr.toLatin1(), _dst.toLatin1(), _expectedHash, this);
        if (_repo.toString() == OSLIST_URL)
        {
            DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr.toLatin1(), _parentCategory.toLatin1(), _osName.toLatin1(), isEmbeddedMode(), _currentLangcode, this);
            connect(tele, SIGNAL(finished()), tele, SLOT(deleteLater()));
            tele->start();
        }
    }

    connect(_thread, SIGNAL(success()), SLOT(onSuccess()));
    connect(_thread, SIGNAL(error(QString)), SLOT(onError(QString)));
    connect(_thread, SIGNAL(finalizing()), SLOT(onFinalizing()));
    connect(_thread, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
    // Ensure cleanup of thread pointer on finish in all paths
    connect(_thread, &QThread::finished, this, [this]() {
        if (_thread)
        {
            _thread->deleteLater();
            _thread = nullptr;
        }
    });

    // Connect to progress signals if this is a DownloadExtractThread
    DownloadExtractThread *downloadThread = qobject_cast<DownloadExtractThread*>(_thread);
    if (downloadThread) {
        connect(downloadThread, &DownloadExtractThread::downloadProgressChanged,
                this, &ImageWriter::downloadProgress);
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, &ImageWriter::verifyProgress);
        
        // Capture progress for performance stats (lightweight - just stores raw samples)
        connect(downloadThread, &DownloadExtractThread::downloadProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordDownloadProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::decompressProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordDecompressProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::writeProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordWriteProgress(now, total);
                });
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordVerifyProgress(now, total);
                });
        
        // Also transition state to Verifying when verify progress first arrives
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 /*now*/, quint64 /*total*/){
                    if (_writeState != WriteState::Verifying && _writeState != WriteState::Finalizing && _writeState != WriteState::Succeeded)
                        setWriteState(WriteState::Verifying);
                });
        
        // Capture ring buffer stall events for time-series correlation
        connect(downloadThread, &DownloadExtractThread::eventRingBufferStats,
                this, [this](qint64 timestampMs, quint32 durationMs, QString metadata){
                    // Record as an event with explicit startMs from the stall timestamp
                    PerformanceStats::TimedEvent event;
                    event.type = PerformanceStats::EventType::RingBufferStarvation;
                    event.startMs = static_cast<uint32_t>(timestampMs);
                    event.durationMs = durationMs;
                    event.metadata = metadata;
                    event.success = true;
                    event.bytesTransferred = 0;
                    _performanceStats->addEvent(event);
                });
        
        // Pipeline timing summary events (emitted at end of extraction)
        connect(downloadThread, &DownloadExtractThread::eventPipelineDecompressionTime,
                this, [this](quint32 totalMs, quint64 bytesDecompressed){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineDecompressionTime,
                        totalMs, bytesDecompressed, true,
                        QString("bytes: %1 MB").arg(bytesDecompressed / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventPipelineWriteWaitTime,
                this, [this](quint32 totalMs, quint64 bytesWritten){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineWriteWaitTime,
                        totalMs, bytesWritten, true,
                        QString("bytes: %1 MB").arg(bytesWritten / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventPipelineRingBufferWaitTime,
                this, [this](quint32 totalMs, quint64 bytesRead){
                    _performanceStats->recordTransferEvent(
                        PerformanceStats::EventType::PipelineRingBufferWaitTime,
                        totalMs, bytesRead, true,
                        QString("bytes: %1 MB").arg(bytesRead / (1024*1024)));
                });
        connect(downloadThread, &DownloadExtractThread::eventWriteRingBufferStats,
                this, [this](quint64 producerStalls, quint64 consumerStalls, 
                             quint64 producerWaitMs, quint64 consumerWaitMs){
                    QString metadata = QString("producer_stalls: %1 (%2 ms); consumer_stalls: %3 (%4 ms)")
                        .arg(producerStalls).arg(producerWaitMs)
                        .arg(consumerStalls).arg(consumerWaitMs);
                    // Use combined wait time as duration for the event
                    quint32 totalWaitMs = static_cast<quint32>(producerWaitMs + consumerWaitMs);
                    _performanceStats->recordEvent(
                        PerformanceStats::EventType::WriteRingBufferStats,
                        totalWaitMs, true, metadata);
                });
    }
    
    // Connect performance event signals from DownloadThread
    connect(_thread, &DownloadThread::eventDriveUnmount,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveUnmount, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveUnmountVolumes,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveUnmountVolumes, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveDiskClean,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveDiskClean, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveRescan,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveRescan, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveOpen,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveOpen, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventDirectIOAttempt,
            this, [this](bool attempted, bool succeeded, bool currentlyEnabled, int errorCode, QString errorMessage){
                QString metadata = QString("attempted: %1; succeeded: %2; currently_enabled: %3; error_code: %4; error: %5")
                    .arg(attempted ? "yes" : "no")
                    .arg(succeeded ? "yes" : "no")
                    .arg(currentlyEnabled ? "yes" : "no")
                    .arg(errorCode)
                    .arg(errorMessage.isEmpty() ? "none" : errorMessage);
                _performanceStats->recordEvent(PerformanceStats::EventType::DirectIOAttempt, 0, currentlyEnabled, metadata);
            });
    connect(_thread, &DownloadThread::eventCustomisation,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::Customisation, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventFinalSync,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::FinalSync, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventVerify,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::HashComputation, durationMs, success, "Post-write verification");
            });
    connect(_thread, &DownloadThread::eventPeriodicSync,
            this, [this](quint32 durationMs, bool success, quint64 bytesWritten){
                QString metadata = QString("at %1 MB").arg(bytesWritten / (1024 * 1024));
                _performanceStats->recordEvent(PerformanceStats::EventType::PageCacheFlush, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventImageExtraction,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::ImageExtraction, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventPartitionTableWrite,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::PartitionTableWrite, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventFatPartitionSetup,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::FatPartitionSetup, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDeviceClose,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DeviceClose, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventNetworkRetry,
            this, [this](quint32 sleepMs, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkRetry, sleepMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventNetworkConnectionStats,
            this, [this](QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkConnectionStats, 0, true, metadata);
            });

    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(staticVersion()).toUtf8());
    _thread->setImageCustomisation(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);
#ifdef Q_OS_DARWIN
    // Pass cached child devices to avoid re-scanning during unmount (saves ~1 second on macOS)
    // Always call setChildDevices (even with empty list) so we skip the expensive scan
    _thread->setChildDevices(_dstChildDevices);
#endif

    // Handle caching setup for downloads using CacheManager
    // Only set up caching when we're downloading (not using cached file as source)
    if (!_expectedHash.isEmpty() && !cacheIsValid)
    {
        // Use CacheManager to setup cache for download
        QString cacheFilePath;
        if (_cacheManager->setupCacheForDownload(_expectedHash, _downloadLen, cacheFilePath))
        {
            qDebug() << "Setting up cache file for download:" << cacheFilePath;
            _thread->setCacheFile(cacheFilePath, _downloadLen);
            // Connect to CacheManager for cache updates (pass both hashes correctly)
            connect(_thread, &DownloadThread::cacheFileHashUpdated,
                    this, [this](const QByteArray& cacheFileHash, const QByteArray& imageHash) {
                        qDebug() << "DownloadThread cache update - cacheFileHash:" << cacheFileHash << "imageHash:" << imageHash;
                        // Update cache with both uncompressed hash (imageHash) and compressed hash (cacheFileHash)
                        _cacheManager->updateCacheFile(imageHash, cacheFileHash);
                    });
        }
        else
        {
            qDebug() << "Cache setup failed or disabled - proceeding without caching";
        }
    }
    else if (cacheIsValid)
    {
        qDebug() << "Using cached file as source - skipping cache setup";
    }

    // Start the actual write operation
    if (_multipleFilesInZip)
    {
        static_cast<DownloadExtractThread *>(_thread)->enableMultipleFileExtraction();
        DriveFormatThread *dft = new DriveFormatThread(_dst.toLatin1(), this);
        connect(dft, SIGNAL(success()), _thread, SLOT(start()));
        connect(dft, SIGNAL(error(QString)), SLOT(onError(QString)));
        connect(dft, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
        connect(dft, &DriveFormatThread::eventDriveFormat,
                this, [this](quint32 durationMs, bool success){
                    _performanceStats->recordEvent(PerformanceStats::EventType::DriveFormat, durationMs, success);
                });
        dft->start();
        setWriteState(WriteState::Writing);
    }
    else
    {
        _thread->start();
        setWriteState(WriteState::Writing);
    }

    startProgressPolling();
}

void MountUtilsLog(std::string msg) {
    Q_UNUSED(msg)
    //qDebug() << "mountutils:" << msg.c_str();
}

void ImageWriter::reboot()
{
    qDebug() << "Rebooting system.";
    (void)system("reboot");
}

void ImageWriter::openUrl(const QUrl &url)
{
    qDebug() << "Opening URL:" << url.toString();

    bool success = false;

#ifdef Q_OS_LINUX
    // Use xdg-open on Linux (including AppImage environments)
    // When running with elevated privileges (sudo/pkexec), we need to run xdg-open
    // as the original user, not as root, so it can access the user's desktop session
    if (::geteuid() == 0) {
        // Running as root - use pkexec to run xdg-open as the original user
        // Determine the original user's UID and username
        uid_t targetUid = 0;
        QString targetUsername;
        
        // Check if we were invoked via pkexec or sudo
        const char* pkexecUid = ::getenv("PKEXEC_UID");
        const char* sudoUid = ::getenv("SUDO_UID");
        
        if (pkexecUid) {
            // Running under pkexec
            targetUid = static_cast<uid_t>(::atoi(pkexecUid));
        } else if (sudoUid) {
            // Running under sudo
            targetUid = static_cast<uid_t>(::atoi(sudoUid));
        } else if (::getuid() != ::geteuid()) {
            // Running under sudo but SUDO_UID not set - use real UID
            targetUid = ::getuid();
        }
        
        if (targetUid != 0) {
            // Look up the username for this UID
            struct passwd* pw = ::getpwuid(targetUid);
            if (pw && pw->pw_name) {
                targetUsername = QString::fromUtf8(pw->pw_name);
            }
        }
        
        if (!targetUsername.isEmpty()) {
            // Use runuser to run xdg-open as the original user
            // runuser is better than pkexec --user because it preserves environment variables
            // and doesn't require authentication when already running as root
            // We need to pass the D-Bus session bus address and other environment variables
            // so xdg-open can connect to the user's desktop session
            
            // Get environment variables needed for xdg-open to work
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            
            // Preserve critical environment variables for D-Bus and display
            QString dbusSessionAddress = env.value("DBUS_SESSION_BUS_ADDRESS");
            QString xdgRuntimeDir = env.value("XDG_RUNTIME_DIR");
            QString display = env.value("DISPLAY");
            QString waylandDisplay = env.value("WAYLAND_DISPLAY");
            QString xauthority = env.value("XAUTHORITY");
            
            // Build environment for the child process
            QProcessEnvironment childEnv;
            if (!dbusSessionAddress.isEmpty()) {
                childEnv.insert("DBUS_SESSION_BUS_ADDRESS", dbusSessionAddress);
            }
            if (!xdgRuntimeDir.isEmpty()) {
                childEnv.insert("XDG_RUNTIME_DIR", xdgRuntimeDir);
            }
            if (!display.isEmpty()) {
                childEnv.insert("DISPLAY", display);
            }
            if (!waylandDisplay.isEmpty()) {
                childEnv.insert("WAYLAND_DISPLAY", waylandDisplay);
            }
            if (!xauthority.isEmpty()) {
                childEnv.insert("XAUTHORITY", xauthority);
            }
            
            // Use runuser with environment variables passed via env command
            // Format: runuser -u <username> -- env VAR=value ... xdg-open <url>
            QStringList runuserArgs;
            runuserArgs << "-u" << targetUsername << "--";
            
            // Build env command with all necessary variables
            QStringList envArgs;
            envArgs << "env";
            if (!dbusSessionAddress.isEmpty()) {
                envArgs << QString("DBUS_SESSION_BUS_ADDRESS=%1").arg(dbusSessionAddress);
            }
            if (!xdgRuntimeDir.isEmpty()) {
                envArgs << QString("XDG_RUNTIME_DIR=%1").arg(xdgRuntimeDir);
            }
            if (!display.isEmpty()) {
                envArgs << QString("DISPLAY=%1").arg(display);
            }
            if (!waylandDisplay.isEmpty()) {
                envArgs << QString("WAYLAND_DISPLAY=%1").arg(waylandDisplay);
            }
            if (!xauthority.isEmpty()) {
                envArgs << QString("XAUTHORITY=%1").arg(xauthority);
            }
            envArgs << "xdg-open" << url.toString();
            
            runuserArgs << envArgs;
            
            success = PlatformQuirks::launchDetached("runuser", runuserArgs);
            if (!success) {
                qWarning() << "Failed to start runuser xdg-open process, falling back to pkexec";
                // Fallback to pkexec if runuser fails (might not be available on all systems)
                QStringList pkexecArgs;
                pkexecArgs << "--user" << targetUsername << "xdg-open" << url.toString();
                success = PlatformQuirks::launchDetached("pkexec", pkexecArgs);
                if (!success) {
                    qWarning() << "Failed to start pkexec xdg-open process";
                } else {
                    qDebug() << "Started pkexec xdg-open";
                }
            } else {
                qDebug() << "Started runuser xdg-open";
            }
        } else {
            qWarning() << "Could not determine original user for xdg-open";
            success = false;
        }
    } else {
        // Not running as root - launch detached to avoid blocking
        success = PlatformQuirks::launchDetached("xdg-open", QStringList() << url.toString());
        if (!success) {
            qWarning() << "Failed to start xdg-open process";
        } else {
            qDebug() << "Started xdg-open";
        }
    }
#elif defined(Q_OS_DARWIN)
    // Use open on macOS
    success = PlatformQuirks::launchDetached("open", QStringList() << url.toString());
    if (!success) {
        qWarning() << "Failed to start open process";
    } else {
        qDebug() << "Started open";
    }
#elif defined(Q_OS_WIN)
    // Use start on Windows
    success = PlatformQuirks::launchDetached("cmd", QStringList() << "/c" << "start" << url.toString());
    if (!success) {
        qWarning() << "Failed to start cmd /c start process";
    } else {
        qDebug() << "Started cmd /c start";
    }
#endif

    // Fallback to Qt's method if platform command failed or platform is unsupported
    if (!success) {
#ifndef CLI_ONLY_BUILD
        qDebug() << "Falling back to QDesktopServices::openUrl";
        QDesktopServices::openUrl(url);
#else
        qWarning() << "Unable to open URL in CLI mode:" << url.toString();
#endif
    }
}

bool ImageWriter::verifyAuthKey(const QString &s, bool strict) const
{
    // Base58 (no 0 O I l)
    static const QRegularExpression base58OnlyRe(QStringLiteral("^[1-9A-HJ-NP-Za-km-z]+$"));

    // Required prefix
    bool hasPrefix = s.startsWith(QStringLiteral("rpuak_")) || s.startsWith(QStringLiteral("rpoak_"));
    if (!hasPrefix)
        return false;

    const QString payload = s.mid(6);
    bool base58Match = base58OnlyRe.match(payload).hasMatch();
    
    if (payload.isEmpty() || !base58Match)
        return false;

    if (strict) {
        // Exactly 24 Base58 chars today → total length 30
        return payload.size() == 24;
    } else {
        // Future-proof: accept >=24 Base58 chars
        return payload.size() >= 24;
    }
}

QString ImageWriter::parseTokenFromUrl(const QUrl &url, bool strictAuthKey) const {
    // Handle QUrl or string, accept auth_key
    if (!url.isValid())
        return {};

    QUrlQuery q(url);
    const QString val = q.queryItemValue(QStringLiteral("auth_key"), QUrl::FullyDecoded);
    if (!val.isEmpty()) {
        if (verifyAuthKey(val, strictAuthKey)) {
            return val;
        }

        qWarning() << "Ignoring auth_key with invalid format/length:" << val;
    }

    return {};
}

void ImageWriter::handleIncomingUrl(const QUrl &url)
{
    qDebug() << "Incoming URL:" << url;

    auto token = parseTokenFromUrl(url);
    if (!token.isEmpty()) {
        if (!_piConnectToken.isEmpty()) {
            if (_piConnectToken != token) {
                // Let QML decide whether to overwrite
                emit connectTokenConflictDetected(token);
            }

            return;
        }

        overwriteConnectToken(token);
    }
}

void ImageWriter::overwriteConnectToken(const QString &token)
{
    // Ephemeral session-only Connect token (never persisted)
    _piConnectToken = token;
    emit connectTokenReceived(token);
    
    // Bring the window to the foreground on Windows when token is received
    bringWindowToForeground();
}

QString ImageWriter::getRuntimeConnectToken() const
{
    return _piConnectToken;
}

void ImageWriter::clearConnectToken()
{
    _piConnectToken.clear();
    emit connectTokenCleared();
}

bool ImageWriter::isElevatableBundle()
{
    return PlatformQuirks::isElevatableBundle();
}

bool ImageWriter::hasElevationPolicyInstalled()
{
    return PlatformQuirks::hasElevationPolicyInstalled();
}

bool ImageWriter::installElevationPolicy()
{
    return PlatformQuirks::runElevatedPolicyInstaller();
}

void ImageWriter::restartWithElevatedPrivileges()
{
    // Pass through --log-file if present
    QStringList extraArgs;
    QStringList appArgs = QCoreApplication::arguments();
    for (int i = 0; i < appArgs.size(); i++) {
        if (appArgs[i] == "--log-file" && i + 1 < appArgs.size()) {
            extraArgs << "--log-file" << appArgs[i + 1];
            break;
        }
    }
    
    PlatformQuirks::execElevated(extraArgs);
}

bool ImageWriter::hasPerformanceData()
{
    return _performanceStats->hasData();
}

bool ImageWriter::exportPerformanceData()
{
#ifndef CLI_ONLY_BUILD
    if (!_performanceStats->hasData()) {
        qDebug() << "No performance data to export";
        return false;
    }
    
    // Generate default filename with timestamp
    QString defaultFilename = QString("rpi-imager-performance-%1.json")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    
    // Get save location from user using native file dialog
    QString initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString filePath = NativeFileDialog::getSaveFileName(
        tr("Save Performance Data"),
        initialDir + "/" + defaultFilename,
        tr("JSON files (*.json);;All files (*)"),
        _mainWindow
    );
    
    // Record file dialog timing as a performance event
    NativeFileDialog::TimingInfo dialogTiming = NativeFileDialog::lastTimingInfo();
    _performanceStats->recordEvent(
        PerformanceStats::EventType::FileDialogOpen,
        static_cast<uint32_t>(dialogTiming.totalBeforeShowMs + dialogTiming.userInteractionMs),
        !filePath.isEmpty(),
        fileDialogTimingToString(dialogTiming)
    );
    
    if (filePath.isEmpty()) {
        qDebug() << "Performance data export cancelled by user";
        return false;
    }
    
    // Ensure .json extension
    if (!filePath.endsWith(".json", Qt::CaseInsensitive)) {
        filePath += ".json";
    }
    
    // Export data - all complex processing happens here, triggered by user action
    bool success = _performanceStats->exportToFile(filePath);
    if (success) {
        qDebug() << "Performance data exported to:" << filePath;
    }
    return success;
#else
    qDebug() << "Performance data export not available in CLI build";
    return false;
#endif
}
