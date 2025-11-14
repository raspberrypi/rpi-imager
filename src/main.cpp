/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QDebug>
#include <QTextStream>
#include <QMessageLogContext>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include <QCommandLineParser>
#include "cli.h"

#ifndef CLI_ONLY_BUILD
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include "imagewriter.h"
#include "networkaccessmanagerfactory.h"
#include "nativefiledialog.h"
#include <QQuickWindow>
#include <QScreen>
#include <QFont>
#include <QFontDatabase>
#include <QSessionManager>
#include <QFileOpenEvent>
#endif
#include "platformquirks.h"
#ifdef Q_OS_DARWIN
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#include <winnls.h>
#include <QTcpServer>
#include <QTcpSocket>
#endif
#include "imageadvancedoptions.h"

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

// If CMake didn't inject it for some reason, fall back to a sensible default.
#ifndef RPI_IMAGER_CALLBACK_PORT
#define RPI_IMAGER_CALLBACK_PORT 49629
#endif
static_assert(RPI_IMAGER_CALLBACK_PORT > 0 && RPI_IMAGER_CALLBACK_PORT <= 65535,
              "RPI_IMAGER_CALLBACK_PORT must be a valid TCP port");
static constexpr quint16 kPort =
    static_cast<quint16>(RPI_IMAGER_CALLBACK_PORT);
#endif


int main(int argc, char *argv[])
{
    // Apply platform-specific quirks and workarounds FIRST
    // This must happen before any Qt initialization (QCoreApplication/QGuiApplication)
    PlatformQuirks::applyQuirks();

#ifdef CLI_ONLY_BUILD
    /* Force CLI mode for CLI-only builds */
    Cli cli(argc, argv);
    return cli.run();
#else
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cli") == 0)
        {
            /* CLI mode */
            Cli cli(argc, argv);
            return cli.run();
        }
    }

    /* GUI mode - all the following code is GUI-specific */
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

    QGuiApplication app(argc, argv);
    
    app.setOrganizationName("Raspberry Pi");
    app.setOrganizationDomain("raspberrypi.com");
    app.setApplicationName("Raspberry Pi Imager");
    app.setApplicationVersion(ImageWriter::staticVersion());
    app.setWindowIcon(QIcon(":/icons/rpi-imager.ico"));

    qmlRegisterUncreatableMetaObject(
        ImageOptions::staticMetaObject, // from Q_NAMESPACE
        "ImageOptions",    // import name in qml
        1, 0,           // version
        "ImageOptions",    // QML type name
        "Namespace only"
    );
    
    // Create ImageWriter early to check embedded mode
    ImageWriter imageWriter;

    // Early check for elevated privileges on platforms that require them (Linux/Windows)
    bool hasPermissionIssue = false;
    QString permissionMessage;
    
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    if (!PlatformQuirks::hasElevatedPrivileges())
    {
        hasPermissionIssue = true;
        
        // Common message parts to reduce translation effort
        QString header = QObject::tr("Raspberry Pi Imager requires elevated privileges to write to storage devices.");
        QString footer = QObject::tr("Without this, you will encounter permission errors when writing images.");
        
#ifdef Q_OS_LINUX
        QString statusAndAction = QObject::tr(
            "You are not running as root.\n\n"
            "Please run with elevated privileges: sudo rpi-imager"
        );
#elif defined(Q_OS_WIN)
        QString statusAndAction = QObject::tr(
            "You are not running as Administrator.\n\n"
            "Please run as Administrator."
        );
#endif
        
        permissionMessage = QString("%1\n\n%2\n\n%3").arg(header, statusAndAction, footer);
        qWarning() << "Not running with elevated privileges - device access may fail";
    }
#endif

#ifdef Q_OS_DARWIN
    // Ensure our app is the default handler for rpi-imager:// scheme so Safari recognizes it
    {
        CFStringRef scheme = CFSTR("rpi-imager");
        CFBundleRef bundle = CFBundleGetMainBundle();
        if (bundle) {
            CFStringRef bundleId = (CFStringRef)CFBundleGetIdentifier(bundle);
            if (bundleId) {
                LSSetDefaultHandlerForURLScheme(scheme, bundleId);
            }
        }
    }
#endif
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
    bool enableLanguageSelection = false;
    QSettings settings;

    /* Parse commandline arguments (if any) using QCommandLineParser */
    QString customRepo;
    QUrl url;
    QUrl callbackUrl;
    int cliRefreshInterval = -1;
    int cliRefreshJitter = -1;

    QCommandLineParser parser;
    parser.setApplicationDescription("Raspberry Pi Imager GUI");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {"repo", "Custom OS list repository URL or local file", "url-or-file", ""},
        {"qm", "Custom translation .qm file", "file", ""},
        {"debug", "Output debug messages to console"},
        {"refresh-interval", "OS list refresh base interval (minutes)", "minutes", ""},
        {"refresh-jitter", "OS list refresh jitter (minutes)", "minutes", ""},
        {"enable-language-selection", "Show language selection on startup"},
        {"disable-telemetry", "Disable telemetry (persist setting)"},
        {"enable-telemetry", "Use default telemetry setting (clear override)"},
        {"qml-file-dialogs", "Force use of QML file dialogs instead of native dialogs"},
        {"enable-secure-boot", "Force enable secure boot customization step regardless of OS capabilities"}
    });

    parser.addPositionalArgument("image", "Image file/URL or rpi-imager:// callback URL (optional)", "[image]");
    parser.process(app);


    const QString repoVal = parser.value("repo");
    if (!repoVal.isEmpty())
    {
        customRepo = repoVal;
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

    const QString qmVal = parser.value("qm");
    if (!qmVal.isEmpty())
    {
        QFileInfo fi(qmVal);
        if (!fi.isFile())
        {
            cerr << "Custom QM file does not exist or is not a regular file: " << qmVal << endl;
            return 1;
        }
        customQm = qmVal;
    }

#ifdef Q_OS_WIN
    if (parser.isSet("debug"))
    {
        /* Attach to console for debug messages on Windows */
        PlatformQuirks::attachConsole();
        qInstallMessageHandler(consoleMsgHandler);
    }
#endif

    if (parser.isSet("refresh-interval"))
    {
        bool ok = false;
        int v = parser.value("refresh-interval").toInt(&ok);
        if (!ok || v < 0)
        {
            cerr << "Invalid value for --refresh-interval" << endl;
            return 1;
        }
        cliRefreshInterval = v;
    }

    if (parser.isSet("refresh-jitter"))
    {
        bool ok = false;
        int v = parser.value("refresh-jitter").toInt(&ok);
        if (!ok || v < 0)
        {
            cerr << "Invalid value for --refresh-jitter" << endl;
            return 1;
        }
        cliRefreshJitter = v;
    }

    enableLanguageSelection = parser.isSet("enable-language-selection");

    if (parser.isSet("disable-telemetry"))
    {
        cerr << "Disabled telemetry" << endl;
        settings.setValue("telemetry", false);
        settings.sync();
    }
    else if (parser.isSet("enable-telemetry"))
    {
        cerr << "Using default telemetry setting" << endl;
        settings.remove("telemetry");
        settings.sync();
    }

    if (parser.isSet("qml-file-dialogs"))
    {
        NativeFileDialog::setForceQmlDialogs(true);
    }

    if (parser.isSet("enable-secure-boot"))
    {
        ImageWriter::setForceSecureBootEnabled(true);
    }

    const QStringList posArgs = parser.positionalArguments();
    if (!posArgs.isEmpty())
    {
        const QString firstPos = posArgs.first();
        if (firstPos.startsWith("http:", Qt::CaseInsensitive) || firstPos.startsWith("https:", Qt::CaseInsensitive))
        {
            url = firstPos;
        }
        else if (firstPos.startsWith("rpi-imager:", Qt::CaseInsensitive))
        {
            callbackUrl = QUrl(firstPos);
        }
        else
        {
            QFileInfo fi(firstPos);
            if (fi.isFile())
            {
                url = QUrl::fromLocalFile(firstPos);
            }
            else
            {
                cerr << "Argument ignored because it is not a regular file: " << firstPos << endl;
            }
        }
    }

#ifdef Q_OS_WIN
    // callback server
    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        while (auto *s = server.nextPendingConnection()) {
            QObject::connect(s, &QTcpSocket::readyRead, s, [s, &imageWriter]() {
                const QByteArray payload = s->readAll();
                s->disconnectFromHost();
                QMetaObject::invokeMethod(
                    &imageWriter,
                    [payload, &imageWriter] {
                        imageWriter.handleIncomingUrl(QUrl(QString::fromUtf8(payload)));
                    },
                    Qt::QueuedConnection
                    );
            });
        }
    });
    if (!server.listen(QHostAddress::LocalHost, kPort)) {
        qWarning() << "TCP listen failed:" << server.errorString();
    }
#endif

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
        QString langcode = "en_GB";
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
    if (cliRefreshInterval >= 0 || cliRefreshJitter >= 0)
    {
        // Sanitize CLI overrides: enforce minimums when non-zero
        // Base interval min: 1 day (1440 minutes)
        // Jitter min: 3 hours (180 minutes)
        constexpr int MIN_BASE_MINUTES = 24 * 60;   // 1440
        constexpr int MIN_JITTER_MINUTES = 3 * 60;  // 180

        int sanitizedInterval = cliRefreshInterval;
        int sanitizedJitter = cliRefreshJitter;

        if (sanitizedInterval > 0 && sanitizedInterval < MIN_BASE_MINUTES)
            sanitizedInterval = MIN_BASE_MINUTES;
        if (sanitizedJitter > 0 && sanitizedJitter < MIN_JITTER_MINUTES)
            sanitizedJitter = MIN_JITTER_MINUTES;

        imageWriter.setOsListRefreshOverride(sanitizedInterval, sanitizedJitter);
    }
    imageWriter.setEngine(&engine);
    engine.setNetworkAccessManagerFactory(&namf);

    // Determine if we should show the language selection landing step
    // Consider language undetermined if QLocale::system() is AnyLanguage or C
    bool couldDetermineLanguage = true;
    {
        QLocale::Language sysLang = QLocale::system().language();
        if (sysLang == QLocale::AnyLanguage || sysLang == QLocale::C)
            couldDetermineLanguage = false;
    }
    const bool showLanguageSelection = enableLanguageSelection || !couldDetermineLanguage;

    engine.setInitialProperties(QVariantMap{
        {"imageWriter", QVariant::fromValue(&imageWriter)},
        {"showLanguageSelection", showLanguageSelection}
    });
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/RpiImager/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    QObject *qmlwindow = engine.rootObjects().value(0);
    qmlwindow->connect(&imageWriter, SIGNAL(downloadProgress(QVariant,QVariant)), qmlwindow, SLOT(onDownloadProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(verifyProgress(QVariant,QVariant)), qmlwindow, SLOT(onVerifyProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(preparationStatusUpdate(QVariant)), qmlwindow, SLOT(onPreparationStatusUpdate(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(error(QVariant)), qmlwindow, SLOT(onError(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(finalizing()), qmlwindow, SLOT(onFinalizing()));
    qmlwindow->connect(&imageWriter, SIGNAL(cancelled()), qmlwindow, SLOT(onCancelled()));
    // osListPrepared is handled by wizard OSSelection instead of main window
    qmlwindow->connect(&imageWriter, SIGNAL(networkInfo(QVariant)), qmlwindow, SLOT(onNetworkInfo(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(selectedDeviceRemoved()), qmlwindow, SLOT(onSelectedDeviceRemoved()));
    qmlwindow->connect(&imageWriter, SIGNAL(writeCancelledDueToDeviceRemoval()), qmlwindow, SLOT(onWriteCancelledDueToDeviceRemoval()));
    qmlwindow->connect(&imageWriter, SIGNAL(keychainPermissionRequested()), qmlwindow, SLOT(onKeychainPermissionRequested()));
    qmlwindow->connect(&imageWriter, SIGNAL(osListFetchFailed()), qmlwindow, SLOT(onOsListFetchFailed()));
    qmlwindow->connect(&imageWriter, SIGNAL(permissionWarning(QVariant)), qmlwindow, SLOT(onPermissionWarning(QVariant)));
#ifdef Q_OS_DARWIN
    // Handle custom URL scheme on macOS via FileOpen events
    struct UrlOpenFilter : public QObject {
        ImageWriter *iw;
        explicit UrlOpenFilter(ImageWriter *w) : iw(w) {}
        bool eventFilter(QObject *obj, QEvent *event) override {
            Q_UNUSED(obj)
            if (event->type() == QEvent::FileOpen) {
                QFileOpenEvent *foe = static_cast<QFileOpenEvent*>(event);
                if (foe && foe->url().isValid()) {
                    iw->handleIncomingUrl(foe->url());
                    return true;
                }
            }
            return false;
        }
    };
    app.installEventFilter(new UrlOpenFilter(&imageWriter));
#endif

    // If launched via custom URL scheme on Windows/Linux, deliver it now
    if (!callbackUrl.isEmpty()) {
        imageWriter.handleIncomingUrl(callbackUrl);
    }
    // Forward platform URL open events to QML via ImageWriter (no-ops, kept for future use)
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &imageWriter, [](Qt::ApplicationState){ /* no-op */ });
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &imageWriter, [](QSessionManager&){ /* no-op */ });

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

    // Only fetch OS list if we have network connectivity
    if (imageWriter.isOnline() && PlatformQuirks::hasNetworkConnectivity())
        imageWriter.beginOSListFetch();

    // Emit permission warning signal after UI is loaded so dialog can be shown
    if (hasPermissionIssue)
    {
        QMetaObject::invokeMethod(&imageWriter, [&imageWriter, permissionMessage]() {
            emit imageWriter.permissionWarning(permissionMessage);
        }, Qt::QueuedConnection);
    }

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
#endif /* !CLI_ONLY_BUILD */
}
