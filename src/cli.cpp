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

int Cli::run()
{
    QCommandLineParser parser;
    parser.addOptions({
        {"cli", ""},
        {"disable-verify", "Disable verification"},
        {"enable-writing-system-drives", "Only use this if you know what you are doing"},
        {"sha256", "Expected hash", "sha256", ""},
        {"cache-file", "Custom cache file (requires setting sha256 as well)", "cache-file", ""},
        {"first-run-script", "Add firstrun.sh to image", "first-run-script", ""},
        {"cloudinit-userdata", "Add cloud-init user-data file to image", "cloudinit-userdata", ""},
        {"cloudinit-networkconfig", "Add cloud-init network-config file to image", "cloudinit-networkconfig", ""},
        {"usb-ether-gadget", "Enable USB Ethernet Gadget mode (does not support --first-run-script)"},
        {"disable-eject", "Disable automatic ejection of storage media after verification"},
        {"debug", "Output debug messages to console"},
        {"quiet", "Only write to console on error"},
    });

    parser.addVersionOption();
    parser.addHelpOption();
    parser.addPositionalArgument("src", "Image file/URL");
    parser.addPositionalArgument("dst", "Destination device");
    parser.process(*_app);

    const QStringList args = parser.positionalArguments();
    if (args.count() != 2)
    {
        std::cerr << "Usage: --cli [--disable-verify] [--disable-eject] [--sha256 <expected hash> [--cache-file <cache file>]] [--first-run-script <script>] [--usb-ether-gadget] [--debug] [--quiet] <image file to write> <destination drive device>" << std::endl;
        return 1;
    }

    if (!parser.isSet("debug"))
    {
        qInstallMessageHandler(devnullMsgHandler);
    }
    _quiet = parser.isSet("quiet");
    QByteArray initFormat = (parser.value("cloudinit-userdata").isEmpty()
                             && parser.value("cloudinit-networkconfig").isEmpty() ) ? "systemd" : "cloudinit";

    if (args[0].startsWith("http:", Qt::CaseInsensitive) || args[0].startsWith("https:", Qt::CaseInsensitive))
    {
        _imageWriter->setSrc(args[0], 0, 0, parser.value("sha256").toLatin1(), false, "", "", initFormat);

        if (!parser.value("cache-file").isEmpty())
        {
            _imageWriter->setCustomCacheFile(parser.value("cache-file"), parser.value("sha256").toLatin1() );
        }
    }
    else
    {
        QFileInfo fi(args[0]);

        if (fi.isFile())
        {
            _imageWriter->setSrc(QUrl::fromLocalFile(args[0]), fi.size(), 0, parser.value("sha256").toLatin1(), false, "", "", initFormat);
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

    if (parser.isSet("enable-writing-system-drives"))
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

    bool isEtherGadgetEnabled = parser.isSet("usb-ether-gadget");

    if (!parser.value("cloudinit-userdata").isEmpty())
    {
        QByteArray userData, networkConfig;
        QFile f(parser.value("cloudinit-userdata"));

        if (!f.exists())
        {
            std::cerr << "Error: user-data file does not exists" << std::endl;
            return 1;
        }
        if (f.open(f.ReadOnly))
        {
            userData = f.readAll();
            f.close();
        }
        else
        {
            std::cerr << "Error: opening user-data file" << std::endl;
            return 1;
        }

        f.setFileName(parser.value("cloudinit-networkconfig"));
        if (!f.exists())
        {
            std::cerr << "Error: network-config file does not exists" << std::endl;
            return 1;
        }
        if (f.open(f.ReadOnly))
        {
            networkConfig = f.readAll();
            f.close();
        }
        else
        {
            std::cerr << "Error: opening network-config file" << std::endl;
            return 1;
        }

        _imageWriter->setImageCustomization("", "", "", userData, networkConfig, false, isEtherGadgetEnabled);
    }
    else if (!parser.value("first-run-script").isEmpty())
    {
        if (isEtherGadgetEnabled) {
            std::cerr << "Error: the --usb-ether-gadget option is not supported when --first-run-script is used.";
            return 1;
        }

        QByteArray firstRunScript;
        QFile f(parser.value("first-run-script"));
        if (!f.exists())
        {
            std::cerr << "Error: firstrun script does not exists" << std::endl;
            return 1;
        }
        if (f.open(f.ReadOnly))
        {
            firstRunScript = f.readAll();
            f.close();
        }
        else
        {
            std::cerr << "Error: opening firstrun script" << std::endl;
            return 1;
        }

        _imageWriter->setImageCustomization("", "", firstRunScript, "", "", true, isEtherGadgetEnabled);
    }

    _imageWriter->setDst(args[1]);
    _imageWriter->setVerifyEnabled(!parser.isSet("disable-verify"));
    _imageWriter->setEtherGadgetEnabled(isEtherGadgetEnabled);
    _imageWriter->setSetting("eject", !parser.isSet("disable-eject"));

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
