/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "downloadextractthread.h"
#include "imagewriter.h"
#include "iconimageprovider.h"
#include "buffer_optimization.h"
#include "drivelistitem.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "driveformatthread.h"
#include "localfileextractthread.h"
#include "downloadstatstelemetry.h"
#include "wlancredentials.h"
#include "device_info.h"
#include "nativefiledialog.h"
#include "platformquirks.h"
#include <archive.h>
#include <archive_entry.h>
#include <lzma.h>
#include <qjsondocument.h>
#include <QJsonArray>
#include <random>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QQmlContext>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimeZone>
#include <QWindow>
#include <QGuiApplication>
#include <QNetworkInterface>
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
#include <QDesktopServices>
#include <QRandomGenerator>
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
#endif

using namespace ImageOptions;

namespace {
    constexpr uint MAX_SUBITEMS_DEPTH = 16;
} // namespace anonymous

ImageWriter::ImageWriter(QObject *parent)
    : QObject(parent), 
      _cacheManager(nullptr), // Initialize after _embeddedMode
      _waitingForCacheVerification(false),
      _networkManager(this),
      _src(), _repo(QUrl(QString(OSLIST_URL))), 
      _dst(), _parentCategory(), _osName(), _currentLang(), _currentLangcode(), _currentKeyboard(),
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
      _powersave(),
      _thread(nullptr), 
      _verifyEnabled(true), _multipleFilesInZip(false), _embeddedMode(false), _online(false),
      _settings(),
      _translations(),
      _trans(nullptr),
      _refreshIntervalOverrideMinutes(-1),
      _refreshJitterOverrideMinutes(-1)
{
    // Initialize CacheManager now that _embeddedMode is properly initialized
    _cacheManager = new CacheManager(_embeddedMode, this);
    
    QString platform;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) )
    {
        platform = QGuiApplication::platformName();
    }
    else
    {
        platform = "cli";
    }
    _device_info = std::make_unique<DeviceInfo>();

#ifdef Q_OS_LINUX
    if (platform == "linuxfb")
    {
        _embeddedMode = true;
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

        StpAnalyzer *stpAnalyzer = new StpAnalyzer(5, this);
        connect(stpAnalyzer, SIGNAL(detected()), SLOT(onSTPdetected()));
        stpAnalyzer->startListening("eth0");
    }
#endif

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
        /* FIXME: we currently lack a font with support for Chinese characters in embedded mode */
        //if (isEmbeddedMode() && langcode == "zh")
        //    continue;

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
    //_currentKeyboard = "us";

    // Centralised network manager, for fetching OS lists
    connect(&_networkManager, SIGNAL(finished(QNetworkReply *)), this, SLOT(handleNetworkRequestFinished(QNetworkReply *)));
    
    // Connect to CacheManager signals
    connect(_cacheManager, &CacheManager::cacheFileUpdated,
            this, [this](const QByteArray& hash) { 
                qDebug() << "Received cacheFileUpdated signal - refreshing UI for hash:" << hash;
                // Prefer surgical UI update: notify model rows changed (cache status text)
                if (_engine) {
                    // Access the OSListModel via getter and mark it dirty without reset
                    OSListModel *m = getOSList();
                    if (m) m->markCacheStatusDirty();
                } else {
                    // Fallback if no engine: emit legacy signal
                    emit osListPrepared();
                }
            });
    
    connect(_cacheManager, &CacheManager::cacheVerificationComplete,
            this, [this](bool isValid) { 
                if (isValid) {
                    // Prefer surgical UI update: notify model rows changed (cache status text)
                    if (_engine) {
                        // Access the OSListModel via getter and mark it dirty without reset
                        OSListModel *m = getOSList();
                        if (m) m->markCacheStatusDirty();
                    } else {
                        // Fallback if no engine: emit legacy signal
                        emit osListPrepared();
                    }
                }
            });
    
    connect(_cacheManager, &CacheManager::cacheInvalidated,
            this, [this]() { 
                // Prefer surgical UI update: notify model rows changed (cache status text)
                if (_engine) {
                    // Access the OSListModel via getter and mark it dirty without reset
                    OSListModel *m = getOSList();
                    if (m) m->markCacheStatusDirty();
                } else {
                    // Fallback if no engine: emit legacy signal
                    emit osListPrepared();
                }
            });
    
    // Connect to specific device removal events
    connect(&_drivelist, &DriveListModel::deviceRemoved,
            this, &ImageWriter::onSelectedDeviceRemoved);
    
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

QString ImageWriter::getNativeOpenFileName(const QString &title,
                                           const QString &initialDir,
                                           const QString &filter)
{
    if (!NativeFileDialog::areNativeDialogsAvailable()) {
        return QString();
    }
    return NativeFileDialog::getOpenFileName(title, initialDir, filter);
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
    
    if (_trans)
    {
        QCoreApplication::removeTranslator(_trans);
        delete _trans;
    }
}

void ImageWriter::setEngine(QQmlApplicationEngine *engine)
{
    _engine = engine;
    if (_engine) {
        // Register icon provider for image://icons/<url>
        _engine->addImageProvider(QStringLiteral("icons"), new IconImageProvider());
    }
}

/* Set URL to download from */
void ImageWriter::setSrc(const QUrl &url, quint64 downloadLen, quint64 extrLen, QByteArray expectedHash, bool multifilesinzip, QString parentcategory, QString osname, QByteArray initFormat)
{
    _src = url;
    _downloadLen = downloadLen;
    _extrLen = extrLen;
    _expectedHash = expectedHash;
    _multipleFilesInZip = multifilesinzip;
    _parentCategory = parentcategory;
    _osName = osname;
    _initFormat = (initFormat == "none") ? "" : initFormat;

    if (!_downloadLen && url.isLocalFile())
    {
        QFileInfo fi(url.toLocalFile());
        _downloadLen = fi.size();
    }
    if (url.isLocalFile())
    {
        // For custom local images (including .img, .zip, etc.), do not offer customization
        _initFormat = "";
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
        QString reason;
        if (_src.isEmpty())
            reason = tr("No image selected.");
        else if (_dst.isEmpty())
            reason = tr("No storage device selected.");
        else if (!_selectedDeviceValid)
            reason = tr("Selected storage device is no longer available.");
        else
            reason = tr("Unknown precondition failure.");

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

    if (!_expectedHash.isEmpty() && _cacheManager->isCached(_expectedHash))
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
            DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr, _parentCategory.toLatin1(), _osName.toLatin1(), _embeddedMode, _currentLangcode, this);
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
        // Also transition state to Verifying when verify progress first arrives
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 /*now*/, quint64 /*total*/){
                    if (_writeState != WriteState::Verifying && _writeState != WriteState::Finalizing && _writeState != WriteState::Succeeded)
                        setWriteState(WriteState::Verifying);
                });
    }
    
    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(constantVersion()).toUtf8());
    _thread->setImageCustomization(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);

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

/* Function to return OS list URL */
QUrl ImageWriter::constantOsListUrl() const
{
    return _repo;
}

/* Function to return version */
QString ImageWriter::constantVersion() const
{
    return IMAGER_VERSION_STR;
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
                manager.get(request);
            }
        }
    }
} // namespace anonymous


void ImageWriter::setHWFilterList(const QJsonArray &tags, const bool &inclusive) {
    _deviceFilter = tags;
    _deviceFilterIsInclusive = inclusive;
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
                    if (response_object.contains("imager") && data->request().url() == constantOsListUrl()) {
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

                // After processing a top-level list fetch, (re)schedule the next refresh
                if (data->request().url() == constantOsListUrl()) {
                    scheduleOsListRefresh();
                }
            } else {
                qDebug() << "Incorrectly formatted OS list at: " << data->url();
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
            data->manager()->get(request);

            // maintain manager
            return;
        } else if (httpStatusCode >= 400 && httpStatusCode < 600) {
            // HTTP Error
            qDebug() << "Failed to fetch URL [" << data->url() << "], got: " << httpStatusCode;
        } else {
            // Completely unknown error, worth logging separately
            qDebug() << "Failed to fetch URL [" << data->url() << "], got unknown response code: " << httpStatusCode;
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
    const QUrl topUrl = constantOsListUrl();
    if (!preflightValidateUrl(topUrl, QStringLiteral("repository:"))) {
        return;
    }

    QNetworkRequest request = QNetworkRequest(topUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // This will set up a chain of requests that culiminate in the eventual fetch and assembly of
    // a complete cached OS list.
   _networkManager.get(request);
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
    _powersave.applyBlock(tr("Downloading and writing image"));
    _dlnow = 0; 
    _verifynow = 0;
}

void ImageWriter::stopProgressPolling()
{
    _powersave.removeBlock();
}

void ImageWriter::setWriteState(WriteState state)
{
    if (_writeState == state)
        return;
    _writeState = state;
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
    emit success();

    if (_settings.value("beep").toBool()) {
        PlatformQuirks::beep();
    }
}

void ImageWriter::onError(QString msg)
{
    setWriteState(WriteState::Failed);
    stopProgressPolling();
    emit error(msg);

    if (_settings.value("beep").toBool()) {
        PlatformQuirks::beep();
    }
}

void ImageWriter::onFinalizing()
{
    setWriteState(WriteState::Finalizing);
    emit finalizing();
}

void ImageWriter::onPreparationStatusUpdate(QString msg)
{
    // heuristic: entering verifying is handled via progress elsewhere
    emit preparationStatusUpdate(msg);
}

void ImageWriter::openFileDialog()
{
    QSettings settings;
    QString path = settings.value("lastpath").toString();
    QFileInfo fi(path);

    if (path.isEmpty() || !fi.exists() || !fi.isReadable() )
        path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    // Use native file dialog - QtWidgets fallback removed
    QString filename = NativeFileDialog::getOpenFileName(tr("Select image"),
                                                        path,
                                                        "Image files (*.img *.zip *.iso *.gz *.xz *.zst *.wic);;All files (*)");

    // Process the selected file if one was chosen
    if (!filename.isEmpty())
    {
        onFileSelected(filename);
    }
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
    /* Check if we have an IP-address other than localhost */
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    bool online = false;

    foreach (QHostAddress a, addresses)
    {
        if (!a.isLoopback() && a.scopeId().isEmpty())
        {
            /* Not a loopback or IPv6 link-local address, so online */
            qDebug() << "IP DETECTED: " << a.toString();
            emit networkInfo(QString("IP: %1").arg(a.toString()));
            online = true;
            break;
        }
    }

    if (online) {
        QNetworkRequest request(QUrl(TIME_URL));
        request.setTransferTimeout(3000); // 3 seconds
        QNetworkReply* response = _networkManager.get(request);
        
        // Connect to the finished signal to ensure headers are available
        QObject::connect(response, &QNetworkReply::finished, [response, this]() {
            if (response->hasRawHeader("date"))
                {
                bool timeSet = false;                
                // systemd-timesyncd will change the timestamp of this file to indicate that the time has been set
                QString filePath = "/var/lib/systemd/timesync/clock";
                QDateTime clock_time;
                QFileInfo fileInfo(filePath);
                
                if (fileInfo.exists()) {clock_time = fileInfo.lastModified();}

                filePath = "/lib/systemd/systemd-timesyncd";
                QDateTime creation_time;
                QFileInfo fileInfo2(filePath);
                
                if (fileInfo2.exists())
                    creation_time = fileInfo2.lastModified();
                if (clock_time > creation_time)
                    timeSet = true;

                if (timeSet)
                {
                    _networkchecktimer.stop();
                    beginOSListFetch();
                    emit networkOnline();
                }
            }
            else
            {
                qDebug() << "Unable to access time server";
            }
            response->deleteLater();
        });
    }
    return online;
}

void ImageWriter::pollNetwork()
{
    isOnline();
}

void ImageWriter::onSTPdetected()
{
    emit networkInfo(tr("STP is enabled on your Ethernet switch. Getting IP will take long time."));
}

bool ImageWriter::isEmbeddedMode()
{
    return _embeddedMode;
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
    return !_embeddedMode;
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
    if ( f.open(f.ReadOnly) )
    {
        timezones = QString(f.readAll()).split('\n');
        f.close();
    }

    return timezones;
}

QStringList ImageWriter::getCountryList()
{
    QStringList countries;
    QFile f(":/countries.txt");
    if ( f.open(f.ReadOnly) )
    {
        countries = QString(f.readAll()).trimmed().split('\n');
        f.close();
    }

    return countries;
}

QStringList ImageWriter::getKeymapLayoutList()
{
    QStringList keymaps;
    QFile f(":/keymap-layouts.txt");
    if ( f.open(f.ReadOnly) )
    {
        keymaps = QString(f.readAll()).trimmed().split('\n');
        f.close();
    }

    return keymaps;
}


QString ImageWriter::getSSID()
{
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

void ImageWriter::setSetting(const QString &key, const QVariant &value)
{
    _settings.setValue(key, value);
    _settings.sync();
}

void ImageWriter::setImageCustomization(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork, const ImageOptions::AdvancedOptions opts)
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

void ImageWriter::applyCustomizationFromSavedSettings()
{
    // Build customization payloads from saved settings
    QVariantMap s = getSavedCustomizationSettings();

    // If the selected image does not support customization, ensure nothing is staged
    if (_initFormat.isEmpty()) {
        setImageCustomization(QByteArray(), QByteArray(), QByteArray(), QByteArray(), QByteArray(), NoAdvancedOptions);
        return;
    }

    if (_initFormat == "systemd") {
        _applySystemdCustomizationFromSettings(s);
    } else {
        _applyCloudInitCustomizationFromSettings(s);
    }
}

void ImageWriter::_applySystemdCustomizationFromSettings(const QVariantMap &s)
{
    QByteArray script;
    auto line = [](const QString &l, QByteArray &out){ out += l.toUtf8(); out += '\n'; };

    const QString hostname = s.value("hostname").toString().trimmed();
    const QString timezone = s.value("timezone").toString().trimmed();
    const bool sshEnabled = s.value("sshEnabled").toBool();
    const bool sshPasswordAuth = s.value("sshPasswordAuth").toBool();
    const QString userName = s.value("sshUserName").toString().trimmed();
    const QString userPass = s.value("sshUserPassword").toString(); // crypted if present
    const QString sshPublicKey = s.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = s.value("sshAuthorizedKeys").toString().trimmed();
    const QString ssid = s.value("wifiSSID").toString();
    const QString cryptedPskFromSettings = s.value("wifiPasswordCrypt").toString();
    bool hidden = s.value("wifiHidden").toBool();
    if (!hidden)
        hidden = s.value("wifiSSIDHidden").toBool();
    const QString wifiCountry = s.value("wifiCountry").toString().trimmed();
    QByteArray cmdlineAppend;
    if (!wifiCountry.isEmpty()) {
        cmdlineAppend = QByteArray(" ") + QByteArray("cfg80211.ieee80211_regdom=") + wifiCountry.toUtf8();
    }
    // Prefer persisted crypted PSK; fallback to deriving from legacy plaintext setting
    QString cryptedPsk = cryptedPskFromSettings;
    if (cryptedPsk.isEmpty())
    {
        const QString legacyPwd = s.value("wifiPassword").toString();
        const bool isPassphrase = (legacyPwd.length() >= 8 && legacyPwd.length() < 64);
        cryptedPsk = isPassphrase ? pbkdf2(legacyPwd.toUtf8(), ssid.toUtf8()) : legacyPwd;
    }
    // Prepare SSH key arguments for imager_custom
    auto shellQuote = [](const QString &v){ QString t = v; t.replace("'", "'\"'\"'"); return QString("'") + t + QString("'"); };
    QStringList keyList;
    if (!sshAuthorizedKeys.isEmpty()) {
        const QStringList lines = sshAuthorizedKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
        for (const QString &k : lines) keyList.append(k.trimmed());
    } else if (!sshPublicKey.isEmpty()) {
        keyList.append(sshPublicKey);
    }
    QString pubkeyArgs;
    for (const QString &k : keyList) {
        pubkeyArgs += " ";
        pubkeyArgs += shellQuote(k);
    }

    const QString effectiveUser = userName.isEmpty() ? QStringLiteral("pi") : userName;
    const QString groups = QStringLiteral("users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo");

    line(QStringLiteral("#!/bin/bash"), script);
    line(QStringLiteral("set -e"), script);

    if (!hostname.isEmpty()) {
        line(QStringLiteral("CURRENT_HOSTNAME=$(cat /etc/hostname | tr -d \" \\\t\\\n\\\r\")"), script);
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_hostname ") + hostname, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   echo ") + hostname + QStringLiteral(" >/etc/hostname"), script);
        line(QStringLiteral("   sed -i \"s/127.0.1.1.*$CURRENT_HOSTNAME/127.0.1.1\\t") + hostname + QStringLiteral("/g\" /etc/hosts"), script);
        line(QStringLiteral("fi"), script);
    }

    if (!timezone.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_timezone ") + shellQuote(timezone), script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   rm -f /etc/localtime"), script);
        line(QStringLiteral("   echo \"") + timezone + QStringLiteral("\" >/etc/timezone"), script);
        line(QStringLiteral("   dpkg-reconfigure -f noninteractive tzdata"), script);
        line(QStringLiteral("fi"), script);
    }

    const QString keyboardLayout = s.value("keyboardLayout").toString().trimmed();
    if (!keyboardLayout.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_keymap ") + shellQuote(keyboardLayout), script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("cat >/etc/default/keyboard <<'KBEOF'"), script);
        line(QStringLiteral("XKBMODEL=\"pc105\""), script);
        line(QStringLiteral("XKBLAYOUT=\"") + keyboardLayout + QStringLiteral("\""), script);
        line(QStringLiteral("XKBVARIANT=\"\""), script);
        line(QStringLiteral("XKBOPTIONS=\"\""), script);
        line(QStringLiteral("KBEOF"), script);
        line(QStringLiteral("   dpkg-reconfigure -f noninteractive keyboard-configuration"), script);
        line(QStringLiteral("fi"), script);
    }

    // Determine first user (uid 1000) and home, for parity with legacy behavior
    line(QStringLiteral("FIRSTUSER=$(getent passwd 1000 | cut -d: -f1)"), script);
    line(QStringLiteral("FIRSTUSERHOME=$(getent passwd 1000 | cut -d: -f6)"), script);
    // Ensure desired user exists when explicit username was provided
    line(QStringLiteral("if ! id -u ") + effectiveUser + QStringLiteral(" >/dev/null 2>&1; then useradd -m -G ") + groups + QStringLiteral(" -s /bin/bash ") + effectiveUser + QStringLiteral("; fi"), script);

    if (sshEnabled) {
        // Legacy uses raspi-config to enable SSH when present
        line(QStringLiteral("if command -v raspi-config >/dev/null 2>&1; then raspi-config nonint do_ssh 0 || true; fi"), script);
        line(QStringLiteral("systemctl enable ssh || systemctl enable sshd || true"), script);
    }

    if (!userPass.isEmpty()) {
        line(QStringLiteral("echo ") + QLatin1Char('\'') + effectiveUser + QLatin1Char(':') + userPass + QLatin1Char('\'') + QStringLiteral(" | chpasswd -e"), script);
    } else if (!sshPublicKey.isEmpty() || !sshAuthorizedKeys.isEmpty()) {
        line(QStringLiteral("passwd -l ") + effectiveUser + QStringLiteral(" || true"), script);
    }

    if (!keyList.isEmpty()) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh -k") + pubkeyArgs, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   install -o \"$FIRSTUSER\" -m 700 -d \"$FIRSTUSERHOME/.ssh\""), script);
        // Fallback: write keys with heredoc (simpler/more robust than process substitution in this context)
        QString allKeys = keyList.join("\n");
        line(QStringLiteral("cat > \"$FIRSTUSERHOME/.ssh/authorized_keys\" <<'EOF'"), script);
        line(allKeys, script);
        line(QStringLiteral("EOF"), script);
        line(QStringLiteral("   chown \"$FIRSTUSER:$FIRSTUSER\" \"$FIRSTUSERHOME/.ssh/authorized_keys\""), script);
        line(QStringLiteral("   chmod 600 \"$FIRSTUSERHOME/.ssh/authorized_keys\""), script);
        line(QStringLiteral("   echo 'PasswordAuthentication no' >>/etc/ssh/sshd_config"), script);
        line(QStringLiteral("   systemctl enable ssh || systemctl enable sshd || true"), script);
        line(QStringLiteral("fi"), script);
        // Permit passwordless sudo when using SSH key (script runs as root; no need for bash -c)
        line(QStringLiteral("echo \"") + effectiveUser + QStringLiteral(" ALL=(ALL) NOPASSWD:ALL\" >/etc/sudoers.d/010-") + effectiveUser, script);
    }

    if (sshEnabled && sshPasswordAuth) {
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        line(QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom enable_ssh"), script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("   echo 'PasswordAuthentication yes' >>/etc/ssh/sshd_config"), script);
        line(QStringLiteral("   systemctl enable ssh || systemctl enable sshd || true"), script);
        line(QStringLiteral("fi"), script);
    }

    if (!ssid.isEmpty()) {
        // Prefer imager_custom set_wlan; fallback to manual wpa_supplicant
        line(QStringLiteral("if [ -f /usr/lib/raspberrypi-sys-mods/imager_custom ]; then"), script);
        QString wlanCmd = QStringLiteral("   /usr/lib/raspberrypi-sys-mods/imager_custom set_wlan ");
        if (hidden) wlanCmd += QStringLiteral(" -h ");
        wlanCmd += shellQuote(ssid) + QStringLiteral(" ") + shellQuote(cryptedPsk) + QStringLiteral(" ") + shellQuote(wifiCountry);
        line(wlanCmd, script);
        line(QStringLiteral("else"), script);
        line(QStringLiteral("cat >/etc/wpa_supplicant/wpa_supplicant.conf <<'WPAEOF'"), script);
        if (!wifiCountry.isEmpty()) line(QStringLiteral("country=") + wifiCountry, script);
        line(QStringLiteral("ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev"), script);
        line(QStringLiteral("ap_scan=1"), script);
        line(QStringLiteral(""), script);
        line(QStringLiteral("update_config=1"), script);
        line(QStringLiteral("network={"), script);
        if (hidden) line(QStringLiteral("\tscan_ssid=1"), script);
        line(QStringLiteral("\tssid=\"") + ssid + QStringLiteral("\""), script);
        if (!cryptedPsk.isEmpty()) line(QStringLiteral("\tpsk=") + cryptedPsk, script);
        line(QStringLiteral("}"), script);
        line(QStringLiteral("WPAEOF"), script);
        line(QStringLiteral("   chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf"), script);
        line(QStringLiteral("   rfkill unblock wifi"), script);
        line(QStringLiteral("   for filename in /var/lib/systemd/rfkill/*:wlan ; do"), script);
        line(QStringLiteral("       echo 0 > $filename"), script);
        line(QStringLiteral("   done"), script);
        line(QStringLiteral("fi"), script);
    }

    // Raspberry Pi Connect token provisioning (store in target user's home)
    const bool piConnectEnabled = s.value("piConnectEnabled").toBool();
    QString piConnectToken = _piConnectToken.trimmed();
    if (piConnectEnabled && !piConnectToken.isEmpty()) {
        // Determine home directory for the effective user
        line(QStringLiteral("TARGET_USER=\"") + effectiveUser + QStringLiteral("\""), script);
        line(QStringLiteral("TARGET_HOME=$(getent passwd \"$TARGET_USER\" | cut -d: -f6)"), script);
        line(QStringLiteral("if [ -z \"$TARGET_HOME\" ] || [ ! -d \"$TARGET_HOME\" ]; then TARGET_HOME=\"/home/" ) + effectiveUser + QStringLiteral("\"; fi"), script);
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"$TARGET_HOME/com.raspberrypi.connect\""), script);
        line(QStringLiteral("cat > \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\" <<'EOF'"), script);
        line(piConnectToken, script);
        line(QStringLiteral("EOF"), script);
        line(QStringLiteral("chown \"$TARGET_USER:$TARGET_USER\" \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\""), script);
        line(QStringLiteral("chmod 600 \"$TARGET_HOME/com.raspberrypi.connect/deploy.key\""), script);

        // Enable systemd user service rpi-connect-signin.service for the target user
        line(QStringLiteral("install -o \"$TARGET_USER\" -m 700 -d \"$TARGET_HOME/.config/systemd/user/default.target.wants\""), script);
        line(QStringLiteral("UNIT_SRC=\"/usr/lib/systemd/user/rpi-connect-signin.service\"; [ -f \"$UNIT_SRC\" ] || UNIT_SRC=\"/lib/systemd/user/rpi-connect-signin.service\""), script);
        line(QStringLiteral("ln -sf \"$UNIT_SRC\" \"$TARGET_HOME/.config/systemd/user/default.target.wants/rpi-connect-signin.service\""), script);
        line(QStringLiteral("chown -R \"$TARGET_USER:$TARGET_USER\" \"$TARGET_HOME/.config/systemd\" || true"), script);
    }

    // Final cleanup to mimic legacy behavior
    line(QStringLiteral("rm -f /boot/firstrun.sh"), script);
    line(QStringLiteral("sed -i 's| systemd.run.*||g' /boot/cmdline.txt"), script);
    line(QStringLiteral("exit 0"), script);

    setImageCustomization(QByteArray(), cmdlineAppend, script, QByteArray(), QByteArray(), NoAdvancedOptions);
}

void ImageWriter::_applyCloudInitCustomizationFromSettings(const QVariantMap &s)
{
    QByteArray cloud;
    QByteArray netcfg;

    auto push = [](const QString &line, QByteArray &out){ if (!line.isEmpty()) { out += line.toUtf8(); out += '\n'; } };

    const QString hostname = s.value("hostname").toString().trimmed();
    if (!hostname.isEmpty()) {
        push(QStringLiteral("hostname: ") + hostname, cloud);
        push(QStringLiteral("manage_etc_hosts: true"), cloud);
        // Parity with legacy QML: install avahi-daemon and disable apt Check-Date on first boot
        push(QStringLiteral("packages:"), cloud);
        push(QStringLiteral("- avahi-daemon"), cloud);
        push(QString(), cloud);
        push(QStringLiteral("apt:"), cloud);
        push(QStringLiteral("  conf: |"), cloud);
        push(QStringLiteral("    Acquire {"), cloud);
        push(QStringLiteral("      Check-Date \"false\";"), cloud);
        push(QStringLiteral("    };"), cloud);
        push(QString(), cloud);
    }

    const QString timezone = s.value("timezone").toString().trimmed();
    if (!timezone.isEmpty()) {
        push(QStringLiteral("timezone: ") + timezone, cloud);
    }

    // Parity with legacy QML: include keyboard model/layout when locale is set
    const QString keyboardLayout = s.value("keyboardLayout").toString().trimmed();
    if (!keyboardLayout.isEmpty()) {
        push(QStringLiteral("keyboard:"), cloud);
        push(QStringLiteral("  model: pc105"), cloud);
        push(QStringLiteral("  layout: \"") + keyboardLayout + QStringLiteral("\""), cloud);
    }

    const bool sshEnabled = s.value("sshEnabled").toBool();
    const bool sshPasswordAuth = s.value("sshPasswordAuth").toBool();
    const QString userName = s.value("sshUserName").toString().trimmed();
    const QString userPass = s.value("sshUserPassword").toString(); // expected crypted if present
    const QString sshPublicKey = s.value("sshPublicKey").toString().trimmed();
    const QString sshAuthorizedKeys = s.value("sshAuthorizedKeys").toString().trimmed();

    if (sshEnabled) {
        push(QStringLiteral("enable_ssh: true"), cloud);

        if (!userName.isEmpty()) {
            push(QStringLiteral("users:"), cloud);
            // Parity: legacy QML used the typed username even when not renaming the user.
            // Fall back to getCurrentUser() when SSH is enabled and no explicit username was saved.
            const QString effectiveUser = userName.isEmpty() && sshEnabled ? getCurrentUser() : (userName.isEmpty() ? QStringLiteral("pi") : userName);
            push(QStringLiteral("- name: ") + effectiveUser, cloud);
            push(QStringLiteral("  groups: users,adm,dialout,audio,netdev,video,plugdev,cdrom,games,input,gpio,spi,i2c,render,sudo"), cloud);
            push(QStringLiteral("  shell: /bin/bash"), cloud);
            if (!userPass.isEmpty()) {
                push(QStringLiteral("  lock_passwd: false"), cloud);
                push(QStringLiteral("  passwd: ") + userPass, cloud);
            } else if (!sshPublicKey.isEmpty() || !sshAuthorizedKeys.isEmpty()) {
                push(QStringLiteral("  lock_passwd: true"), cloud);
            }
            // Include all authorized keys (multi-line) if provided, else fall back to single key
            if (!sshAuthorizedKeys.isEmpty() || !sshPublicKey.isEmpty()) {
                push(QStringLiteral("  ssh_authorized_keys:"), cloud);
                if (!sshAuthorizedKeys.isEmpty()) {
                    const QStringList keys = sshAuthorizedKeys.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);
                    for (const QString &k : keys) {
                        push(QStringLiteral("    - ") + k.trimmed(), cloud);
                    }
                } else {
                    push(QStringLiteral("    - ") + sshPublicKey, cloud);
                }
                push(QStringLiteral("  sudo: ALL=(ALL) NOPASSWD:ALL"), cloud);
            }
            push(QString(), cloud); // blank line
        }

        if (sshPasswordAuth) {
            push(QStringLiteral("ssh_pwauth: true"), cloud);
        }
    }

    const bool isRpiosCloudInit = checkSWCapability("rpios_cloudinit");
    const bool enableI2C = s.value("enableI2C").toBool();
    const bool enableSPI = s.value("enableSPI").toBool();
    const QString enableSerial = s.value("enableSerial").toString();
    const bool armInterfaceEnabled = enableI2C || enableSPI || enableSerial != "Disabled";
    const bool isUsbGadgetEnabled = s.value("enableUsbGadget").toBool();

    // cc_raspberry_pi config for rpios_cloudinit capable OSs
    if (isRpiosCloudInit && (armInterfaceEnabled)) {
        push(QStringLiteral("rpi:"), cloud);

        // configure arm interfaces
        if (armInterfaceEnabled) {
            push(QStringLiteral("  interfaces:"), cloud);

            if (enableI2C) {
                push(QStringLiteral("    i2c: true"), cloud);
            }
            if (enableSPI) {
                push(QStringLiteral("    spi: true"), cloud);
            }
            if (enableSerial != "Disabled") {
                if (enableSerial == "" || enableSerial == "Default") {
                    push(QStringLiteral("    serial: true"), cloud);
                } else {
                    push(QStringLiteral("    serial:"), cloud);
                    if (enableSerial == "Console & Hardware") {
                        push(QStringLiteral("      console: true"), cloud);
                        push(QStringLiteral("      hardware: true"), cloud);
                    } else if (enableSerial == "Console") {
                        push(QStringLiteral("      console: true"), cloud);
                        push(QStringLiteral("      hardware: false"), cloud);
                    } else if (enableSerial == "Hardware") {
                        push(QStringLiteral("      console: false"), cloud);
                        push(QStringLiteral("      hardware: true"), cloud);
                    } else {
                        qDebug() << "Invalid serial mode: " << enableSerial;
                    }
                }
            }
        }
    }

    if (isRpiosCloudInit && isUsbGadgetEnabled) {
        // TODO: maybe create configuration struct so struct.package_update to avoid duplicates and typos
        push(QStringLiteral("package_update: true"), cloud);
        push(QStringLiteral("package_upgrade: false"), cloud);
        push(QStringLiteral("packages:"), cloud);
        push(QStringLiteral("  - rpi-usb-gadget"), cloud);
        push(QStringLiteral("runcmd:"), cloud);
        push(QStringLiteral("  - rpi-usb-gadget on"), cloud);
    }

    const QString ssid = s.value("wifiSSID").toString();
    const QString cryptedPskFromSettings = s.value("wifiPasswordCrypt").toString();
    const bool hidden = s.value("wifiHidden").toBool();
    const QString wifiCountry = s.value("wifiCountry").toString().trimmed();
    QByteArray cmdlineAppend;
    if (!wifiCountry.isEmpty()) {
        cmdlineAppend = QByteArray(" ") + QByteArray("cfg80211.ieee80211_regdom=") + wifiCountry.toUtf8();
    }
    if (!ssid.isEmpty()) {
        push(QStringLiteral("network:"), netcfg);
        push(QStringLiteral("  version: 2"), netcfg);
        push(QStringLiteral("  wifis:"), netcfg);
        push(QStringLiteral("    renderer: %1")
                 .arg(isRpiosCloudInit ? QStringLiteral("NetworkManager")
                                       : QStringLiteral("networkd")),
             netcfg);
        push(QStringLiteral("    wlan0:"), netcfg);
        push(QStringLiteral("      dhcp4: true"), netcfg);
        push(QStringLiteral("      access-points:"), netcfg);
        {
            QString key = ssid;
            key.replace('"', QStringLiteral("\\\""));
            push(QStringLiteral("        \"") + key + QStringLiteral("\":"), netcfg);
        }
        // Prefer persisted crypted PSK; fallback to deriving from legacy plaintext setting
        QString effectiveCryptedPsk = cryptedPskFromSettings;
        if (effectiveCryptedPsk.isEmpty()) {
            const QString legacyPwd = s.value("wifiPassword").toString();
            if (!legacyPwd.isEmpty()) {
                const bool isPassphrase = (legacyPwd.length() >= 8 && legacyPwd.length() < 64);
                effectiveCryptedPsk = isPassphrase ? pbkdf2(legacyPwd.toUtf8(), ssid.toUtf8()) : legacyPwd;
            }
        }
        if (!effectiveCryptedPsk.isEmpty()) {
            QString epwd = effectiveCryptedPsk;
            epwd.replace('"', QStringLiteral("\\\""));
            push(QStringLiteral("          password: \"") + epwd + QStringLiteral("\""), netcfg);
        }
        if (hidden) {
            push(QStringLiteral("          hidden: true"), netcfg);
        }
        push(QStringLiteral("      optional: true"), netcfg);
    }

    // Raspberry Pi Connect token provisioning via cloud-init write_files (store in user's home)
    const bool piConnectEnabled = s.value("piConnectEnabled").toBool();
    QString piConnectToken = _piConnectToken.trimmed();
    if (piConnectEnabled && !piConnectToken.isEmpty()) {
        // Use the same effective user decision as above
        const QString effectiveUser = userName.isEmpty() && sshEnabled ? getCurrentUser() : (userName.isEmpty() ? QStringLiteral("pi") : userName);
        const QString targetPath = QStringLiteral("/home/") + effectiveUser + QStringLiteral("/com.raspberrypi.connect/deploy.key");
        push(QStringLiteral("write_files:"), cloud);
        push(QStringLiteral("  - path: ") + targetPath, cloud);
        push(QStringLiteral("    permissions: '0600'"), cloud);
        push(QStringLiteral("    owner: ") + effectiveUser + QStringLiteral(":") + effectiveUser, cloud);
        push(QStringLiteral("    content: |"), cloud);
        QString indented = QStringLiteral("      ") + piConnectToken;
        push(indented, cloud);
        // Ensure directory exists with correct owner
        push(QString(), cloud);
        push(QStringLiteral("runcmd:"), cloud);
        push(QStringLiteral("  - [ bash, -lc, \"install -o ") + effectiveUser + QStringLiteral(" -m 700 -d /home/") + effectiveUser + QStringLiteral("/com.raspberrypi.connect\" ]"), cloud);
    }

    setImageCustomization(QByteArray(), cmdlineAppend, QByteArray(), cloud, netcfg, NoAdvancedOptions);
}

QString ImageWriter::crypt(const QByteArray &password)
{
    QByteArray salt = "$5$";
    QByteArray saltchars =
      "./0123456789ABCDEFGHIJKLMNOPQRST"
      "UVWXYZabcdefghijklmnopqrstuvwxyz";
    std::mt19937 gen(static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch()));
    std::uniform_int_distribution<> uid(0, saltchars.length()-1);

    for (int i=0; i<10; i++)
        salt += saltchars[uid(gen)];

    return sha256_crypt(password.constData(), salt.constData());
}

QString ImageWriter::pbkdf2(const QByteArray &psk, const QByteArray &ssid)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, psk, ssid, 4096, 32).toHex();
}

void ImageWriter::setSavedCustomizationSettings(const QVariantMap &map)
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

QVariantMap ImageWriter::getSavedCustomizationSettings()
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

void ImageWriter::clearSavedCustomizationSettings()
{
    _settings.beginGroup("imagecustomization");
    _settings.remove("");
    _settings.endGroup();
    _settings.sync();
}

bool ImageWriter::hasSavedCustomizationSettings()
{
    _settings.sync();
    _settings.beginGroup("imagecustomization");
    bool result = !_settings.childKeys().isEmpty();
    _settings.endGroup();

    return result;
}

bool ImageWriter::imageSupportsCustomization()
{
    return !_initFormat.isEmpty();
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

    if (_engine)
    {
        _engine->retranslate();
    }

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
    return !_embeddedMode || QFile::exists("/dev/input/mouse0");
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
    
    _waitingForCacheVerification = false;
    
    // Disconnect signals to avoid receiving future notifications
    disconnect(_cacheManager, &CacheManager::cacheVerificationProgress,
               this, &ImageWriter::onCacheVerificationProgress);
    disconnect(_cacheManager, &CacheManager::cacheVerificationComplete,
               this, &ImageWriter::onCacheVerificationComplete);
    
    qDebug() << "Cache verification completed - valid:" << isValid;
    
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
            qDebug() << "Cancelling write operation due to device removal";
            _cancelledDueToDeviceRemoval = true;
            cancelWrite();
            // Will show specific error message in onCancelled()
        } else if (_writeState == WriteState::Succeeded) {
            // Write completed successfully and now drive was ejected - this is normal
            qDebug() << "Device removed after successful write - ignoring (likely ejected)";
            // Don't notify UI - this is expected behavior after successful write
        } else {
            // Normal case - device removed when not writing
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
            DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr.toLatin1(), _parentCategory.toLatin1(), _osName.toLatin1(), _embeddedMode, _currentLangcode, this);
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
        // Also transition state to Verifying when verify progress first arrives
        connect(downloadThread, &DownloadExtractThread::verifyProgressChanged,
                this, [this](quint64 /*now*/, quint64 /*total*/){
                    if (_writeState != WriteState::Verifying && _writeState != WriteState::Finalizing && _writeState != WriteState::Succeeded)
                        setWriteState(WriteState::Verifying);
                });
    }
    
    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(constantVersion()).toUtf8());
    _thread->setImageCustomization(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat, _advancedOptions);

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
    system("reboot");
}

void ImageWriter::openUrl(const QUrl &url)
{
    qDebug() << "Opening URL:" << url.toString();
    
    bool success = false;
    
#ifdef Q_OS_LINUX
    // Use xdg-open on Linux (including AppImage environments)
    int result = QProcess::execute("xdg-open", QStringList() << url.toString());
    success = (result == 0);
    if (!success) {
        qWarning() << "xdg-open failed with exit code:" << result;
    }
#elif defined(Q_OS_DARWIN)
    // Use open on macOS  
    int result = QProcess::execute("open", QStringList() << url.toString());
    success = (result == 0);
    if (!success) {
        qWarning() << "open failed with exit code:" << result;
    }
#elif defined(Q_OS_WIN)
    // Use start on Windows
    int result = QProcess::execute("cmd", QStringList() << "/c" << "start" << url.toString());
    success = (result == 0);
    if (!success) {
        qWarning() << "cmd /c start failed with exit code:" << result;
    }
#endif

    // Fallback to Qt's method if platform command failed or platform is unsupported
    if (!success) {
        qDebug() << "Falling back to QDesktopServices::openUrl";
        QDesktopServices::openUrl(url);
    }
}

void ImageWriter::handleIncomingUrl(const QUrl &url)
{
    qDebug() << "Incoming URL:" << url;
    emit connectCallbackReceived(QVariant::fromValue(url));
}

void ImageWriter::setRuntimeConnectToken(const QString &token)
{
    _piConnectToken = token;
}

QString ImageWriter::getRuntimeConnectToken() const
{
    return _piConnectToken;
}
