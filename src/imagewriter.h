#ifndef IMAGEWRITER_H
#define IMAGEWRITER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include <memory>

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QSettings>
#include <QString>
#include <QVariant>
#ifndef CLI_ONLY_BUILD
#include <QQmlEngine>
#endif
#include "config.h"
#include "suspend_inhibitor.h"
#include "drivelistmodel.h"
#include "hwlistmodel.h"
#include "oslistmodel.h"
#ifndef CLI_ONLY_BUILD
#include "nativefiledialog.h"
#include <QWindow>
#endif
#include "cachemanager.h"
#include "device_info.h"
#include "imageadvancedoptions.h"
#include "performancestats.h"

class QQmlApplicationEngine;
class DownloadThread;
class DownloadExtractThread;
class QTranslator;
class WriteProgressWatchdog;
#ifndef CLI_ONLY_BUILD
class NativeFileDialog;
#endif

class ImageWriter : public QObject
{
    Q_OBJECT
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
#endif
public:
    enum class WriteState {
        Idle,
        Preparing,
        Writing,
        Verifying,
        Finalizing,
        Succeeded,
        Failed,
        Cancelled
    };
    Q_ENUM(WriteState)

    explicit ImageWriter(QObject *parent = nullptr);
    virtual ~ImageWriter();
    void setEngine(QQmlApplicationEngine *engine);

    Q_PROPERTY(WriteState writeState READ writeState NOTIFY writeStateChanged)
    Q_PROPERTY(bool isOsListUnavailable READ isOsListUnavailable NOTIFY osListUnavailableChanged)

    /* Returns true if the extract size is reliably known (false for gz files which can't store sizes >4GB) */
    Q_INVOKABLE bool isExtractSizeKnown() const { return _extractSizeKnown; }

    /* Set URL to download from, and if known download length and uncompressed length */
    Q_INVOKABLE void setSrc(const QUrl &url, quint64 downloadLen = 0, quint64 extrLen = 0, QByteArray expectedHash = "", bool multifilesinzip = false, QString parentcategory = "", QString osname = "", QByteArray initFormat = "", QString releaseDate = "");

    /* Set device to write to */
    Q_INVOKABLE void setDst(const QString &device, quint64 deviceSize = 0);

    /* Set verification enabled */
    Q_INVOKABLE void setVerifyEnabled(bool verify);

    /* Set custom repo */
    Q_INVOKABLE void setCustomRepo(const QUrl &repo);

    /* Set custom cache file - now handled by CacheManager */
    Q_INVOKABLE void setCustomCacheFile(const QString &cacheFile, const QByteArray &sha256);

    /* Returns true if src and dst are set */
    Q_INVOKABLE bool readyToWrite();

    /* Returns true if running on a Raspberry Pi */
    Q_INVOKABLE bool isRaspberryPiDevice();

    /* Returns hardware name if running on a Raspberry Pi */
    Q_INVOKABLE QString getHardwareName();

    Q_INVOKABLE bool createHardwareTags();

    /* Start writing */
    Q_INVOKABLE void startWrite();

    /* Cancel write */
    Q_INVOKABLE void cancelWrite();

    /* Skip cache verification and proceed with download */
    Q_INVOKABLE void skipCacheVerification();

    /* Skip only the current post-write verification pass (does not change default setting) */
    Q_INVOKABLE void skipCurrentVerification();

    /* Return true if url is in our local disk cache */
    Q_INVOKABLE bool isCached(const QUrl &url, const QByteArray &sha256);

    /* Drive list polling runs continuously in background */

    /* Return list of available drives. Drive polling runs continuously in background.
       Note: If you mark this as Q_INVOKABLE, be sure to parent it to ImageWriter,
       to prevent GC from deleting it.
    */
    Q_INVOKABLE DriveListModel *getDriveList();

    /* Return list of available devices. */
    Q_INVOKABLE HWListModel *getHWList();

    /* Return list of available devices. */
    Q_INVOKABLE OSListModel *getOSList();

    /* Utility function to return filename part from URL */
    Q_INVOKABLE QString fileNameFromUrl(const QUrl &url);

    /* Function to return current OS list URL in a displayable format */
    Q_INVOKABLE QString osListUrlForDisplay() const;

    /* Function to return current OS list URL (may be customized) */
    Q_INVOKABLE QUrl osListUrl() const;

    /* Function to return version (for QML - C++ code should use staticVersion()) */
    Q_INVOKABLE QString constantVersion() const;

    /* Static version - for use without creating an instance */
    static QString staticVersion();

    /* Returns true if version argument is newer than current program */
    Q_INVOKABLE bool isVersionNewer(const QString &version);

    /* Set custom repository */
    Q_INVOKABLE void setCustomOsListUrl(const QUrl &url);

    /* Get the cached OS list. This may be empty if network connectivity is not available. */
    Q_INVOKABLE QByteArray getFilteredOSlist();

    /* Overload which returns QJsonDocument */
    Q_INVOKABLE QJsonDocument getFilteredOSlistDocument();

    /** Begin the asynchronous fetch of the OS lists, and associated sublists. */
    Q_INVOKABLE void beginOSListFetch();

    /** Set the HW filter, for a filtered view of the OS list */
    Q_INVOKABLE void setHWFilterList(const QJsonArray &tags, const bool &inclusive);

    /* Set the capabilities supported by the hardware, for a filtered view of options that require the hardware to have certain capabilities. */
    Q_INVOKABLE void setHWCapabilitiesList(const QJsonArray &json);

    /* Set the capabilities supported by the hardware, for a filtered view of options that require the software to have certain capabilities. */
    Q_INVOKABLE void setSWCapabilitiesList(const QString &json);
    Q_INVOKABLE void setSWCapabilitiesList(const QVariantList &caps);

    /* Get the HW filter list */
    Q_INVOKABLE QJsonArray getHWFilterList();

    /* Get if the HW filter is in inclusive mode */
    Q_INVOKABLE bool getHWFilterListInclusive();

    /* Get if both hard and software support a certain feature. If no differentSWCap is provided it will check for cap support in SW and HW lists. */
    Q_INVOKABLE bool checkHWAndSWCapability(const QString &cap, const QString &differentSWCap = "");

    /* Check if the hardware supports a certain feature. */
    Q_INVOKABLE bool checkHWCapability(const QString &cap);

    /* Check if the software supports a certain feature. */
    Q_INVOKABLE bool checkSWCapability(const QString &cap);

    /* Utility function to open OS file dialog */
    Q_INVOKABLE void openFileDialog(const QString &title, const QString &filter);

    /* Expose native file dialog availability to QML */
    Q_INVOKABLE bool nativeFileDialogAvailable() {
#ifndef CLI_ONLY_BUILD
        return NativeFileDialog::areNativeDialogsAvailable();
#else
        return false;
#endif
    }

    /* Accept selection from QML fallback FileDialog */
    Q_INVOKABLE void acceptCustomImageFromQml(const QUrl &fileUrl) { onFileSelected(fileUrl.toLocalFile()); }

    /* Generic native open-file dialog for QML callsites (sync) */
    Q_INVOKABLE QString getNativeOpenFileName(const QString &title = QString(),
                                             const QString &initialDir = QString(),
                                             const QString &filter = QString());

    /* Set the main window for modal file dialogs */
    Q_INVOKABLE void setMainWindow(QObject *window);

    /* Bring the application window to the foreground */
    void bringWindowToForeground();

    /* Read text file contents */
    Q_INVOKABLE QString readFileContents(const QString &filePath);

    /* Handle keychain permission response from QML */
    Q_INVOKABLE void keychainPermissionResponse(bool granted);

    /* Return filename part of URL set */
    Q_INVOKABLE QString srcFileName();
    
    /* Returns size (bytes) of selected source, if known; 0 if unknown */
    Q_INVOKABLE quint64 getSelectedSourceSize();
    /* Format a byte size with an appropriate translated unit (KB/MB/GB/TB) */
    Q_INVOKABLE QString formatSize(quint64 bytes, int decimals = 1);

    /* Returns true if online */
    Q_INVOKABLE bool isOnline();

    /* Returns true if run on embedded Linux platform */
    Q_INVOKABLE bool isEmbeddedMode() const;

    /* Returns true if window has decorations (title bar visible) */
    Q_INVOKABLE bool hasWindowDecorations() const;

    /* Mount any USB sticks that can contain source images under /media
       Returns true if at least one device was mounted */
    Q_INVOKABLE bool mountUsbSourceMedia();

    /* Returns a json formatted list of the OS images found on USB stick */
    Q_INVOKABLE QByteArray getUsbSourceOSlist();

    /* Functions to collect information from computer running imager to make image customization easier */
    Q_INVOKABLE QString getDefaultPubKey();
    Q_INVOKABLE bool hasPubKey();
    Q_INVOKABLE bool hasSshKeyGen();
    Q_INVOKABLE void generatePubKey();
    Q_INVOKABLE QString getTimezone();
    Q_INVOKABLE QStringList getTimezoneList();
    Q_INVOKABLE QStringList getCountryList();
    Q_INVOKABLE QStringList getKeymapLayoutList();
    Q_INVOKABLE QStringList getCapitalCitiesList();
    Q_INVOKABLE QVariantMap getLocaleDataForCapital(const QString &capitalCity);
    Q_INVOKABLE QString getSSID();
    Q_INVOKABLE QString getPSK();
    Q_INVOKABLE QString getPSKForSSID(const QString &ssid);

    Q_INVOKABLE bool getBoolSetting(const QString &key);
    Q_INVOKABLE QString getStringSetting(const QString &key);
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE QString getRsaKeyFingerprint(const QString &keyPath);
    
    // Debug options (secret menu: Cmd+Option+S on macOS, Ctrl+Alt+S on others)
    Q_INVOKABLE bool getDebugDirectIO() const;
    Q_INVOKABLE void setDebugDirectIO(bool enabled);
    Q_INVOKABLE bool getDebugPeriodicSync() const;
    Q_INVOKABLE void setDebugPeriodicSync(bool enabled);
    Q_INVOKABLE bool getDebugVerboseLogging() const;
    Q_INVOKABLE void setDebugVerboseLogging(bool enabled);
    Q_INVOKABLE bool getDebugAsyncIO() const;
    Q_INVOKABLE void setDebugAsyncIO(bool enabled);
    Q_INVOKABLE int getDebugAsyncQueueDepth() const;
    Q_INVOKABLE void setDebugAsyncQueueDepth(int depth);
    Q_INVOKABLE bool getDebugIPv4Only() const;
    Q_INVOKABLE void setDebugIPv4Only(bool enabled);
    Q_INVOKABLE bool getDebugSkipEndOfDevice() const;
    Q_INVOKABLE void setDebugSkipEndOfDevice(bool enabled);
    
    // Customisation API
    Q_INVOKABLE void applyCustomisationFromSettings(const QVariantMap &settings);  // Main entry: generates scripts from settings
    Q_INVOKABLE void setImageCustomisation(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork, const ImageOptions::AdvancedOptions opts = {}, const QByteArray &initFormat = {});  // Advanced: bypass generator with pre-made scripts
    
    // Persistence API
    Q_INVOKABLE void setSavedCustomisationSettings(const QVariantMap &map);  // Legacy: prefer setPersistedCustomisationSetting()
    Q_INVOKABLE QVariantMap getSavedCustomisationSettings();
    Q_INVOKABLE void setPersistedCustomisationSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void removePersistedCustomisationSetting(const QString &key);
    Q_INVOKABLE bool imageSupportsCustomization();
    Q_INVOKABLE bool imageSupportsCcRpi();

    Q_INVOKABLE QString crypt(const QByteArray &password);
    Q_INVOKABLE QString pbkdf2(const QByteArray &psk, const QByteArray &ssid);

    Q_INVOKABLE QStringList getTranslations();
    Q_INVOKABLE QString getCurrentLanguage();
    Q_INVOKABLE QString getCurrentKeyboard();
    Q_INVOKABLE QString getCurrentUser();
    Q_INVOKABLE void changeLanguage(const QString &newLanguageName);
    Q_INVOKABLE void changeKeyboard(const QString &newKeymapLayout);
    Q_INVOKABLE bool customRepo();
    Q_INVOKABLE QString customRepoHost();
    
    /* Validate if a string is a valid repository URL (http/https ending with .json or .rpi-imager-manifest) */
    Q_INVOKABLE bool isValidRepoUrl(const QString &url) const;
    
    // Secure Boot CLI override
    static void setForceSecureBootEnabled(bool enabled);
    Q_INVOKABLE bool isSecureBootForcedByCliFlag() const;

    void replaceTranslator(QTranslator *trans);
    QString detectPiKeyboard();
    Q_INVOKABLE bool hasMouse();
    Q_INVOKABLE void reboot();
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE bool isScreenReaderActive() const;
    Q_INVOKABLE void handleIncomingUrl(const QUrl &url);
    Q_INVOKABLE void overwriteConnectToken(const QString &token);
    Q_INVOKABLE QString getRuntimeConnectToken() const;
    Q_INVOKABLE bool verifyAuthKey(const QString &token, bool strict = false) const;
    Q_INVOKABLE void clearConnectToken();
    
    /* Override OS list refresh schedule (in minutes); pass negative to clear override */
    Q_INVOKABLE void setOsListRefreshOverride(int intervalMinutes, int jitterMinutes);

    Q_INVOKABLE void refreshOsListFrom(const QUrl &url);
    Q_INVOKABLE void refreshOsListFromDefaultUrl();

    /* Elevatable bundle (e.g., AppImage) privilege escalation support */
    Q_INVOKABLE bool isElevatableBundle();
    Q_INVOKABLE bool hasElevationPolicyInstalled();
    Q_INVOKABLE bool installElevationPolicy();
    Q_INVOKABLE void restartWithElevatedPrivileges();

    /* Check if audio notification (beep) is available on this system */
    Q_INVOKABLE bool isBeepAvailable();

    /* Performance data export - opens native save dialog and writes performance data to file.
       If native dialogs aren't available, emits performanceSaveDialogNeeded for QML fallback. */
    Q_INVOKABLE bool exportPerformanceData();
    
    /* Export performance data to a specific file path (used by QML fallback) */
    Q_INVOKABLE bool exportPerformanceDataToFile(const QString &filePath);
    
    /* Get suggested filename for performance data export */
    Q_INVOKABLE QString getPerformanceDataFilename();
    
    /* Check if performance data is available */
    Q_INVOKABLE bool hasPerformanceData();

    /* Check if OS list is unavailable - derived from whether we have data (for QML offline UI) */
    bool isOsListUnavailable() const { return _completeOsList.isEmpty(); }

    /* Get access to performance stats for instrumentation */
    PerformanceStats* performanceStats() { return _performanceStats; }

signals:
    /* We are emiting signals with QVariant as parameters because QML likes it that way */

    void downloadProgress(QVariant dlnow, QVariant dltotal);
    void writeProgress(QVariant now, QVariant total);
    void verifyProgress(QVariant now, QVariant total);
    void error(QVariant msg);
    void success();
    void fileSelected(QVariant filename);
    void cancelled();
    void finalizing();
    void networkOnline();
    void preparationStatusUpdate(QVariant msg);
    void osListPrepared();
    void bottleneckStatusChanged(QVariant status, QVariant throughputKBps);
    void operationWarning(QVariant message);  // Non-fatal warning during operation (e.g., sync fallback)
    void hwFilterChanged();
    void networkInfo(QVariant msg);
    void cacheVerificationStarted();
    void cacheVerificationFinished();
    void selectedDeviceRemoved();
    void writeCancelledDueToDeviceRemoval();
    void keychainPermissionRequested();
    void keychainPermissionResponseReceived();
    void writeStateChanged();
    void connectTokenReceived(const QString &token);
    void connectTokenConflictDetected(const QString &token);
    void connectTokenCleared();
    void repositoryUrlReceived(const QString &url);
    void customRepoChanged();
    void customRepoHostChanged();  // Emitted when displayed repo host changes (e.g., after redirect)
    void cacheStatusChanged();
    void osListUnavailableChanged();
    void permissionWarning(QVariant msg);
    void locationPermissionGranted();
    void performanceSaveDialogNeeded(const QString &suggestedFilename, const QString &initialDir);

protected slots:
    void startProgressPolling();
    void stopProgressPolling();
    void pollNetwork();
    void restartWrite(QString reason);  // Restart write in sync mode after stall
    
    void onSuccess();
    void onError(QString msg);
    void onFileSelected(QString filename);
    void onCancelled();
    void onFinalizing();
    void onPreparationStatusUpdate(QString msg);
    void onOsListFetchComplete(const QByteArray &data, const QUrl &url, const QUrl &effectiveUrl);
    void onOsListFetchError(const QString &errorMessage, const QUrl &url);
    void onNetworkConnectionStats(const QString &statsMetadata, const QUrl &url);
    void onSTPdetected();
    void onCacheVerificationProgress(qint64 bytesProcessed, qint64 totalBytes);
    void onCacheVerificationComplete(bool isValid);
    void onSelectedDeviceRemoved(const QString &device);
    void onOsListRefreshTimeout();

private:
    void setWriteState(WriteState state);
    WriteState writeState() const { return _writeState; }
    // Cache management
    CacheManager* _cacheManager;
    bool _waitingForCacheVerification;
    QElapsedTimer _cacheVerificationTimer;  // Tracks cache verification duration
    
    // Keychain permission tracking
    bool _keychainPermissionGranted;
    bool _keychainPermissionReceived;

    // Recursively walk all the entries with subitems and, for any which
    // refer to an external JSON list, fetch the list and put it in place.
    void fillSubLists(QJsonArray &topLevel);
    void queueSublistFetches(const QJsonArray &list, int depth);
    QHash<QUrl, qint64> _pendingFetchStartTimes;  // Track request start times for performance
    QJsonDocument _completeOsList;
    QJsonArray _deviceFilter, _hwCapabilities, _swCapabilities;
    bool _deviceFilterIsInclusive;
    std::shared_ptr<DeviceInfo> _device_info;

    QString parseTokenFromUrl(const QUrl &url, bool strictAuthKey = false) const;

protected:
    QUrl _src, _repo;
    QString _dst, _parentCategory, _osName, _osReleaseDate, _currentLang, _currentLangcode, _currentKeyboard;
    QByteArray _expectedHash, _cmdline, _config, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat;
    ImageOptions::AdvancedOptions _advancedOptions;
    quint64 _downloadLen, _extrLen, _devLen, _dlnow, _verifynow;
    DriveListModel _drivelist;
    bool _selectedDeviceValid;
    WriteState _writeState;
    bool _cancelledDueToDeviceRemoval;
    HWListModel _hwlist;
    OSListModel _oslist;
    QQmlApplicationEngine *_engine;
    QTimer _networkchecktimer;
    QTimer _osListRefreshTimer;
    SuspendInhibitor *_suspendInhibitor;
    DownloadThread *_thread;
    bool _verifyEnabled, _multipleFilesInZip, _online, _extractSizeKnown;
    QSettings _settings;
    QMap<QString,QString> _translations;
    QTranslator *_trans;
    int _refreshIntervalOverrideMinutes;
    int _refreshJitterOverrideMinutes;
    // Session-only storage for Raspberry Pi Connect token
    QString _piConnectToken;
    // CLI flag to force enable secure boot regardless of OS capabilities
    static bool _forceSecureBootEnabled;
#ifndef CLI_ONLY_BUILD
    QWindow *_mainWindow;
#endif

    // Performance statistics capture
    PerformanceStats *_performanceStats;
    
    // Progress watchdog - separate component that monitors for stalls
    WriteProgressWatchdog* _progressWatchdog = nullptr;
    bool _forceSyncMode = false;  // Force sync I/O on next write (after recovery restart)
    
    // Debug options (secret menu)
    bool _debugDirectIO;
    bool _debugPeriodicSync;
    bool _debugVerboseLogging;
    bool _debugAsyncIO;
    int _debugAsyncQueueDepth;
    bool _debugIPv4Only;
    bool _debugSkipEndOfDevice;

    void _parseCompressedFile();
    void _parseXZFile();
    void _parseGzFile();
    QString _pubKeyFileName();
    QString _privKeyFileName();
    QString _sshKeyDir();
    QString _sshKeyGen();
    void _applySystemdCustomisationFromSettings(const QVariantMap &s);
    void _applyCloudInitCustomisationFromSettings(const QVariantMap &s);
    void _continueStartWriteAfterCacheVerification(bool cacheIsValid);
    void scheduleOsListRefresh();
    void _handleMemoryAllocationFailure(const char* what);
    void _handleSetupException(const char* what);
};

#endif // IMAGEWRITER_H
