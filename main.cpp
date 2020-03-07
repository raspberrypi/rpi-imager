/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QDebug>
#include <QTextStream>
#include "imagewriter.h"
#include "drivelistmodel.h"
#include "networkaccessmanagerfactory.h"
#include "i18n.h"
#include <QtWidgets/QApplication>
#include <QMessageLogContext>
#include <QQuickWindow>

static QTextStream cerr(stderr);

#ifdef Q_OS_WIN
static void consoleMsgHandler(QtMsgType, const QMessageLogContext &, const QString &str) {
    cerr << str << endl;
}
#endif

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#ifdef Q_OS_WIN
    // prefer ANGLE (DirectX) over desktop OpenGL
    QApplication::setAttribute(Qt::AA_UseOpenGLES);
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("Raspberry Pi");
    app.setOrganizationDomain("raspberrypi.org");
    app.setApplicationName("Imager");
    app.setWindowIcon(QIcon(":/icons/rpi-imager.ico"));
    ImageWriter imageWriter;
    NetworkAccessManagerFactory namf;
    QQmlApplicationEngine engine;

    /* Add i18n */
    I18n i18n;
    QObject::connect(&i18n, &I18n::retransRequest, &engine, &QQmlApplicationEngine::retranslate);
    engine.rootContext()->setContextProperty("i18n", &i18n);

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
            cerr << args[0] << " [--debug] [--repo <repository URL>] [<image file to write>]" << endl;
            return 0;
        }
        else
        {
            cerr << "Ignoring unknown argument: " << args[i] << endl;
        }
    }

    if (!url.isEmpty())
        imageWriter.setSrc(url);
    imageWriter.setEngine(&engine);
    engine.setNetworkAccessManagerFactory(&namf);
    engine.rootContext()->setContextProperty("imageWriter", &imageWriter);
    engine.rootContext()->setContextProperty("driveListModel", imageWriter.getDriveList());
    engine.rootContext()->setContextProperty("i18n", &i18n);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    QObject *qmlwindow = engine.rootObjects().value(0);
    qmlwindow->connect(&imageWriter, SIGNAL(downloadProgress(QVariant,QVariant)), qmlwindow, SLOT(onDownloadProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(verifyProgress(QVariant,QVariant)), qmlwindow, SLOT(onVerifyProgress(QVariant,QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(error(QVariant)), qmlwindow, SLOT(onError(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(success()), qmlwindow, SLOT(onSuccess()));
    qmlwindow->connect(&imageWriter, SIGNAL(fileSelected(QVariant)), qmlwindow, SLOT(onFileSelected(QVariant)));
    qmlwindow->connect(&imageWriter, SIGNAL(cancelled()), qmlwindow, SLOT(onCancelled()));
    qmlwindow->connect(&imageWriter, SIGNAL(finalizing()), qmlwindow, SLOT(onFinalizing()));

    int rc = app.exec();
    return rc;
}

