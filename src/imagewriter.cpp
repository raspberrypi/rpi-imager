/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "imagewriter.h"
#include "writeprogresswatchdog.h"
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
#include "systemmemorymanager.h"
#include "downloadstatstelemetry.h"
#include "wlancredentials.h"
#include "device_info.h"
#include "platformquirks.h"
#ifndef CLI_ONLY_BUILD
#include "iconimageprovider.h"
#include "iconmultifetcher.h"
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
#include <QDateTime>
#include "curlfetcher.h"
#include "curlnetworkconfig.h"
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
#include <new>
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
      _verifyEnabled(true), _multipleFilesInZip(false), _online(false), _extractSizeKnown(true),
      _settings(),
      _translations(),
      _trans(nullptr),
      _refreshIntervalOverrideMinutes(-1),
      _refreshJitterOverrideMinutes(-1),
#ifndef CLI_ONLY_BUILD
      _piConnectToken(),
      _mainWindow(nullptr),
#else
      _piConnectToken(),
#endif
      _progressWatchdog(nullptr),
      _forceSyncMode(false)
{
    // Initialise CacheManager
    _cacheManager = new CacheManager(this);
    
    // Initialise PerformanceStats
    _performanceStats = new PerformanceStats(this);
    
    // Initialize debug options with defaults
    // Direct I/O is enabled by default (matches current behavior)
    // Periodic sync is enabled by default but skipped when direct I/O is active
    _debugDirectIO = true;
    _debugPeriodicSync = true;
    _debugVerboseLogging = false;
    _debugAsyncIO = true;       // Async I/O enabled by default for performance
    _debugIPv4Only = false;     // Use both IPv4 and IPv6 by default
    _debugSkipEndOfDevice = false; // Normal behavior; enable for counterfeit cards
    
    // Calculate optimal async queue depth based on system memory
    _debugAsyncQueueDepth = SystemMemoryManager::instance().getOptimalAsyncQueueDepth();
    
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
    // Stop network monitoring first - the callback captures 'this' pointer
    // Must be done before any other cleanup to prevent use-after-free
    qDebug() << "Stopping network monitoring";
    PlatformQuirks::stopNetworkMonitoring();
    
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
    // If extract size is provided from manifest, we can trust it; otherwise assume known until proven otherwise
    _extractSizeKnown = true;
    qDebug() << "setSrc: initFormat parameter:" << initFormat << "-> _initFormat set to:" << _initFormat;

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
        else if (lowercaseurl.endsWith(".gz"))
            _parseGzFile();
        else
            _parseCompressedFile();
    }

    if (_devLen && _extrLen > _devLen)
    {
        emit error(tr("Storage capacity is not large enough.\n\n"
                      "The image requires at least %1 of storage.")
                   .arg(formatSize(_extrLen)));
        return;
    }

    if (_extrLen && !_multipleFilesInZip && _extrLen % 512 != 0)
    {
        emit error(tr("Input file is not a valid disk image.\n\n"
                      "File size %1 bytes is not a multiple of 512 bytes.")
                   .arg(_extrLen));
        return;
    }

    // Start performance stats session early so cache lookup is captured
    // Use platform-specific write device path (e.g., rdisk on macOS for direct I/O)
    QString writeDevicePath = PlatformQuirks::getWriteDevicePath(_dst);
    _performanceStats->startSession(_osName.isEmpty() ? _src.fileName() : _osName, 
                                    _extrLen > 0 ? _extrLen : _downloadLen, 
                                    writeDevicePath);

    // Populate system info for performance analysis
    {
        PerformanceStats::SystemInfo sysInfo;
        auto &memMgr = SystemMemoryManager::instance();
        
        // Memory info
        sysInfo.totalMemoryBytes = static_cast<quint64>(memMgr.getTotalMemoryMB()) * 1024 * 1024;
        sysInfo.availableMemoryBytes = static_cast<quint64>(memMgr.getAvailableMemoryMB()) * 1024 * 1024;
        
        // Device info - use platform-specific write device path
        sysInfo.devicePath = writeDevicePath;
        sysInfo.deviceSizeBytes = _devLen;
        sysInfo.deviceDescription = "";  // Would need DriveListItem lookup
        sysInfo.deviceIsUsb = true;      // Assume USB for now
        sysInfo.deviceIsRemovable = true;
        
        // Platform info
#ifdef Q_OS_MACOS
        sysInfo.osName = "macOS";
#elif defined(Q_OS_LINUX)
        sysInfo.osName = "Linux";
#elif defined(Q_OS_WIN)
        sysInfo.osName = "Windows";
#else
        sysInfo.osName = "Unknown";
#endif
        sysInfo.osVersion = QSysInfo::productVersion();
        sysInfo.cpuArchitecture = QSysInfo::currentCpuArchitecture();
        sysInfo.cpuCoreCount = QThread::idealThreadCount();
        
        // Imager version
        sysInfo.imagerVersion = IMAGER_VERSION_STR;
#ifdef QT_DEBUG
        sysInfo.imagerBuildType = "Debug";
#else
        sysInfo.imagerBuildType = "Release";
#endif
        sysInfo.qtVersion = qVersion();
        sysInfo.qtBuildVersion = QT_VERSION_STR;
        
        // Write configuration - not known yet, will be set later when file is opened
        // These will be set via the DirectIOAttempt event metadata
        sysInfo.directIOEnabled = false;  // Default, actual value set when file opens
        sysInfo.periodicSyncEnabled = true;  // Default
        
        auto syncConfig = memMgr.calculateSyncConfiguration();
        sysInfo.syncIntervalBytes = syncConfig.syncIntervalBytes;
        sysInfo.syncIntervalMs = syncConfig.syncIntervalMs;
        sysInfo.memoryTier = syncConfig.memoryTier;
        
        // Buffer configuration
        sysInfo.writeBufferSize = memMgr.getOptimalWriteBufferSize();
        sysInfo.inputBufferSize = memMgr.getOptimalInputBufferSize();
        sysInfo.inputRingBufferSlots = memMgr.getOptimalRingBufferSlots(sysInfo.inputBufferSize);
        // Write ring buffer is dynamically sized based on optimal queue depth
        // Report the max configurable depth (512) + headroom for logging purposes
        // Actual allocation may be smaller based on available memory
        sysInfo.writeRingBufferSlots = 512 + 4;
        
        _performanceStats->setSystemInfo(sysInfo);
    }

    // Time cache lookup for performance tracking
    // Use hasPotentialCache() which doesn't require verification to be complete
    // This allows us to start verification for cached files that haven't been verified yet
    QElapsedTimer cacheLookupTimer;
    cacheLookupTimer.start();
    bool potentialCacheHit = !_expectedHash.isEmpty() && _cacheManager->hasPotentialCache(_expectedHash);
    _performanceStats->recordEvent(PerformanceStats::EventType::CacheLookup,
        static_cast<quint32>(cacheLookupTimer.elapsed()), true,
        potentialCacheHit ? "potential_hit" : (_expectedHash.isEmpty() ? "no_hash" : "miss"));
    
    if (potentialCacheHit)
    {
        // Use background cache manager to check cache file integrity
        CacheManager::CacheStatus cacheStatus = _cacheManager->getCacheStatus();
        qDebug() << "Cache status: verificationComplete=" << cacheStatus.verificationComplete
                 << "isValid=" << cacheStatus.isValid
                 << "file=" << cacheStatus.cacheFileName;

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

    try {
        if (QUrl(urlstr).isLocalFile())
        {
            _thread = new LocalFileExtractThread(urlstr, writeDevicePath.toLatin1(), _expectedHash, this);
        }
        else
        {
            _thread = new DownloadExtractThread(urlstr, writeDevicePath.toLatin1(), _expectedHash, this);
            if (_repo.toString() == OSLIST_URL)
            {
                DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr, _parentCategory.toLatin1(), _osName.toLatin1(), isEmbeddedMode(), _currentLangcode, this);
                connect(tele, SIGNAL(finished()), tele, SLOT(deleteLater()));
                tele->start();
            }
        }
    } catch (const std::bad_alloc& e) {
        // Memory allocation failed during thread/buffer creation
        qDebug() << "Memory allocation failed during write setup:" << e.what();
        
        // Log the failure to performance stats
        _performanceStats->recordEvent(
            PerformanceStats::EventType::MemoryAllocationFailure,
            0,  // No duration - immediate failure
            false,
            QString("Failed to allocate memory for write operation: %1").arg(e.what())
        );
        
        // Provide a clear, actionable error message to the user
        QString errorMsg = tr("Failed to start write operation: insufficient memory.\n\n"
                              "The system does not have enough available memory to perform this operation. "
                              "Try closing other applications to free up memory, then try again.\n\n"
                              "Technical details: %1").arg(e.what());
        
        setWriteState(WriteState::Failed);
        _performanceStats->endSession(false, "Memory allocation failure");
        emit error(errorMsg);
        return;
    } catch (const std::exception& e) {
        // Other exception during thread creation
        qDebug() << "Exception during write setup:" << e.what();
        
        _performanceStats->recordEvent(
            PerformanceStats::EventType::MemoryAllocationFailure,
            0,
            false,
            QString("Exception during write setup: %1").arg(e.what())
        );
        
        setWriteState(WriteState::Failed);
        _performanceStats->endSession(false, QString("Setup exception: %1").arg(e.what()));
        emit error(tr("Failed to start write operation: %1").arg(e.what()));
        return;
    }

    // Set the extract size for accurate write progress (compressed images have larger extracted size)
    _thread->setExtractTotal(_extrLen > 0 ? _extrLen : _downloadLen);

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
        connect(downloadThread, &DownloadExtractThread::writeProgressChanged,
                this, &ImageWriter::writeProgress);
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, &ImageWriter::verifyProgress);
        
        // Connect async write progress signal for event-driven UI updates during WaitForPendingWrites
        // This signal is emitted from IOCP completion callbacks, providing real-time progress
        connect(downloadThread, &DownloadThread::asyncWriteProgress,
                this, &ImageWriter::writeProgress, Qt::QueuedConnection);
        
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
        connect(downloadThread, &DownloadThread::asyncWriteProgress,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordWriteProgress(now, total);
                }, Qt::QueuedConnection);
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
    connect(_thread, &DownloadThread::eventDriveAuthorization,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveAuthorization, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveMbrZeroing,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveMbrZeroing, durationMs, success, metadata);
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
                // Update systemInfo with actual direct I/O state now that we know it
                _performanceStats->updateDirectIOEnabled(currentlyEnabled);
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
            this, [this](quint32 durationMs, bool success, QByteArray writeHash, QByteArray verifyHash){
                QString metadata = QString("Post-write verification; writeHash: %1; verifyHash: %2")
                    .arg(QString::fromLatin1(writeHash), QString::fromLatin1(verifyHash));
                _performanceStats->recordEvent(PerformanceStats::EventType::HashComputation, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventPeriodicSync,
            this, [this](quint32 durationMs, bool success, quint64 bytesWritten){
                QString metadata = QString("at %1 MB").arg(bytesWritten / (1024 * 1024));
                _performanceStats->recordEvent(PerformanceStats::EventType::PeriodicSync, durationMs, success, metadata);
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
    connect(_thread, &DownloadThread::eventDeviceIOTimeout,
            this, [this](quint32 pendingWrites, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DeviceIOTimeout, 
                    30000, false, QString("pending=%1; %2").arg(pendingWrites).arg(metadata));
            });
    connect(_thread, &DownloadThread::eventQueueDepthReduction,
            this, [this](int oldDepth, int newDepth, int pendingWrites){
                _performanceStats->recordEvent(PerformanceStats::EventType::QueueDepthReduction, 0, true,
                    QString("depth=%1->%2; pending=%3").arg(oldDepth).arg(newDepth).arg(pendingWrites));
            });
    connect(_thread, &DownloadThread::eventDrainAndHotSwap,
            this, [this](quint32 durationMs, int pendingBefore, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DrainAndHotSwap, durationMs, success,
                    QString("pending=%1").arg(pendingBefore));
            });
    connect(_thread, &DownloadThread::syncFallbackActivated,
            this, [this](QString reason){
                _performanceStats->recordEvent(PerformanceStats::EventType::SyncFallbackActivated, 0, true, reason);
                emit operationWarning(reason);
            });
    connect(_thread, &DownloadThread::requestWriteRestart,
            this, &ImageWriter::restartWrite);
    connect(_thread, &DownloadThread::eventNetworkRetry,
            this, [this](quint32 sleepMs, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkRetry, sleepMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventNetworkConnectionStats,
            this, [this](QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkConnectionStats, 0, true, metadata);
            });
    
    // Write timing breakdown signals (for detailed hypothesis testing)
    connect(_thread, &DownloadThread::eventWriteTimingBreakdown,
            this, [this](quint32 totalWriteOps, quint64 totalSyscallMs, quint64 totalPreHashWaitMs,
                         quint64 totalPostHashWaitMs, quint64 totalSyncMs, quint32 syncCount){
                QString metadata = QString("writeOps: %1; syscallMs: %2; preHashWaitMs: %3; postHashWaitMs: %4; syncMs: %5; syncCount: %6")
                    .arg(totalWriteOps).arg(totalSyscallMs).arg(totalPreHashWaitMs)
                    .arg(totalPostHashWaitMs).arg(totalSyncMs).arg(syncCount);
                // Use total time (syscall + hash waits) as duration
                quint32 totalMs = static_cast<quint32>(totalSyscallMs + totalPreHashWaitMs + totalPostHashWaitMs);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteTimingBreakdown, totalMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventWriteSizeDistribution,
            this, [this](quint32 minSizeKB, quint32 maxSizeKB, quint32 avgSizeKB, quint64 totalBytes, quint32 writeCount){
                QString metadata = QString("minKB: %1; maxKB: %2; avgKB: %3; totalBytes: %4; count: %5")
                    .arg(minSizeKB).arg(maxSizeKB).arg(avgSizeKB).arg(totalBytes).arg(writeCount);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteSizeDistribution, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventWriteAfterSyncImpact,
            this, [this](quint32 avgThroughputBeforeSyncKBps, quint32 avgThroughputAfterSyncKBps, quint32 sampleCount){
                QString metadata = QString("beforeSyncKBps: %1; afterSyncKBps: %2; samples: %3")
                    .arg(avgThroughputBeforeSyncKBps).arg(avgThroughputAfterSyncKBps).arg(sampleCount);
                // Calculate the impact percentage (positive = degradation after sync)
                int impactPercent = 0;
                if (avgThroughputBeforeSyncKBps > 0) {
                    impactPercent = static_cast<int>(100 * (static_cast<int>(avgThroughputBeforeSyncKBps) - static_cast<int>(avgThroughputAfterSyncKBps)) / static_cast<int>(avgThroughputBeforeSyncKBps));
                }
                metadata += QString("; impactPercent: %1").arg(impactPercent);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteAfterSyncImpact, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventAsyncIOConfig,
            this, [this](bool enabled, bool supported, int queueDepth, quint32 pendingAtEnd){
                QString metadata = QString("enabled: %1; supported: %2; queueDepth: %3; pendingAtEnd: %4")
                    .arg(enabled).arg(supported).arg(queueDepth).arg(pendingAtEnd);
                _performanceStats->recordEvent(PerformanceStats::EventType::AsyncIOConfig, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventAsyncIOTiming,
            this, [this](quint32 totalMs, quint64 bytesWritten, quint32 writeCount){
                QString metadata = QString("wallClockMs: %1; bytesWritten: %2 MB; writeCount: %3")
                    .arg(totalMs).arg(bytesWritten / (1024*1024)).arg(writeCount);
                _performanceStats->recordTransferEvent(
                    PerformanceStats::EventType::AsyncIOTiming,
                    totalMs, bytesWritten, true, metadata);
            });
    
    // Forward bottleneck state to QML for UI feedback
    connect(_thread, &DownloadThread::bottleneckStateChanged,
            this, [this](DownloadThread::BottleneckState state, quint32 throughputKBps){
                QString statusText;
                switch (state) {
                    case DownloadThread::BottleneckState::None:
                        statusText = "";
                        break;
                    case DownloadThread::BottleneckState::Network:
                        statusText = tr("Limited by download speed");
                        break;
                    case DownloadThread::BottleneckState::Decompression:
                        statusText = tr("Limited by decompression speed");
                        break;
                    case DownloadThread::BottleneckState::Storage:
                        statusText = tr("Limited by storage device speed");
                        break;
                    case DownloadThread::BottleneckState::Verifying:
                        statusText = tr("Verifying written data");
                        break;
                }
                emit bottleneckStatusChanged(statusText, throughputKBps);
            });

    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(staticVersion()).toUtf8());
    qDebug() << "startWrite: Passing to thread - initFormat:" << _initFormat << "cloudinit empty:" << _cloudinit.isEmpty() << "cloudinitNetwork empty:" << _cloudinitNetwork.isEmpty();
    _thread->setImageCustomisation(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);
    
    // Pass debug options to the thread
    _thread->setDebugDirectIO(_debugDirectIO);
    _thread->setDebugPeriodicSync(_debugPeriodicSync);
    _thread->setDebugVerboseLogging(_debugVerboseLogging);
    // Disable async I/O if forced to sync mode (due to previous recovery)
    _thread->setDebugAsyncIO(_debugAsyncIO && !_forceSyncMode);
    if (_forceSyncMode) {
        qDebug() << "Compatibility mode active - using synchronous I/O";
    }
    _thread->setDebugAsyncQueueDepth(_debugAsyncQueueDepth);
    _thread->setDebugIPv4Only(_debugIPv4Only);
    _thread->setDebugSkipEndOfDevice(_debugSkipEndOfDevice);

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

/* Cancel write - for user-initiated cancellation only */
void ImageWriter::cancelWrite()
{
    if (_waitingForCacheVerification)
    {
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
    
    // Clean up thread
    QObject *senderObj = sender();
    if (senderObj) {
        senderObj->deleteLater();
        if (senderObj == _thread)
        {
            _thread = nullptr;
        }
    }

    // End performance stats session
    _performanceStats->endSession(false, _cancelledDueToDeviceRemoval ? "Device removed" : "Cancelled by user");

    // If cancellation was due to device removal, emit a dedicated signal
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
    // Strip 'v' prefix if present - QVersionNumber requires strings to start with a digit
    QString serverVersion = version.startsWith('v', Qt::CaseInsensitive) ? version.mid(1) : version;
    QString currentVersion = QString(IMAGER_VERSION_STR);
    if (currentVersion.startsWith('v', Qt::CaseInsensitive)) {
        currentVersion = currentVersion.mid(1);
    }
    
    return QVersionNumber::fromString(serverVersion) > QVersionNumber::fromString(currentVersion);
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
    // Note: We intentionally skip filesystem validation (exists/isFile) for local files
    // as these calls can be slow on iCloud-synced directories or network volumes.
    // Invalid local files will fail gracefully when the actual fetch is attempted.
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
        // Local file URLs are allowed through - actual file access will validate existence
        return true;
    }

} // namespace anonymous

// Recursively find subitems_url entries in a JSON array and queue fetches
void ImageWriter::queueSublistFetches(const QJsonArray &list, int depth)
{
    if (depth > MAX_SUBITEMS_DEPTH) {
        qDebug() << "Aborting fetch of subitems JSON, exceeded maximum configured limit of" << MAX_SUBITEMS_DEPTH << "levels.";
        return;
    }

    for (const auto &entry : list) {
        auto entryObject = entry.toObject();
        if (entryObject.contains("subitems")) {
            // Recurse into existing subitems
            queueSublistFetches(entryObject["subitems"].toArray(), depth + 1);
        } else if (entryObject.contains("subitems_url")) {
            const QString subUrlStr = entryObject["subitems_url"].toString();
            const QUrl subUrl(subUrlStr);
            if (!preflightValidateUrl(subUrl, QStringLiteral("subitems_url:"))) {
                continue;
            }

            // Create a CurlFetcher for this sublist
            auto *fetcher = new CurlFetcher(this);
            connect(fetcher, &CurlFetcher::finished, this, &ImageWriter::onOsListFetchComplete);
            connect(fetcher, &CurlFetcher::error, this, &ImageWriter::onOsListFetchError);
            connect(fetcher, &CurlFetcher::connectionStats, this, &ImageWriter::onNetworkConnectionStats);
            
            // Track start time for performance
            _pendingFetchStartTimes[subUrl] = QDateTime::currentMSecsSinceEpoch();
            fetcher->fetch(subUrl);
        }
    }
}


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

void ImageWriter::setSWCapabilitiesList(const QVariantList &caps) {
    _swCapabilities = QJsonArray{};
    for (const auto &cap : caps) {
        _swCapabilities.append(cap.toString().trimmed().toLower());
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
    const auto needle = cap.trimmed().toLower();
    for (const auto &v : _hwCapabilities)
        if (v.toString().toLower() == needle) return true;
    return false;
}

bool ImageWriter::checkSWCapability(const QString &cap) {
    const auto needle = cap.trimmed().toLower();
    for (const auto &v : _swCapabilities)
        if (v.toString().toLower() == needle) return true;
    return false;
}

void ImageWriter::onOsListFetchComplete(const QByteArray &data, const QUrl &url)
{
    // Calculate request duration for performance tracking
    quint32 durationMs = 0;
    qint64 bytesReceived = data.size();
    
    if (_pendingFetchStartTimes.contains(url)) {
        qint64 startTime = _pendingFetchStartTimes.take(url);
        durationMs = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() - startTime);
    }
    
    // Track if this is the top-level OS list request
    bool isTopLevelRequest = (url == osListUrl());

    auto response_object = QJsonDocument::fromJson(data).object();

    if (response_object.contains("os_list")) {
        // Step 1: Insert the items into the canonical JSON document.
        //         It doesn't matter that these may still contain subitems_url items
        //         As these will be fixed up as the subitems_url instances are blinked in
        bool wasEmpty = _completeOsList.isEmpty();
        
        // Stop network monitoring on any successful fetch (initial or refresh)
        // This handles both the startup case and the "refresh failed, now succeeded" case
        PlatformQuirks::stopNetworkMonitoring();
        
        if (wasEmpty) {
            _completeOsList = QJsonDocument(response_object);
            // Notify UI that OS list is now available (was unavailable, now has data)
            emit osListUnavailableChanged();
        } else {
            // Preserve latest top-level imager metadata if present in the top-level fetch
            auto new_list = findAndInsertJsonResult(_completeOsList["os_list"].toArray(), response_object["os_list"].toArray(), url, 1);
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

        // Queue fetches for any subitems_url entries
        queueSublistFetches(response_object["os_list"].toArray(), 1);
        emit osListPrepared();
        
        // Record performance event for OS list fetch
        if (durationMs > 0) {
            PerformanceStats::EventType eventType = isTopLevelRequest 
                ? PerformanceStats::EventType::OsListFetch 
                : PerformanceStats::EventType::SublistFetch;
            QString metadata = url.toString();
            _performanceStats->recordTransferEvent(eventType, durationMs, bytesReceived, true, metadata);
        }

        // After processing a top-level list fetch, (re)schedule the next refresh
        if (isTopLevelRequest) {
            scheduleOsListRefresh();
        }
    } else {
        qDebug() << "Incorrectly formatted OS list at:" << url;
        // If this was the top-level fetch and it failed, notify UI
        if (isTopLevelRequest && _completeOsList.isEmpty()) {
            qWarning() << "Top-level OS list fetch failed - malformed response. Operating in offline mode.";
            emit osListUnavailableChanged();
        }
    }
}

void ImageWriter::onOsListFetchError(const QString &errorMessage, const QUrl &url)
{
    // Clean up start time tracking
    _pendingFetchStartTimes.remove(url);
    
    // Track if this is the top-level OS list request
    bool isTopLevelRequest = (url == osListUrl());

    // Detect scheme-less URL usage which commonly happens with local repo files
    if (url.scheme().isEmpty() || !url.isValid()) {
        qWarning() << "Failed to fetch URL '" << url.toString()
                   << "' - the URL appears to be relative or missing a scheme."
                   << "When using a local --repo file, ensure all subitems_url entries are absolute"
                   << "(e.g., file:///path/to/sublist.json or https://...).";
    }

    qDebug() << "Failed to fetch URL [" << url << "]:" << errorMessage;
    
    if (isTopLevelRequest) {
        if (_completeOsList.isEmpty()) {
            // No data at all - notify UI of offline state
            qWarning() << "Top-level OS list fetch failed:" << errorMessage << ". Operating in offline mode.";
            emit osListUnavailableChanged();
        } else {
            // We have stale data - reschedule refresh to retry later
            // Use a shorter retry interval (5 minutes) rather than full refresh interval
            qWarning() << "OS list refresh failed:" << errorMessage << ". Will retry in 5 minutes.";
            _osListRefreshTimer.start(5 * 60 * 1000);  // 5 minute retry
            
            // Also start network monitoring to catch when connectivity returns
            if (!PlatformQuirks::hasNetworkConnectivity()) {
                PlatformQuirks::startNetworkMonitoring([this](bool available) {
                    if (available) {
                        qDebug() << "Network restored - retrying OS list refresh";
                        _osListRefreshTimer.stop();  // Cancel pending retry timer
                        QMetaObject::invokeMethod(this, "beginOSListFetch", Qt::QueuedConnection);
                    }
                });
            }
        }
    }
}

void ImageWriter::onNetworkConnectionStats(const QString &statsMetadata, const QUrl &url)
{
    // Record CURL connection timing metrics for performance analysis
    // Format: "dns_ms: X; connect_ms: X; tls_ms: X; ttfb_ms: X; total_ms: X; speed_kbps: X; size_bytes: X; http: X"
    QString fullMetadata = QString("url: %1; %2").arg(url.host(), statsMetadata);
    _performanceStats->recordEvent(PerformanceStats::EventType::NetworkConnectionStats, 0, true, fullMetadata);
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

    // Create a CurlFetcher to fetch the OS list
    auto *fetcher = new CurlFetcher(this);
    connect(fetcher, &CurlFetcher::finished, this, &ImageWriter::onOsListFetchComplete);
    connect(fetcher, &CurlFetcher::error, this, &ImageWriter::onOsListFetchError);
    connect(fetcher, &CurlFetcher::connectionStats, this, &ImageWriter::onNetworkConnectionStats);
    
    // Track start time for performance
    _pendingFetchStartTimes[topUrl] = QDateTime::currentMSecsSinceEpoch();
    
    // This will set up a chain of requests that culminate in the eventual fetch and assembly of
    // a complete cached OS list.
    fetcher->fetch(topUrl);
}

void ImageWriter::refreshOsListFrom(const QUrl &url) {
    setCustomRepo(url);
    bool wasAvailable = !_completeOsList.isEmpty();
    _completeOsList = QJsonDocument();
    if (wasAvailable) {
        // Notify UI that OS list is now unavailable (cleared for refetch)
        emit osListUnavailableChanged();
    }
    _osListRefreshTimer.stop();
#ifndef CLI_ONLY_BUILD
    // Clear icon cache when switching repositories - new repo will have different icons
    IconMultiFetcher::instance().clearCache();
#endif
    beginOSListFetch();
    emit customRepoChanged();
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
    try {
        if (_suspendInhibitor == nullptr)
            _suspendInhibitor = CreateSuspendInhibitor();
    } catch (...) {
        // Continue anyway if we can't create the inhibitor
    }
    
    _dlnow = 0;
    _verifynow = 0;
    
    // Create and start the progress watchdog component
    if (!_progressWatchdog) {
        _progressWatchdog = new WriteProgressWatchdog(this);
        
        // Connect watchdog signals to our handlers
        connect(_progressWatchdog, &WriteProgressWatchdog::switchedToSyncMode,
                this, [this](QString reason) {
                    qDebug() << "Watchdog: Hot-swap to sync mode succeeded";
                    _performanceStats->recordEvent(PerformanceStats::EventType::WatchdogRecovery, 0, true,
                        QString("action=hotSwap; %1").arg(reason));
                    emit operationWarning(reason);
                });
        connect(_progressWatchdog, &WriteProgressWatchdog::restartNeeded,
                this, [this](QString reason) {
                    _performanceStats->recordEvent(PerformanceStats::EventType::WatchdogRecovery, 0, false,
                        QString("action=restart; %1").arg(reason));
                    restartWrite(reason);
                });
        connect(_progressWatchdog, &WriteProgressWatchdog::hardTimeout,
                this, [this](QString error) {
                    _performanceStats->recordEvent(PerformanceStats::EventType::WatchdogRecovery, 0, false,
                        QString("action=hardTimeout; %1").arg(error));
                    onError(error);
                });
        connect(_progressWatchdog, &WriteProgressWatchdog::stallWarning,
                this, [this](int seconds, int pending) {
                    qDebug() << "Stall warning:" << seconds << "s," << pending << "pending";
                    _performanceStats->recordEvent(PerformanceStats::EventType::ProgressStall, 
                        static_cast<uint32_t>(seconds * 1000), true,
                        QString("pending=%1").arg(pending));
                });
    }
    
    if (_thread) {
        _progressWatchdog->start(_thread);
    }
}

void ImageWriter::stopProgressPolling()
{
    // Stop the progress watchdog
    if (_progressWatchdog) {
        _progressWatchdog->stop();
    }
    
    // Release the inhibition on system suspend and display sleep
    if (_suspendInhibitor != nullptr) {
        delete _suspendInhibitor;
        _suspendInhibitor = nullptr;
    }
}

void ImageWriter::restartWrite(QString reason)
{
    qDebug() << "Restarting write:" << reason;
    
    // Show warning to user
    emit operationWarning(reason);
    
    // Force sync mode for the retry
    _forceSyncMode = true;
    
    // Record the failed attempt
    _performanceStats->endSession(false, "Async I/O stall - restarting in sync mode");
    
    // Stop current thread and restart when it finishes
    if (_thread && _thread->isRunning()) {
        connect(_thread, &QThread::finished, this, [this]() {
            qDebug() << "Thread stopped, starting new write in sync mode";
            _thread->deleteLater();
            _thread = nullptr;
            startWrite();
        }, Qt::SingleShotConnection);
        
        _thread->cancelDownload();
    } else {
        // Thread already stopped
        if (_thread) {
            _thread->deleteLater();
            _thread = nullptr;
        }
        startWrite();
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

    // Don't validate the path with filesystem calls (exists/isReadable) - these can be
    // extremely slow on macOS with iCloud-synced directories or network volumes.
    // The native file dialog handles invalid/missing paths gracefully by falling back
    // to its default location.
    if (path.isEmpty())
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

void ImageWriter::_parseGzFile()
{
    QFile f(_src.toLocalFile());
    _extrLen = 0;

    // Gzip trailer format (last 8 bytes):
    // - CRC32 (4 bytes, little-endian)
    // - ISIZE (4 bytes, little-endian) - original file size modulo 2^32
    //
    // IMPORTANT: The ISIZE field is only 32 bits, so it stores the original size
    // modulo 2^32. This means we cannot reliably determine the uncompressed size
    // for files larger than 4GB. We still parse it for storage space estimation,
    // but mark the size as unreliable for progress display purposes.
    const qint64 GZIP_TRAILER_SIZE = 8;

    // Mark gzip extract size as unreliable - the format cannot accurately represent
    // sizes >4GB, so progress percentage cannot be reliably calculated.
    // This causes the UI to show an indeterminate progress bar instead of misleading percentages.
    _extractSizeKnown = false;

    if (f.size() > GZIP_TRAILER_SIZE && f.open(QIODevice::ReadOnly))
    {
        f.seek(f.size() - 4);  // Seek to ISIZE field (last 4 bytes)
        QByteArray isizeData = f.read(4);

        if (isizeData.size() == 4)
        {
            // ISIZE is stored as little-endian 32-bit unsigned integer
            quint32 isize = static_cast<quint8>(isizeData[0]) |
                           (static_cast<quint8>(isizeData[1]) << 8) |
                           (static_cast<quint8>(isizeData[2]) << 16) |
                           (static_cast<quint8>(isizeData[3]) << 24);

            _extrLen = isize;

            // Handle files larger than 4GB where ISIZE wraps around
            // If the uncompressed size appears smaller than the compressed size,
            // the original file was likely > 4GB. This is a heuristic for storage
            // space checks but NOT reliable for progress calculation.
            qint64 compressedSize = f.size();
            while (_extrLen < static_cast<quint64>(compressedSize))
            {
                _extrLen += Q_UINT64_C(0x100000000);  // Add 4GB
            }

            qDebug() << "Parsed .gz file. Estimated uncompressed size:" << _extrLen
                     << "(ISIZE field:" << isize << ") - size unreliable for progress";
        }
        else
        {
            qDebug() << "Unable to read ISIZE from .gz file";
        }

        f.close();
    }
    else
    {
        qDebug() << "Unable to open .gz file for parsing";
    }
}

bool ImageWriter::isOnline()
{
    // Use platform abstraction for network connectivity check
    bool hasBasicConnectivity = PlatformQuirks::hasNetworkConnectivity();
    
    // For embedded mode, report IP addresses on status display and check time sync
    if (isEmbeddedMode()) {
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
            
            // Check if network is truly ready (including time sync)
            bool networkReady = PlatformQuirks::isNetworkReady();
            if (networkReady) {
                _networkchecktimer.stop();
                beginOSListFetch();
                emit networkOnline();
            }
            return networkReady;
        }
        return false;
    }
    
    // For non-embedded (desktop) mode: if network becomes available after being
    // unavailable, and we never successfully fetched the OS list, trigger a retry.
    // This handles the case where the initial fetch failed due to a firewall blocking
    // access, and the user later grants permission (fixes GitHub issue #1212).
    if (hasBasicConnectivity && !_online && _completeOsList.isEmpty()) {
        qDebug() << "Network now available and OS list empty - retrying fetch";
        _online = true;
        beginOSListFetch();
        emit networkOnline();
    } else if (hasBasicConnectivity && !_online) {
        // Network came online but we already have OS list data
        _online = true;
    } else if (!hasBasicConnectivity && _online) {
        // Network went offline
        _online = false;
    } else if (!hasBasicConnectivity && _completeOsList.isEmpty()) {
        // No network and no OS list - notify UI so it can show offline state
        // This handles startup without network (fixes GitHub issue #809)
        emit osListUnavailableChanged();
        
        // Start monitoring for network availability so we can auto-retry
        PlatformQuirks::startNetworkMonitoring([this](bool available) {
            if (available && _completeOsList.isEmpty()) {
                qDebug() << "Network became available - auto-retrying OS list fetch";
                // Use QMetaObject::invokeMethod to ensure we're on the Qt thread
                QMetaObject::invokeMethod(this, "beginOSListFetch", Qt::QueuedConnection);
            }
        });
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

bool ImageWriter::hasWindowDecorations() const
{
#ifndef CLI_ONLY_BUILD
    // Embedded mode never has decorations
    if (isEmbeddedMode())
        return false;

    // Check platform - some don't support decorations at all
    QString platform = QGuiApplication::platformName().toLower();
    if (platform == "linuxfb" || platform == "eglfs" ||
        platform == "minimal" || platform == "offscreen")
        return false;

    // Check if main window exists and has been shown
    if (_mainWindow) {
        // Check for explicit frameless hint
        if (_mainWindow->flags() & Qt::FramelessWindowHint)
            return false;

        // Check frame margins - if top margin is 0, WM isn't providing decorations
        // (tiling WMs like i3/sway, WM rules to hide title bars, etc.)
        QMargins margins = _mainWindow->frameMargins();
        if (margins.top() == 0)
            return false;
    }

    return true;
#else
    return false;
#endif
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

// Debug options implementation (secret menu: Cmd+Option+S on macOS, Ctrl+Alt+S on others)
bool ImageWriter::getDebugDirectIO() const
{
    return _debugDirectIO;
}

void ImageWriter::setDebugDirectIO(bool enabled)
{
    if (_debugDirectIO != enabled) {
        _debugDirectIO = enabled;
        qDebug() << "Debug: Direct I/O" << (enabled ? "enabled" : "disabled");
    }
}

bool ImageWriter::getDebugPeriodicSync() const
{
    return _debugPeriodicSync;
}

void ImageWriter::setDebugPeriodicSync(bool enabled)
{
    if (_debugPeriodicSync != enabled) {
        _debugPeriodicSync = enabled;
        qDebug() << "Debug: Periodic sync" << (enabled ? "enabled" : "disabled");
    }
}

bool ImageWriter::getDebugVerboseLogging() const
{
    return _debugVerboseLogging;
}

void ImageWriter::setDebugVerboseLogging(bool enabled)
{
    if (_debugVerboseLogging != enabled) {
        _debugVerboseLogging = enabled;
        qDebug() << "Debug: Verbose logging" << (enabled ? "enabled" : "disabled");
    }
}

bool ImageWriter::getDebugAsyncIO() const
{
    return _debugAsyncIO;
}

void ImageWriter::setDebugAsyncIO(bool enabled)
{
    if (_debugAsyncIO != enabled) {
        _debugAsyncIO = enabled;
        qDebug() << "Debug: Async I/O" << (enabled ? "enabled" : "disabled");
    }
}

int ImageWriter::getDebugAsyncQueueDepth() const
{
    return _debugAsyncQueueDepth;
}

void ImageWriter::setDebugAsyncQueueDepth(int depth)
{
    // Cap to max ring buffer depth (512) to prevent exceeding allocated slots
    depth = qBound(1, depth, 512);
    if (_debugAsyncQueueDepth != depth) {
        _debugAsyncQueueDepth = depth;
        qDebug() << "Debug: Async queue depth set to" << depth;
    }
}

bool ImageWriter::getDebugIPv4Only() const
{
    return _debugIPv4Only;
}

void ImageWriter::setDebugIPv4Only(bool enabled)
{
    if (_debugIPv4Only != enabled) {
        _debugIPv4Only = enabled;
        // Update the shared network config so all libcurl operations use IPv4-only
        CurlNetworkConfig::instance().setIPv4Only(enabled);
        qDebug() << "Debug: IPv4-only mode" << (enabled ? "enabled" : "disabled");
        
        // If IPv4-only was just enabled and we don't have an OS list yet,
        // retry the fetch - the user likely enabled this because IPv6 was failing
        if (enabled && _completeOsList.isEmpty() && isOnline()) {
            qDebug() << "IPv4-only enabled with empty OS list - retrying fetch";
            beginOSListFetch();
        }
    }
}

bool ImageWriter::getDebugSkipEndOfDevice() const
{
    return _debugSkipEndOfDevice;
}

void ImageWriter::setDebugSkipEndOfDevice(bool enabled)
{
    if (_debugSkipEndOfDevice != enabled) {
        _debugSkipEndOfDevice = enabled;
        qDebug() << "Debug: Skip end-of-device operations" << (enabled ? "enabled (counterfeit card mode)" : "disabled");
    }
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

void ImageWriter::setImageCustomisation(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork, const ImageOptions::AdvancedOptions opts, const QByteArray &initFormat)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudinitNetwork;
    _advancedOptions = opts;
    
    // If initFormat is provided, use it; otherwise keep current value
    // This allows CLI to explicitly set the format along with content
    if (!initFormat.isEmpty()) {
        _initFormat = initFormat;
    }

    qDebug() << "Custom config.txt entries:" << config;
    qDebug() << "Custom cmdline.txt entries:" << cmdline;
    qDebug() << "Custom firstrun.sh:" << firstrun;
    qDebug() << "Cloudinit:" << cloudinit;
    qDebug() << "Advanced options:" << opts;
    qDebug() << "initFormat parameter:" << initFormat << "-> _initFormat:" << _initFormat;
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
    // Note: Don't validate rsaKeyPath with QFile::exists() here - it can be slow on
    // iCloud-synced or network paths. The actual write operation will validate the file.
    ImageOptions::AdvancedOptions advOpts = NoAdvancedOptions;
    bool secureBootEnabled = s.value("secureBootEnabled").toBool();
    QString rsaKeyPath = _settings.value("secureboot_rsa_key").toString();
    if (secureBootEnabled && !rsaKeyPath.isEmpty()) {
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
    // Note: Don't validate rsaKeyPath with QFile::exists() here - it can be slow on
    // iCloud-synced or network paths. The actual write operation will validate the file.
    ImageOptions::AdvancedOptions advOpts = NoAdvancedOptions;
    bool secureBootEnabled = s.value("secureBootEnabled").toBool();
    QString rsaKeyPath = _settings.value("secureboot_rsa_key").toString();
    if (secureBootEnabled && !rsaKeyPath.isEmpty()) {
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

QString ImageWriter::customRepoHost()
{
    if (!customRepo()) {
        return QString();
    }
    
    if (_repo.isLocalFile()) {
        // For local files, show the filename instead of host
        return _repo.fileName();
    }
    
    // For remote URLs, return the host in ASCII (punycode) to prevent IDN homograph attacks
    // Also truncate very long hostnames to prevent UI issues
    QString host = _repo.host(QUrl::FullyEncoded);
    if (host.length() > 50) {
        host = host.left(47) + QStringLiteral("...");
    }
    return host;
}

bool ImageWriter::isValidRepoUrl(const QString &url) const
{
    // Validate: must be http/https URL ending with .json or manifest extension
    static const QRegularExpression repoUrlRe(
        QStringLiteral("^https?://[^ \\t\\r\\n]+\\.(json|" MANIFEST_EXTENSION ")$"), 
        QRegularExpression::CaseInsensitiveOption);
    return repoUrlRe.match(url).hasMatch();
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

    // Record cache verification duration for performance tracking with hash values
    quint32 verificationDurationMs = static_cast<quint32>(_cacheVerificationTimer.elapsed());
    CacheManager::CacheStatus cacheStatus = _cacheManager->getCacheStatus();
    QString metadata = QString("Cache verification; expectedHash: %1; computedHash: %2")
        .arg(QString::fromLatin1(cacheStatus.cacheFileHash),
             QString::fromLatin1(cacheStatus.computedHash));
    _performanceStats->recordEvent(PerformanceStats::EventType::CacheVerification,
        verificationDurationMs, isValid, metadata);

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
        // Use platform-specific write device path (e.g., rdisk on macOS for direct I/O)
        QString writeDevicePath = PlatformQuirks::getWriteDevicePath(_dst);
        try {
            _thread = new LocalFileExtractThread(urlstr.toLatin1(), writeDevicePath.toLatin1(), _expectedHash, this);
        } catch (const std::bad_alloc& e) {
            _handleMemoryAllocationFailure(e.what());
            return;
        } catch (const std::exception& e) {
            _handleSetupException(e.what());
            return;
        }
    }
    else
    {
        // Use platform-specific write device path (e.g., rdisk on macOS for direct I/O)
        QString writeDevicePath = PlatformQuirks::getWriteDevicePath(_dst);
        try {
            _thread = new DownloadExtractThread(urlstr.toLatin1(), writeDevicePath.toLatin1(), _expectedHash, this);
            if (_repo.toString() == OSLIST_URL)
            {
                DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr.toLatin1(), _parentCategory.toLatin1(), _osName.toLatin1(), isEmbeddedMode(), _currentLangcode, this);
                connect(tele, SIGNAL(finished()), tele, SLOT(deleteLater()));
                tele->start();
            }
        } catch (const std::bad_alloc& e) {
            _handleMemoryAllocationFailure(e.what());
            return;
        } catch (const std::exception& e) {
            _handleSetupException(e.what());
            return;
        }
    }

    // Set the extract size for accurate write progress (compressed images have larger extracted size)
    _thread->setExtractTotal(_extrLen > 0 ? _extrLen : _downloadLen);

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
        connect(downloadThread, &DownloadExtractThread::writeProgressChanged,
                this, &ImageWriter::writeProgress);
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, &ImageWriter::verifyProgress);
        
        // Connect async write progress signal for event-driven UI updates during WaitForPendingWrites
        // This signal is emitted from IOCP completion callbacks, providing real-time progress
        connect(downloadThread, &DownloadThread::asyncWriteProgress,
                this, &ImageWriter::writeProgress, Qt::QueuedConnection);
        
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
        connect(downloadThread, &DownloadThread::asyncWriteProgress,
                this, [this](quint64 now, quint64 total){
                    _performanceStats->recordWriteProgress(now, total);
                }, Qt::QueuedConnection);
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
    connect(_thread, &DownloadThread::eventDriveAuthorization,
            this, [this](quint32 durationMs, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveAuthorization, durationMs, success);
            });
    connect(_thread, &DownloadThread::eventDriveMbrZeroing,
            this, [this](quint32 durationMs, bool success, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DriveMbrZeroing, durationMs, success, metadata);
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
                // Update systemInfo with actual direct I/O state now that we know it
                _performanceStats->updateDirectIOEnabled(currentlyEnabled);
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
            this, [this](quint32 durationMs, bool success, QByteArray writeHash, QByteArray verifyHash){
                QString metadata = QString("Post-write verification; writeHash: %1; verifyHash: %2")
                    .arg(QString::fromLatin1(writeHash), QString::fromLatin1(verifyHash));
                _performanceStats->recordEvent(PerformanceStats::EventType::HashComputation, durationMs, success, metadata);
            });
    connect(_thread, &DownloadThread::eventPeriodicSync,
            this, [this](quint32 durationMs, bool success, quint64 bytesWritten){
                QString metadata = QString("at %1 MB").arg(bytesWritten / (1024 * 1024));
                _performanceStats->recordEvent(PerformanceStats::EventType::PeriodicSync, durationMs, success, metadata);
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
    connect(_thread, &DownloadThread::eventDeviceIOTimeout,
            this, [this](quint32 pendingWrites, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::DeviceIOTimeout, 
                    30000, false, QString("pending=%1; %2").arg(pendingWrites).arg(metadata));
            });
    connect(_thread, &DownloadThread::eventQueueDepthReduction,
            this, [this](int oldDepth, int newDepth, int pendingWrites){
                _performanceStats->recordEvent(PerformanceStats::EventType::QueueDepthReduction, 0, true,
                    QString("depth=%1->%2; pending=%3").arg(oldDepth).arg(newDepth).arg(pendingWrites));
            });
    connect(_thread, &DownloadThread::eventDrainAndHotSwap,
            this, [this](quint32 durationMs, int pendingBefore, bool success){
                _performanceStats->recordEvent(PerformanceStats::EventType::DrainAndHotSwap, durationMs, success,
                    QString("pending=%1").arg(pendingBefore));
            });
    connect(_thread, &DownloadThread::syncFallbackActivated,
            this, [this](QString reason){
                _performanceStats->recordEvent(PerformanceStats::EventType::SyncFallbackActivated, 0, true, reason);
                emit operationWarning(reason);
            });
    connect(_thread, &DownloadThread::requestWriteRestart,
            this, &ImageWriter::restartWrite);
    connect(_thread, &DownloadThread::eventNetworkRetry,
            this, [this](quint32 sleepMs, QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkRetry, sleepMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventNetworkConnectionStats,
            this, [this](QString metadata){
                _performanceStats->recordEvent(PerformanceStats::EventType::NetworkConnectionStats, 0, true, metadata);
            });
    
    // Write timing breakdown signals (for detailed hypothesis testing)
    connect(_thread, &DownloadThread::eventWriteTimingBreakdown,
            this, [this](quint32 totalWriteOps, quint64 totalSyscallMs, quint64 totalPreHashWaitMs,
                         quint64 totalPostHashWaitMs, quint64 totalSyncMs, quint32 syncCount){
                QString metadata = QString("writeOps: %1; syscallMs: %2; preHashWaitMs: %3; postHashWaitMs: %4; syncMs: %5; syncCount: %6")
                    .arg(totalWriteOps).arg(totalSyscallMs).arg(totalPreHashWaitMs)
                    .arg(totalPostHashWaitMs).arg(totalSyncMs).arg(syncCount);
                // Use total time (syscall + hash waits) as duration
                quint32 totalMs = static_cast<quint32>(totalSyscallMs + totalPreHashWaitMs + totalPostHashWaitMs);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteTimingBreakdown, totalMs, true, metadata);
            });
    connect(_thread, &DownloadThread::eventWriteSizeDistribution,
            this, [this](quint32 minSizeKB, quint32 maxSizeKB, quint32 avgSizeKB, quint64 totalBytes, quint32 writeCount){
                QString metadata = QString("minKB: %1; maxKB: %2; avgKB: %3; totalBytes: %4; count: %5")
                    .arg(minSizeKB).arg(maxSizeKB).arg(avgSizeKB).arg(totalBytes).arg(writeCount);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteSizeDistribution, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventWriteAfterSyncImpact,
            this, [this](quint32 avgThroughputBeforeSyncKBps, quint32 avgThroughputAfterSyncKBps, quint32 sampleCount){
                QString metadata = QString("beforeSyncKBps: %1; afterSyncKBps: %2; samples: %3")
                    .arg(avgThroughputBeforeSyncKBps).arg(avgThroughputAfterSyncKBps).arg(sampleCount);
                // Calculate the impact percentage (positive = degradation after sync)
                int impactPercent = 0;
                if (avgThroughputBeforeSyncKBps > 0) {
                    impactPercent = static_cast<int>(100 * (static_cast<int>(avgThroughputBeforeSyncKBps) - static_cast<int>(avgThroughputAfterSyncKBps)) / static_cast<int>(avgThroughputBeforeSyncKBps));
                }
                metadata += QString("; impactPercent: %1").arg(impactPercent);
                _performanceStats->recordEvent(PerformanceStats::EventType::WriteAfterSyncImpact, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventAsyncIOConfig,
            this, [this](bool enabled, bool supported, int queueDepth, quint32 pendingAtEnd){
                QString metadata = QString("enabled: %1; supported: %2; queueDepth: %3; pendingAtEnd: %4")
                    .arg(enabled).arg(supported).arg(queueDepth).arg(pendingAtEnd);
                _performanceStats->recordEvent(PerformanceStats::EventType::AsyncIOConfig, 0, true, metadata);
            });
    connect(_thread, &DownloadThread::eventAsyncIOTiming,
            this, [this](quint32 totalMs, quint64 bytesWritten, quint32 writeCount){
                QString metadata = QString("wallClockMs: %1; bytesWritten: %2 MB; writeCount: %3")
                    .arg(totalMs).arg(bytesWritten / (1024*1024)).arg(writeCount);
                _performanceStats->recordTransferEvent(
                    PerformanceStats::EventType::AsyncIOTiming,
                    totalMs, bytesWritten, true, metadata);
            });
    
    // Forward bottleneck state to QML for UI feedback
    connect(_thread, &DownloadThread::bottleneckStateChanged,
            this, [this](DownloadThread::BottleneckState state, quint32 throughputKBps){
                QString statusText;
                switch (state) {
                    case DownloadThread::BottleneckState::None:
                        statusText = "";
                        break;
                    case DownloadThread::BottleneckState::Network:
                        statusText = tr("Limited by download speed");
                        break;
                    case DownloadThread::BottleneckState::Decompression:
                        statusText = tr("Limited by decompression speed");
                        break;
                    case DownloadThread::BottleneckState::Storage:
                        statusText = tr("Limited by storage device speed");
                        break;
                    case DownloadThread::BottleneckState::Verifying:
                        statusText = tr("Verifying written data");
                        break;
                }
                emit bottleneckStatusChanged(statusText, throughputKBps);
            });

    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(staticVersion()).toUtf8());
    qDebug() << "_continueStartWrite: Passing to thread - initFormat:" << _initFormat << "cloudinit empty:" << _cloudinit.isEmpty() << "cloudinitNetwork empty:" << _cloudinitNetwork.isEmpty();
    _thread->setImageCustomisation(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);
    
    // Pass debug options to the thread
    _thread->setDebugDirectIO(_debugDirectIO);
    _thread->setDebugPeriodicSync(_debugPeriodicSync);
    _thread->setDebugVerboseLogging(_debugVerboseLogging);
    // Disable async I/O if forced to sync mode (due to previous recovery)
    _thread->setDebugAsyncIO(_debugAsyncIO && !_forceSyncMode);
    if (_forceSyncMode) {
        qDebug() << "Compatibility mode active - using synchronous I/O";
    }
    _thread->setDebugAsyncQueueDepth(_debugAsyncQueueDepth);
    _thread->setDebugIPv4Only(_debugIPv4Only);
    _thread->setDebugSkipEndOfDevice(_debugSkipEndOfDevice);

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

    // Check if this is a local file URL for a manifest file (double-click on manifest or .json file)
    if (url.isLocalFile()) {
        const QString localPath = url.toLocalFile();
        if (localPath.endsWith(QLatin1String("." MANIFEST_EXTENSION), Qt::CaseInsensitive) ||
            localPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            QFileInfo fi(localPath);
            if (fi.isFile() && fi.isReadable()) {
                qDebug() << "Local manifest file opened:" << localPath;
                emit repositoryUrlReceived(url.toString());
                return;
            } else {
                qWarning() << "Cannot read manifest file:" << localPath;
            }
        }
        return;
    }

    // Check for repository URL parameter (repo=https://example.com/repo.json)
    QUrlQuery query(url);
    const QString repoVal = query.queryItemValue(QStringLiteral("repo"), QUrl::FullyDecoded);
    if (!repoVal.isEmpty()) {
        if (isValidRepoUrl(repoVal)) {
            qDebug() << "Valid repository URL received:" << repoVal;
            emit repositoryUrlReceived(repoVal);
        } else {
            qWarning() << "Ignoring invalid repository URL format:" << repoVal;
        }
    }

    // Check for auth_key token (existing behavior)
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

bool ImageWriter::isBeepAvailable()
{
    return PlatformQuirks::isBeepAvailable();
}

bool ImageWriter::hasPerformanceData()
{
    return _performanceStats->hasData();
}

QString ImageWriter::getPerformanceDataFilename()
{
    return QString("rpi-imager-performance-%1.json")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
}

bool ImageWriter::exportPerformanceData()
{
#ifndef CLI_ONLY_BUILD
    if (!_performanceStats->hasData()) {
        qDebug() << "No performance data to export";
        return false;
    }
    
    // Warn if there's no imaging session data (only background events)
    if (!_performanceStats->hasImagingData()) {
        qWarning() << "Performance data contains no imaging session - data may have been exported"
                   << "before a write operation or after app restart";
    }
    
    // Generate default filename with timestamp
    QString defaultFilename = getPerformanceDataFilename();
    QString initialDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    
    // Check if native dialogs are available
    if (!NativeFileDialog::areNativeDialogsAvailable()) {
        // Emit signal for QML to handle the save dialog
        qDebug() << "Native file dialog not available, requesting QML fallback";
        emit performanceSaveDialogNeeded(defaultFilename, initialDir);
        return true; // Signal emitted, QML will handle the rest
    }
    
    // Get save location from user using native file dialog
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
    
    return exportPerformanceDataToFile(filePath);
#else
    qDebug() << "Performance data export not available in CLI build";
    return false;
#endif
}

bool ImageWriter::exportPerformanceDataToFile(const QString &filePath)
{
#ifndef CLI_ONLY_BUILD
    if (!_performanceStats->hasData()) {
        qDebug() << "No performance data to export";
        return false;
    }
    
    QString finalPath = filePath;
    
    // Ensure .json extension
    if (!finalPath.endsWith(".json", Qt::CaseInsensitive)) {
        finalPath += ".json";
    }
    
    // Export data - all complex processing happens here, triggered by user action
    bool success = _performanceStats->exportToFile(finalPath);
    if (success) {
        qDebug() << "Performance data exported to:" << finalPath;
    }
    return success;
#else
    Q_UNUSED(filePath);
    qDebug() << "Performance data export not available in CLI build";
    return false;
#endif
}

void ImageWriter::_handleMemoryAllocationFailure(const char* what)
{
    qDebug() << "Memory allocation failed during write setup:" << what;
    
    // Log the failure to performance stats
    _performanceStats->recordEvent(
        PerformanceStats::EventType::MemoryAllocationFailure,
        0,  // No duration - immediate failure
        false,
        QString("Failed to allocate memory for write operation: %1").arg(what)
    );
    
    // Provide a clear, actionable error message to the user
    QString errorMsg = tr("Failed to start write operation: insufficient memory.\n\n"
                          "The system does not have enough available memory to perform this operation. "
                          "Try closing other applications to free up memory, then try again.\n\n"
                          "Technical details: %1").arg(what);
    
    setWriteState(WriteState::Failed);
    _performanceStats->endSession(false, "Memory allocation failure");
    emit error(errorMsg);
}

void ImageWriter::_handleSetupException(const char* what)
{
    qDebug() << "Exception during write setup:" << what;
    
    _performanceStats->recordEvent(
        PerformanceStats::EventType::MemoryAllocationFailure,
        0,
        false,
        QString("Exception during write setup: %1").arg(what)
    );
    
    setWriteState(WriteState::Failed);
    _performanceStats->endSession(false, QString("Setup exception: %1").arg(what));
    emit error(tr("Failed to start write operation: %1").arg(what));
}
