/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QDebug>
#include <QTextStream>
#include "imagewriter.h"
#include "networkaccessmanagerfactory.h"
#include "cli.h"
#include "platformquirks.h"
#include <QMessageLogContext>
#include <QQuickWindow>
#include <QTranslator>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QFont>
#include <QFontDatabase>
#ifdef Q_OS_DARWIN
#include <CoreFoundation/CoreFoundation.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <winnls.h>
#endif

static QTextStream cerr(stderr);

/* Newer Qt versions throw warnings if using ::endl instead of Qt::endl
   Older versions lack Qt::endl */
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
//using Qt::endl;
#define endl  Qt::endl
#endif

#ifdef Q_OS_WIN
static void consoleMsgHandler(QtMsgType, const QMessageLogContext &, const QString &str) {
    cerr << str << endl;
}
#endif


int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cli") == 0)
        {
            /* CLI mode */
            Cli cli(argc, argv);
            return cli.run();
        }
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    /** QtQuick on QT5 exhibits spurious disk cache failures that cannot be
     * resolved by a user in a trivial manner (they have to delete the cache manually).
     *
     * This flag can potentially noticeably increase the start time of the application, however
     * between this and a hard-to-detect spurious failure affecting Linux, macOS and Windows,
     * this trade is the one most likely to result in a good experience for the widest group
     * of users.
     */
    qputenv("QML_DISABLE_DISK_CACHE", "true");
    
    // Disable virtual keyboard input method to prevent QtVirtualKeyboard dependency
    qputenv("QT_IM_MODULE", "");

#if QT_VERSION > QT_VERSION_CHECK(6, 5, 0)
    // In version 6.5, Qt implemented Google Material Design 3,
    // which renders fairly radically differently to Material Design 2.
    // Of particular note is the 'Normal' vs 'Dense' variant choice,
    // where 'Dense' is recommended for Desktops and environments with pointers.
    // See https://www.qt.io/blog/material-3-changes-in-qt-quick-controls
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
#endif

    // Apply platform-specific quirks and workarounds
    PlatformQuirks::applyQuirks();

    QGuiApplication app(argc, argv);
    
    app.setOrganizationName("Raspberry Pi");
    app.setOrganizationDomain("raspberrypi.org");
    app.setApplicationName("Imager");
    app.setWindowIcon(QIcon(":/icons/rpi-imager.ico"));
    
    // Create ImageWriter early to check embedded mode
    ImageWriter imageWriter;

#ifdef Q_OS_LINUX
    if (imageWriter.isEmbeddedMode()) {
        // Font and locale setup only needed for embedded Linux systems
        // Desktop systems have proper font fallbacks already configured
        
        /* Set default font - load embedded Roboto font */
        QStringList fontList = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(":/fonts/Roboto-Regular.ttf"));
        if (!fontList.isEmpty()) {
            QGuiApplication::setFont(QFont(fontList.first(), 10));
        }
        
        /* Add system fallback font if available (common on Linux systems) */
        if (QFile::exists("/usr/share/fonts/truetype/droid/DroidSansFallback.ttf")) {
            QFontDatabase::addApplicationFont("/usr/share/fonts/truetype/droid/DroidSansFallback.ttf");
        }

        /* Set default locale for embedded systems that might not have proper locale detection */
        QLocale::Language l = QLocale::system().language();
        if (l == QLocale::AnyLanguage || l == QLocale::C) {
            QLocale::setDefault(QLocale("en"));
        }
        
        qDebug() << "Embedded mode detected. System locale:" << QLocale::system().name();
    }
#endif
    NetworkAccessManagerFactory namf;
    QQmlApplicationEngine engine;
    QString customQm;
    QSettings settings;

    /* Parse commandline arguments (if any) */
    QString customRepo;
    QUrl url;
    QStringList args = app.arguments();
    for (int i=1; i < args.size(); i++)
    {
        if (!args[i].startsWith("-") && url.isEmpty())
        {
            if (args[i].startsWith("http:", Qt::CaseInsensitive) || args[i].startsWith("https:", Qt::CaseInsensitive))
            {
                url = args[i];
            }
            else
            {
                QFileInfo fi(args[i]);

                if (fi.isFile())
                {
                    url = QUrl::fromLocalFile(args[i]);
                }
                else
                {
                    cerr << "Argument ignored because it is not a regular file: " << args[i] << endl;;
                }
            }
        }
        else if (args[i] == "--repo")
        {
            if (args.size()-i < 2 || args[i+1].startsWith("-"))
            {
                cerr << "Missing URL after --repo" << endl;
                return 1;
            }

            customRepo = args[++i];
            if (customRepo.startsWith("http://") || customRepo.startsWith("https://"))
            {
                imageWriter.setCustomOsListUrl(customRepo);
            }
            else
            {
                QFileInfo fi(customRepo);
                if (!fi.isFile())
                {
                    cerr << "Custom repository file does not exist or is not a regular file: " << customRepo << endl;
                    return 1;
                }

                imageWriter.setCustomOsListUrl(QUrl::fromLocalFile(customRepo));
            }
        }
        else if (args[i] == "--qm")
        {
            if (args.size()-i < 2 || args[i+1].startsWith("-"))
            {
                cerr << "Missing QM file after --qm" << endl;
                return 1;
            }
            customQm = args[++i];

            QFileInfo fi(customQm);
            if (!fi.isFile())
            {
                cerr << "Custom QM file does not exist or is not a regular file: " << customQm << endl;
                return 1;
            }
        }
        else if (args[i] == "--debug")
        {
#ifdef Q_OS_WIN
            /* Allocate console for debug messages on Windows */
            if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole())
            {
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);
                std::ios::sync_with_stdio();
                qInstallMessageHandler(consoleMsgHandler);
            }
#endif
        }
        else if (args[i] == "--help")
        {
            cerr << "rpi-imager [--debug] [--version] [--repo <repository URL>] [--qm <custom qm translation file>] [--disable-telemetry] [<image file to write>]" << endl;
            cerr << "-OR- rpi-imager --cli [--disable-verify] [--sha256 <expected hash>] [--debug] [--quiet] <image file to write> <destination drive device>" << endl;
            return 0;
        }
        else if (args[i] == "--version")
        {
            cerr << "rpi-imager version " << imageWriter.constantVersion() << endl;
            cerr << "Repository: " << imageWriter.constantOsListUrl().toString() << endl;
            return 0;
        }
        else if (args[i] == "--disable-telemetry")
        {
            cerr << "Disabled telemetry" << endl;
            settings.setValue("telemetry", false);
            settings.sync();
        }
        else if (args[i] == "--enable-telemetry")
        {
            cerr << "Using default telemetry setting" << endl;
            settings.remove("telemetry");
            settings.sync();
        }
        else
        {
            cerr << "Ignoring unknown argument: " << args[i] << endl;
        }
    }

    QTranslator *translator = new QTranslator;
    if (customQm.isEmpty())
    {
#ifdef Q_OS_DARWIN
        QString langcode = "en_GB";
        CFArrayRef prefLangs = CFLocaleCopyPreferredLanguages();
        if (CFArrayGetCount(prefLangs))
        {
            char buf[32] = {0};
            CFStringRef strRef = (CFStringRef) CFArrayGetValueAtIndex(prefLangs, 0);
            CFStringGetCString(strRef, buf, sizeof(buf), kCFStringEncodingUTF8);
            langcode = buf;
            langcode.replace('-', '_');
            qDebug() << "OSX most preferred language:" << langcode;
        }

        CFRelease(prefLangs);
        QLocale::setDefault(QLocale(langcode));
#elif defined(Q_OS_WIN)
        // Use Windows API to get the actual UI language preference
        // This fixes the issue where QLocale::system() returns wrong language
        // when multiple language packs are installed
        QString langcode = "en_US";
        LANGID langId = GetUserDefaultUILanguage();
        if (langId != 0)
        {
            WCHAR langName[LOCALE_NAME_MAX_LENGTH] = {0};
            if (LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), langName, 
                                LOCALE_NAME_MAX_LENGTH, 0) != 0)
            {
                langcode = QString::fromWCharArray(langName);
                langcode.replace('-', '_');
                qDebug() << "Windows UI language:" << langcode;
            }
        }
        QLocale::setDefault(QLocale(langcode));
#endif

        if (translator->load(QLocale(), "rpi-imager", "_", QLatin1String(":/i18n")))
            imageWriter.replaceTranslator(translator);
        else
            delete translator;
    }
    else
    {
        if (translator->load(customQm))
            imageWriter.replaceTranslator(translator);
        else
            delete translator;
    }

    if (!url.isEmpty())
        imageWriter.setSrc(url);
    imageWriter.setEngine(&engine);
    engine.setNetworkAccessManagerFactory(&namf);

    engine.setInitialProperties(QVariantMap{{"imageWriter", QVariant::fromValue(&imageWriter)}});
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/RpiImager/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    QObject *qmlwindow = engine.rootObjects().value(0);
    qmlwindow->connect(&imageWriter, SIGNAL(downloadProgress(QVariant,QVariant)), qmlwindow, SLOT(onDownloadProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(verifyProgress(QVariant,QVariant)), qmlwindow, SLOT(onVerifyProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(preparationStatusUpdate(QVariant)), qmlwindow, SLOT(onPreparationStatusUpdate(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(error(QVariant)), qmlwindow, SLOT(onError(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(success()), qmlwindow, SLOT(onSuccess()));
    qmlwindow->connect(&imageWriter, SIGNAL(fileSelected(QVariant)), qmlwindow, SLOT(onFileSelected(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(cancelled()), qmlwindow, SLOT(onCancelled()));
    qmlwindow->connect(&imageWriter, SIGNAL(finalizing()), qmlwindow, SLOT(onFinalizing()));
    qmlwindow->connect(&imageWriter, SIGNAL(networkOnline()), qmlwindow, SLOT(fetchOSlist()));
    qmlwindow->connect(&imageWriter, SIGNAL(osListPrepared()), qmlwindow, SLOT(onOsListPrepared()));
    qmlwindow->connect(&imageWriter, SIGNAL(networkInfo(QVariant)), qmlwindow, SLOT(onNetworkInfo(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(cacheVerificationStarted()), qmlwindow, SLOT(onCacheVerificationStarted()));
    qmlwindow->connect(&imageWriter, SIGNAL(cacheVerificationFinished()), qmlwindow, SLOT(onCacheVerificationFinished()));
    qmlwindow->connect(&imageWriter, SIGNAL(selectedDeviceRemoved()), qmlwindow, SLOT(onSelectedDeviceRemoved()));
    qmlwindow->connect(&imageWriter, SIGNAL(keychainPermissionRequested()), qmlwindow, SLOT(onKeychainPermissionRequested()));

    /* Set window position */
    auto screensize = app.primaryScreen()->geometry();
    int x = settings.value("x", -1).toInt();
    int y = settings.value("y", -1).toInt();
    int w = qmlwindow->property("width").toInt();
    int h = qmlwindow->property("height").toInt();

    if (x != -1 && y != -1)
    {
        if ( !app.screenAt(QPoint(x,y)) || !app.screenAt(QPoint(x+w,y+h)) )
        {
            qDebug() << "Not restoring saved window position as it falls outside any currently attached screen";
            x = y = -1;
        }
    }

    if (x == -1 || y == -1)
    {
        x = qMax(1, (screensize.width()-w)/2);
        y = qMax(1, (screensize.height()-h)/2);
    }

    qmlwindow->setProperty("x", x);
    qmlwindow->setProperty("y", y);

    if (imageWriter.isOnline())
        imageWriter.beginOSListFetch();

    int rc = app.exec();

    int newX = qmlwindow->property("x").toInt();
    int newY = qmlwindow->property("y").toInt();
    if (x != newX || y != newY)
    {
        settings.setValue("x", newX);
        settings.setValue("y", newY);
        settings.sync();
    }

    return rc;
}

