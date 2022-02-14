/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "cli.h"
#include "imagewriter.h"
#include <iostream>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include "drivelistmodel.h"
#include "dependencies/drivelist/src/drivelist.hpp"

/* Message handler to discard qDebug() output if using cli (unless --debug is set) */
static void devnullMsgHandler(QtMsgType, const QMessageLogContext &, const QString &)
{
}

Cli::Cli(int &argc, char *argv[]) : QObject(nullptr)
{
#ifdef Q_OS_WIN
    /* Allocate console on Windows (only needed if compiled as GUI program) */
    if (::AttachConsole(ATTACH_PARENT_PROCESS) || ::AllocConsole())
    {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        std::ios::sync_with_stdio();
    }
#endif
    _app = new QCoreApplication(argc, argv);
    _app->setOrganizationName("Raspberry Pi");
    _app->setOrganizationDomain("raspberrypi.org");
    _app->setApplicationName("Imager");
    _imageWriter = new ImageWriter;
    connect(_imageWriter, &ImageWriter::success, this, &Cli::onSuccess);
    connect(_imageWriter, &ImageWriter::error, this, &Cli::onError);
    connect(_imageWriter, &ImageWriter::preparationStatusUpdate, this, &Cli::onPreparationStatusUpdate);
    connect(_imageWriter, &ImageWriter::downloadProgress, this, &Cli::onDownloadProgress);
    connect(_imageWriter, &ImageWriter::verifyProgress, this, &Cli::onVerifyProgress);
}

Cli::~Cli()
{
    delete _imageWriter;
    delete _app;
}

int Cli::main()
{
    QCommandLineParser parser;
    QCommandLineOption cli("cli");
    parser.addOption(cli);
    QCommandLineOption disableVerify("disable-verify", "Disable verification");
    parser.addOption(disableVerify);
    QCommandLineOption writeSystemDrive("enable-writing-system-drives", "Only use this if you know what you are doing");
    parser.addOption(writeSystemDrive);
    QCommandLineOption sha256Option("sha256", "Expected hash", "sha256", "");
    parser.addOption(sha256Option);
    QCommandLineOption debugOption("debug", "Output debug messages to console");
    parser.addOption(debugOption);
    QCommandLineOption quietOption("quiet", "Only write to console on error");
    parser.addOption(quietOption);

    parser.addPositionalArgument("src", "Image file/URL");
    parser.addPositionalArgument("dst", "Destination device");
    parser.process(*_app);

    const QStringList args = parser.positionalArguments();
    if (args.count() != 2)
    {
        std::cerr << "Usage: --cli [--disable-verify] [--sha256 <expected hash>] [--debug] [--quiet] <image file to write> <destination drive device>" << std::endl;
        return 1;
    }

    if (!parser.isSet(debugOption))
    {
        qInstallMessageHandler(devnullMsgHandler);
    }
    _quiet = parser.isSet(quietOption);

    if (args[0].startsWith("http:", Qt::CaseInsensitive) || args[0].startsWith("https:", Qt::CaseInsensitive))
    {
        _imageWriter->setSrc(args[0], 0, 0, parser.value(sha256Option).toLatin1() );
    }
    else
    {
        QFileInfo fi(args[0]);

        if (fi.isFile())
        {
            _imageWriter->setSrc(QUrl::fromLocalFile(args[0]), fi.size(), 0, parser.value(sha256Option).toLatin1() );
        }
        else if (!fi.exists())
        {
            std::cerr << "Error: source file does not exists" << std::endl;
            return 1;
        }
        else
        {
            std::cerr << "Error: source is not a regular file" << std::endl;
            return 1;
        }
    }

    if (parser.isSet(writeSystemDrive))
    {
        std::cerr << "WARNING: writing to system drives is enabled." << std::endl;
    }
    else
    {
        DriveListModel dlm;
        dlm.processDriveList(Drivelist::ListStorageDevices() );
        bool foundDrive = false;
        int numDrives = dlm.rowCount( QModelIndex() );

        for (int i = 0; i < numDrives; i++)
        {
            if (dlm.index(i, 0).data(dlm.deviceRole) == args[1])
            {
                foundDrive = true;
                break;
            }
        }

        if (!foundDrive)
        {
            std::cerr << "Destination drive is not in list of removable volumes. Choose one of the following:" << std::endl << std::endl;

            for (int i = 0; i < numDrives; i++)
            {
                QModelIndex idx = dlm.index(i, 0);
                QByteArray line = idx.data(dlm.deviceRole).toByteArray()+" ("+idx.data(dlm.descriptionRole).toByteArray()+")";

                std::cerr << line.constData() << std::endl;
            }

            std::cerr << std::endl << "Or use --enable-writing-system-drives to overrule." << std::endl;
            return 1;
        }
    }

    _imageWriter->setDst(args[1]);
    _imageWriter->setVerifyEnabled(!parser.isSet(disableVerify));

    /* Run startWrite() in event loop (otherwise calling _app->exit() on error does not work) */
    QTimer::singleShot(1, _imageWriter, &ImageWriter::startWrite);
    return _app->exec();
}

void Cli::onSuccess()
{
    if (!_quiet)
    {
        _clearLine();
        std::cerr << "Write successful." << std::endl;
    }
    _app->exit(0);
}

void Cli::_clearLine()
{
    /* Properly clearing line requires platform specific code.
       Just write some spaces for now, and return to beginning of line. */
    std::cerr << "                                          \r";
}

void Cli::onError(QVariant msg)
{
    QByteArray m = msg.toByteArray();

    if (!_quiet)
    {
        _clearLine();
    }
    std::cerr << "Error: " << m.constData() << std::endl;
    _app->exit(1);
}

void Cli::onDownloadProgress(QVariant dlnow, QVariant dltotal)
{
    _printProgress("Writing",  dlnow, dltotal);
}

void Cli::onVerifyProgress(QVariant now, QVariant total)
{
    _printProgress("Verifying", now, total);
}

void Cli::onPreparationStatusUpdate(QVariant msg)
{
    if (!_quiet)
    {
        QByteArray ascii = QByteArray("  ")+msg.toByteArray()+"\r";
        _clearLine();
        std::cerr << ascii.constData();
    }
}

void Cli::_printProgress(const QByteArray &msg, QVariant now, QVariant total)
{
    if (_quiet)
        return;

    float n = now.toFloat();
    float t = total.toFloat();

    if (t)
    {
        int percent = n/t*100;
        if (percent != _lastPercent || msg != _lastMsg)
        {
            QByteArray txt = QByteArray("  ")+msg+": ["+QByteArray(percent/5, '-')+'>'+QByteArray(20-percent/5, ' ')+"] "+QByteArray::number(percent)+" %\r";
            std::cerr << txt.constData();
            _lastPercent = percent;
            _lastMsg = msg;
        }
    }
    else if (msg != _lastMsg)
    {
        std::cerr << msg.constData() << "\r";
        _lastMsg = msg;
    }
}
