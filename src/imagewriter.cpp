/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "imagewriter.h"
#include "drivelistitem.h"
#include "downloadextractthread.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "driveformatthread.h"
#include "localfileextractthread.h"
#include "downloadstatstelemetry.h"
#include <archive.h>
#include <archive_entry.h>
#include <random>
#include <QFileInfo>
#include <QQmlApplicationEngine>
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
#include <QVersionNumber>
#ifndef QT_NO_WIDGETS
#include <QFileDialog>
#include <QApplication>
#endif
#ifdef Q_OS_DARWIN
#include <QtNetwork>
#include <QMessageBox>
#include <security/security.h>
#else
#include "openssl/evp.h"
#include "openssl/sha.h"
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#include <wlanapi.h>
#ifndef WLAN_PROFILE_GET_PLAINTEXT_KEY
#define WLAN_PROFILE_GET_PLAINTEXT_KEY 4
#endif

#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#endif

#ifdef QT_NO_WIDGETS
#include <QtPlatformHeaders/QEglFSFunctions>
#endif

ImageWriter::ImageWriter(QObject *parent)
    : QObject(parent), _repo(QUrl(QString(OSLIST_URL))), _dlnow(0), _verifynow(0),
      _engine(nullptr), _thread(nullptr), _verifyEnabled(false), _cachingEnabled(false),
      _embeddedMode(false), _online(false), _trans(nullptr)
{
    connect(&_polltimer, SIGNAL(timeout()), SLOT(pollProgress()));

    QString platform;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) )
    {
        platform = QGuiApplication::platformName();
    }
    else
    {
        platform = "cli";
    }

    if (platform == "eglfs" || platform == "linuxfb")
    {
        _embeddedMode = true;
        connect(&_networkchecktimer, SIGNAL(timeout()), SLOT(pollNetwork()));
        _networkchecktimer.start(100);
        changeKeyboard(detectPiKeyboard());
        if (_currentKeyboard.isEmpty())
            _currentKeyboard = "us";

        QFile f("/sys/bus/nvmem/devices/rmem0/nvmem");
        if (f.exists() && f.open(f.ReadOnly))
        {
            const QByteArrayList eepromSettings = f.readAll().split('\n');
            f.close();
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

#ifdef Q_OS_WIN
    _taskbarButton = nullptr;
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

    _settings.beginGroup("caching");
    _cachingEnabled = !_embeddedMode && _settings.value("enabled", IMAGEWRITER_ENABLE_CACHE_DEFAULT).toBool();
    _cachedFileHash = _settings.value("lastDownloadSHA256").toByteArray();
    _cacheFileName = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QDir::separator()+"lastdownload.cache";
    if (!_cachedFileHash.isEmpty())
    {
        QFileInfo f(_cacheFileName);
        if (!f.exists() || !f.isReadable() || !f.size())
        {
            _cachedFileHash.clear();
            _settings.remove("lastDownloadSHA256");
            _settings.sync();
        }
    }
    _settings.endGroup();

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
        if (isEmbeddedMode() && langcode == "zh")
            continue;

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
}

ImageWriter::~ImageWriter()
{
    if (_trans)
    {
        QCoreApplication::removeTranslator(_trans);
        delete _trans;
    }
}

void ImageWriter::setEngine(QQmlApplicationEngine *engine)
{
    _engine = engine;
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
        _initFormat = "auto";
    }
}

/* Set device to write to */
void ImageWriter::setDst(const QString &device, quint64 deviceSize)
{
    _dst = device;
    _devLen = deviceSize;
}

/* Returns true if src and dst are set */
bool ImageWriter::readyToWrite()
{
    return !_src.isEmpty() && !_dst.isEmpty();
}

/* Start writing */
void ImageWriter::startWrite()
{
    if (!readyToWrite())
        return;

    if (_src.toString() == "internal://format")
    {
        DriveFormatThread *dft = new DriveFormatThread(_dst.toLatin1(), this);
        connect(dft, SIGNAL(success()), SLOT(onSuccess()));
        connect(dft, SIGNAL(error(QString)), SLOT(onError(QString)));
        dft->start();
        return;
    }

    QByteArray urlstr = _src.toString(_src.FullyEncoded).toLatin1();
    QString lowercaseurl = urlstr.toLower();
    bool compressed = lowercaseurl.endsWith(".zip") || lowercaseurl.endsWith(".xz") || lowercaseurl.endsWith(".bz2") || lowercaseurl.endsWith(".gz") || lowercaseurl.endsWith(".7z") || lowercaseurl.endsWith(".zst") || lowercaseurl.endsWith(".cache");
    if (!_extrLen && _src.isLocalFile())
    {
        if (!compressed)
            _extrLen = _downloadLen;
        else if (lowercaseurl.endsWith(".zip"))
            _parseCompressedFile();
    }

    if (_devLen && _extrLen > _devLen)
    {
        emit error(tr("Storage capacity is not large enough.<br>Needs to be at least %1 GB.").arg(QString::number(_extrLen/1000000000.0, 'f', 1)));
        return;
    }

    if (_extrLen && !_multipleFilesInZip && _extrLen % 512 != 0)
    {
        emit error(tr("Input file is not a valid disk image.<br>File size %1 bytes is not a multiple of 512 bytes.").arg(_extrLen));
        return;
    }

    if (!_expectedHash.isEmpty() && _cachedFileHash == _expectedHash)
    {
        // Use cached file
        urlstr = QUrl::fromLocalFile(_cacheFileName).toString(_src.FullyEncoded).toLatin1();
    }

    if (QUrl(urlstr).isLocalFile())
    {
        _thread = new LocalFileExtractThread(urlstr, _dst.toLatin1(), _expectedHash, this);
    }
    else if (compressed)
    {
        _thread = new DownloadExtractThread(urlstr, _dst.toLatin1(), _expectedHash, this);
        if (_repo.toString() == OSLIST_URL)
        {
            DownloadStatsTelemetry *tele = new DownloadStatsTelemetry(urlstr, _parentCategory.toLatin1(), _osName.toLatin1(), _embeddedMode, _currentLangcode, this);
            connect(tele, SIGNAL(finished()), tele, SLOT(deleteLater()));
            tele->start();
        }
    }
    else
    {
        _thread = new DownloadThread(urlstr, _dst.toLatin1(), _expectedHash, this);
        _thread->setInputBufferSize(IMAGEWRITER_UNCOMPRESSED_BLOCKSIZE);
    }

    connect(_thread, SIGNAL(success()), SLOT(onSuccess()));
    connect(_thread, SIGNAL(error(QString)), SLOT(onError(QString)));
    connect(_thread, SIGNAL(finalizing()), SLOT(onFinalizing()));
    connect(_thread, SIGNAL(preparationStatusUpdate(QString)), SLOT(onPreparationStatusUpdate(QString)));
    _thread->setVerifyEnabled(_verifyEnabled);
    _thread->setUserAgent(QString("Mozilla/5.0 rpi-imager/%1").arg(constantVersion()).toUtf8());
    _thread->setImageCustomization(_config, _cmdline, _firstrun, _cloudinit, _cloudinitNetwork, _initFormat);

    if (!_expectedHash.isEmpty() && _cachedFileHash != _expectedHash && _cachingEnabled)
    {
        if (!_cachedFileHash.isEmpty())
        {
            if (_settings.isWritable() && QFile::remove(_cacheFileName))
            {
                _settings.remove("caching/lastDownloadSHA256");
                _settings.sync();
                _cachedFileHash.clear();
            }
            else
            {
                qDebug() << "Error removing old cache file. Disabling caching";
                _cachingEnabled = false;
            }
        }

        if (_cachingEnabled)
        {
            QStorageInfo si(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
            qint64 avail = si.bytesAvailable();
            qDebug() << "Available disk space for caching:" << avail/1024/1024/1024 << "GB";

            if (avail-_downloadLen < IMAGEWRITER_MINIMAL_SPACE_FOR_CACHING)
            {
                qDebug() << "Low disk space. Not caching files to disk.";
            }
            else
            {
                _thread->setCacheFile(_cacheFileName, _downloadLen);
                connect(_thread, SIGNAL(cacheFileUpdated(QByteArray)), SLOT(onCacheFileUpdated(QByteArray)));
            }
        }
    }

    if (_multipleFilesInZip)
    {
        static_cast<DownloadExtractThread *>(_thread)->enableMultipleFileExtraction();
        DriveFormatThread *dft = new DriveFormatThread(_dst.toLatin1(), this);
        connect(dft, SIGNAL(success()), _thread, SLOT(start()));
        connect(dft, SIGNAL(error(QString)), SLOT(onError(QString)));
        dft->start();
    }
    else
    {
        _thread->start();
    }

    startProgressPolling();
}

void ImageWriter::onCacheFileUpdated(QByteArray sha256)
{
    _settings.setValue("caching/lastDownloadSHA256", sha256);
    _settings.sync();
    _cachedFileHash = sha256;
    qDebug() << "Done writing cache file";
}

/* Cancel write */
void ImageWriter::cancelWrite()
{
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

void ImageWriter::onCancelled()
{
    sender()->deleteLater();
    if (sender() == _thread)
    {
        _thread = nullptr;
    }
    emit cancelled();
}

/* Return true if url is in our local disk cache */
bool ImageWriter::isCached(const QUrl &, const QByteArray &sha256)
{
    return !sha256.isEmpty() && _cachedFileHash == sha256;
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

/* Start polling the list of available drives */
void ImageWriter::startDriveListPolling()
{
    _drivelist.startPolling();
}

/* Stop polling the list of available drives */
void ImageWriter::stopDriveListPolling()
{
    _drivelist.stopPolling();
}

DriveListModel *ImageWriter::getDriveList()
{
    return &_drivelist;
}

void ImageWriter::startProgressPolling()
{
    _powersave.applyBlock(tr("Downloading and writing image"));
#ifdef Q_OS_WIN
    if (!_taskbarButton && _engine)
    {
        QWindow* window = qobject_cast<QWindow*>( _engine->rootObjects().at(0) );
        if (window)
        {
            _taskbarButton = new QWinTaskbarButton(this);
            _taskbarButton->setWindow(window);
            _taskbarButton->progress()->setMaximum(0);
            _taskbarButton->progress()->setVisible(true);
        }
    }
#endif
    _dlnow = 0; _verifynow = 0;
    _polltimer.start(PROGRESS_UPDATE_INTERVAL);
}

void ImageWriter::stopProgressPolling()
{
    _polltimer.stop();
    pollProgress();
#ifdef Q_OS_WIN
    if (_taskbarButton)
    {
        _taskbarButton->progress()->setVisible(false);
        _taskbarButton->deleteLater();
        _taskbarButton = nullptr;
    }
#endif
    _powersave.removeBlock();
}

void ImageWriter::pollProgress()
{
    if (!_thread)
        return;

    quint64 newDlNow, dlTotal;
    if (_extrLen)
    {
        newDlNow = _thread->bytesWritten();
        dlTotal = _extrLen;
    }
    else
    {
        newDlNow = _thread->dlNow();
        dlTotal = _thread->dlTotal();
    }

    if (newDlNow != _dlnow)
    {
        _dlnow = newDlNow;
#ifdef Q_OS_WIN
        if (_taskbarButton)
        {
            _taskbarButton->progress()->setMaximum(dlTotal/1048576);
            _taskbarButton->progress()->setValue(newDlNow/1048576);
        }
#endif
        emit downloadProgress(newDlNow, dlTotal);
    }

    quint64 newVerifyNow = _thread->verifyNow();

    if (newVerifyNow != _verifynow)
    {
        _verifynow = newVerifyNow;
        quint64 verifyTotal = _thread->verifyTotal();
#ifdef Q_OS_WIN
        if (_taskbarButton)
        {
            _taskbarButton->progress()->setMaximum(verifyTotal/1048576);
            _taskbarButton->progress()->setValue(newVerifyNow/1048576);
        }
#endif
        emit verifyProgress(newVerifyNow, verifyTotal);
    }
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
    stopProgressPolling();
    emit success();

#ifndef QT_NO_WIDGETS
    if (_settings.value("beep").toBool() && qobject_cast<QApplication*>(QCoreApplication::instance()) )
    {
        QApplication::beep();
    }
#endif
}

void ImageWriter::onError(QString msg)
{
    stopProgressPolling();
    emit error(msg);

#ifndef QT_NO_WIDGETS
    if (_settings.value("beep").toBool() && qobject_cast<QApplication*>(QCoreApplication::instance()) )
        QApplication::beep();
#endif
}

void ImageWriter::onFinalizing()
{
    _polltimer.stop();
    emit finalizing();
}

void ImageWriter::onPreparationStatusUpdate(QString msg)
{
    emit preparationStatusUpdate(msg);
}

void ImageWriter::openFileDialog()
{
#ifndef QT_NO_WIDGETS
    QSettings settings;
    QString path = settings.value("lastpath").toString();
    QFileInfo fi(path);

    if (path.isEmpty() || !fi.exists() || !fi.isReadable() )
        path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    QFileDialog *fd = new QFileDialog(nullptr, tr("Select image"),
                                      path,
                                      "Image files (*.img *.zip *.iso *.gz *.xz *.zst);;All files (*.*)");
    connect(fd, SIGNAL(fileSelected(QString)), SLOT(onFileSelected(QString)));

    if (_engine)
    {
        fd->createWinId();
        QWindow *handle = fd->windowHandle();
        QWindow *qmlwindow = qobject_cast<QWindow *>(_engine->rootObjects().value(0));
        if (qmlwindow)
        {
            handle->setTransientParent(qmlwindow);
        }
    }

    fd->show();
#endif
}

void ImageWriter::onFileSelected(QString filename)
{
#ifndef QT_NO_WIDGETS
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

    sender()->deleteLater();
#endif
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

bool ImageWriter::isOnline()
{
    return _online || !_embeddedMode;
}

void ImageWriter::pollNetwork()
{
#ifdef Q_OS_LINUX
    /* Check if we have an IP-address other than localhost */
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();

    foreach (QHostAddress a, addresses)
    {
        if (!a.isLoopback() && a.scopeId().isEmpty())
        {
            /* Not a loopback or IPv6 link-local address, so online */
            qDebug() << "IP:" << a;
            _online = true;
            break;
        }
    }

    if (_online)
    {
        _networkchecktimer.stop();

        // Wait another 0.1 sec, as dhcpcd may not have set up nameservers yet
        QTimer::singleShot(100, this, SLOT(syncTime()));
    }
#endif
}

void ImageWriter::syncTime()
{
#ifdef Q_OS_LINUX
    qDebug() << "Network online. Synchronizing time.";
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply*)), SLOT(onTimeSyncReply(QNetworkReply*)));
    manager->head(QNetworkRequest(QUrl(TIME_URL)));
#endif
}

void ImageWriter::onTimeSyncReply(QNetworkReply *reply)
{
#ifdef Q_OS_LINUX
    if (reply->hasRawHeader("Date"))
    {
        QDateTime dt = QDateTime::fromString(reply->rawHeader("Date"), Qt::RFC2822Date);
        qDebug() << "Received current time from server:" << dt;
        struct timeval tv = {
            (time_t) dt.toSecsSinceEpoch(), 0
        };
        ::settimeofday(&tv, NULL);

        emit networkOnline();
    }
    else
    {
        qDebug() << "Error synchronizing time. Trying again in 3 seconds";
        QTimer::singleShot(3000, this, SLOT(syncTime()));
    }

    reply->deleteLater();
#else
    Q_UNUSED(reply)
#endif
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
    QStringList namefilters = {"*.img", "*.zip", "*.gz", "*.xz", "*.zst"};

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

QString ImageWriter::getDefaultPubKey()
{
    QByteArray pubkey;
    QFile pubfile(QDir::homePath()+"/.ssh/id_rsa.pub");

    if (pubfile.exists() && pubfile.open(QFile::ReadOnly))
    {
        pubkey = pubfile.readAll().trimmed();
        pubfile.close();
    }

    return pubkey;
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
    /* Qt used to have proper bearer management that was able to provide a list of
       SSIDs, but since they retired it, resort to calling platform specific tools for now.
       Minimal implementation that only gets the currently connected SSID */

    QString program, regexpstr, ssid;
    QStringList args;
    QProcess proc;

#ifdef Q_OS_WIN
    program = "netsh";
    args << "wlan" << "show" << "interfaces";
    regexpstr = "[ \t]+SSID[ \t]*: (.+)";
#else
#ifdef Q_OS_DARWIN
    program = "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport";
    args << "-I";
    regexpstr = "[ \t]+SSID: (.+)";
#else
    program = "iwgetid";
    args << "-r";
#endif
#endif

    proc.start(program, args);
    if (proc.waitForStarted(2000) && proc.waitForFinished(2000))
    {
        if (regexpstr.isEmpty())
        {
            ssid = proc.readAll().trimmed();
        }
        else
        {
            QRegularExpression rx(regexpstr);
            const QList<QByteArray> outputlines = proc.readAll().replace('\r', "").split('\n');

            for (const QByteArray &line : outputlines) {
                QRegularExpressionMatch match = rx.match(line);
                if (match.hasMatch())
                {
                    ssid = match.captured(1);
                    break;
                }
            }
        }
    }

    return ssid;
}

QString ImageWriter::getPSK(const QString &ssid)
{
#ifdef Q_OS_WIN
    /* Windows allows retrieving wifi PSK */
    HANDLE h;
    DWORD ret = 0;
    DWORD supportedVersion = 0;
    DWORD clientVersion = 2;
    QString psk;

    if (WlanOpenHandle(clientVersion, NULL, &supportedVersion, &h) != ERROR_SUCCESS)
        return QString();

    PWLAN_INTERFACE_INFO_LIST ifList = NULL;

    if (WlanEnumInterfaces(h, NULL, &ifList) == ERROR_SUCCESS)
    {
        for (int i=0; i < ifList->dwNumberOfItems; i++)
        {
            PWLAN_PROFILE_INFO_LIST profileList = NULL;

            if (WlanGetProfileList(h, &ifList->InterfaceInfo[i].InterfaceGuid,
                                   NULL, &profileList) == ERROR_SUCCESS)
            {
                for (int j=0; j < profileList->dwNumberOfItems; j++)
                {
                    QString s = QString::fromWCharArray(profileList->ProfileInfo[j].strProfileName);
                    qDebug() << "Enumerating wifi profiles, SSID found:" << s << " looking for:" << ssid;

                    if (s == ssid) {
                        DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                        DWORD access = 0;
                        DWORD ret = 0;
                        LPWSTR xmlstr = NULL;

                        if ( (ret = WlanGetProfile(h, &ifList->InterfaceInfo[i].InterfaceGuid, profileList->ProfileInfo[j].strProfileName,
                                          NULL, &xmlstr, &flags, &access)) == ERROR_SUCCESS && xmlstr)
                        {
                            QString xml = QString::fromWCharArray(xmlstr);
                            qDebug() << "XML wifi profile:" << xml;
                            QRegularExpression rx("<keyMaterial>(.+)</keyMaterial>");
                            QRegularExpressionMatch match = rx.match(xml);

                            if (match.hasMatch()) {
                                psk = match.captured(1);
                            }

                            WlanFreeMemory(xmlstr);
                            break;
                        }
                    }
                }
            }

            if (profileList) {
                WlanFreeMemory(profileList);
            }
        }
    }

    if (ifList)
        WlanFreeMemory(ifList);
    WlanCloseHandle(h, NULL);

    return psk;

#else
#ifdef Q_OS_DARWIN
    SecKeychainRef keychainRef;
    QString psk;
    QByteArray ssidAscii = ssid.toLatin1();

    if (QMessageBox::question(nullptr, "",
                          tr("Would you like to prefill the wifi password from the system keychain?")) == QMessageBox::Yes)
    {
        if (SecKeychainOpen("/Library/Keychains/System.keychain", &keychainRef) == errSecSuccess)
        {
            UInt32 resultLen;
            void *result;
            if (SecKeychainFindGenericPassword(keychainRef, 0, NULL, ssidAscii.length(), ssidAscii.constData(), &resultLen, &result, NULL) == errSecSuccess)
            {
                psk = QByteArray((char *) result, resultLen);
                SecKeychainItemFreeContent(NULL, result);
            }
            CFRelease(keychainRef);
        }
    }

    return psk;
#else
    Q_UNUSED(ssid)
    return QString();
#endif
#endif
}

bool ImageWriter::getBoolSetting(const QString &key)
{
    /* Some keys have defaults */
    if (key == "telemetry")
        return _settings.value(key, TELEMETRY_ENABLED_DEFAULT).toBool();
    else if (key == "eject")
        return _settings.value(key, true).toBool();
    else
        return _settings.value(key).toBool();
}

void ImageWriter::setSetting(const QString &key, const QVariant &value)
{
    _settings.setValue(key, value);
    _settings.sync();
}

void ImageWriter::setImageCustomization(const QByteArray &config, const QByteArray &cmdline, const QByteArray &firstrun, const QByteArray &cloudinit, const QByteArray &cloudinitNetwork)
{
    _config = config;
    _cmdline = cmdline;
    _firstrun = firstrun;
    _cloudinit = cloudinit;
    _cloudinitNetwork = cloudinitNetwork;

    qDebug() << "Custom config.txt entries:" << config;
    qDebug() << "Custom cmdline.txt entries:" << cmdline;
    qDebug() << "Custom firstuse.sh:" << firstrun;
    qDebug() << "Cloudinit:" << cloudinit;
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
    /* Qt has support for calculating Pbkdf2 starting from Qt 5.12 but
     * older Linux distributions may not have that.
     * We can use OpenSSL instead on platforms that have that.
     * But Mac OS X lacks that, so do use Qt there */

#ifdef Q_OS_DARWIN
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha1, psk, ssid, 4096, 32).toHex();
#else
    QByteArray digest(32, 0);
    PKCS5_PBKDF2_HMAC_SHA1(psk.constData(), psk.length(), (const unsigned char*) ssid.constData(), ssid.length(), 4096, digest.length(), (unsigned char *) digest.data());
    return digest.toHex();
#endif
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

    return result;
}

void ImageWriter::clearSavedCustomizationSettings()
{
    _settings.beginGroup("imagecustomization");
    _settings.remove("");
    _settings.endGroup();
}

bool ImageWriter::hasSavedCustomizationSettings()
{
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

#ifdef QT_NO_WIDGETS
    QString kmapfile = "/usr/share/qmaps/"+newKeymapLayout+".qmap";

    if (QFile::exists(kmapfile))
        QEglFSFunctions::loadKeymap(kmapfile);
#endif

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
        QRegularExpression rx("RPI_Wired_Keyboard_([0-9]+)");

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

bool ImageWriter::hasMouse()
{
    return !_embeddedMode || QFile::exists("/dev/input/mouse0");
}

bool ImageWriter::customRepo()
{
    return _repo.toString() != OSLIST_URL;
}

void MountUtilsLog(std::string msg) {
    Q_UNUSED(msg)
    //qDebug() << "mountutils:" << msg.c_str();
}
