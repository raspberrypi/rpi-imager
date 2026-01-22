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
#include "drivelist/drivelist.h"
#include "imageadvancedoptions.h"
#include "platformquirks.h"

/* Message handler to discard qDebug() output if using cli (unless --debug is set) */
static void devnullMsgHandler(QtMsgType, const QMessageLogContext &, const QString &)
{
}

Cli::Cli(int &argc, char *argv[]) : QObject(nullptr), _imageWriter(nullptr)
{
    /* Attach to console for output (Windows-specific, no-op on other platforms) */
    PlatformQuirks::attachConsole();
    _app = new QCoreApplication(argc, argv);
    _app->setOrganizationName("Raspberry Pi");
    _app->setOrganizationDomain("raspberrypi.com");
    _app->setApplicationName("Raspberry Pi Imager");
    _app->setApplicationVersion(ImageWriter::staticVersion());
    // Don't create ImageWriter here - defer until we know we need it
}

Cli::~Cli()
{
    delete _imageWriter;
    delete _app;
}

int Cli::run()
{
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
#ifndef CLI_ONLY_BUILD
        {"cli", ""},  // Only relevant when running GUI build in CLI mode
#endif
        {"disable-verify", "Disable verification"},
        {"enable-writing-system-drives", "Only use this if you know what you are doing"},
        {"sha256", "Expected hash", "sha256", ""},
        {"cache-file", "Custom cache file (requires setting sha256 as well)", "cache-file", ""},
        {"first-run-script", "Add firstrun.sh to image", "first-run-script", ""},
        {"cloudinit-userdata", "Add cloud-init user-data file to image", "cloudinit-userdata", ""},
        {"cloudinit-networkconfig", "Add cloud-init network-config file to image", "cloudinit-networkconfig", ""},
        {"disable-eject", "Disable automatic ejection of storage media after verification"},
        {"debug", "Output debug messages to console"},
        {"quiet", "Only write to console on error"},
        {"log-file", "Log output to file (for debugging)", "path", ""},
        {"secure-boot-key", "Path to RSA private key (PEM format) for secure boot signing", "key-file", ""},
    });

    parser.addPositionalArgument("src", "Image file/URL");
    parser.addPositionalArgument("dst", "Destination device");
    parser.process(*_app);

    // Check for elevated privileges on platforms that require them (Linux/Windows)
    if (!PlatformQuirks::hasElevatedPrivileges())
    {
        // Common error message
        const char* commonMsg = "Writing to storage devices requires elevated privileges.";
        
#ifdef Q_OS_LINUX
        // Get the actual executable name (e.g., AppImage name or 'rpi-imager')
        // Check if running from AppImage first
        QString execName;
        QByteArray appImagePath = qgetenv("APPIMAGE");
        if (!appImagePath.isEmpty()) {
            execName = QFileInfo(QString::fromUtf8(appImagePath)).fileName();
        } else {
            execName = QFileInfo(_app->arguments()[0]).fileName();
        }
        
        std::cerr << "ERROR: Not running as root." << std::endl;
        std::cerr << commonMsg << std::endl;
        std::cerr << "Please run with sudo: sudo " << execName.toStdString()
#ifndef CLI_ONLY_BUILD
        << " --cli"
#endif
        << " ..." << std::endl;
#elif defined(Q_OS_WIN)
        std::cerr << "ERROR: Not running as Administrator." << std::endl;
        std::cerr << commonMsg << std::endl;
        std::cerr << "Please run as Administrator." << std::endl;
#endif
        return 1;
    }


    const QStringList args = parser.positionalArguments();
    if (args.count() != 2)
    {
        std::cerr << parser.helpText().toStdString() << std::endl;
        return 1;
    }

    // Now create ImageWriter for actual write operations
    _imageWriter = new ImageWriter;
    connect(_imageWriter, &ImageWriter::success, this, &Cli::onSuccess);
    connect(_imageWriter, &ImageWriter::error, this, &Cli::onError);
    connect(_imageWriter, &ImageWriter::preparationStatusUpdate, this, &Cli::onPreparationStatusUpdate);
    connect(_imageWriter, &ImageWriter::downloadProgress, this, &Cli::onDownloadProgress);
    connect(_imageWriter, &ImageWriter::verifyProgress, this, &Cli::onVerifyProgress);

    if (!parser.isSet("debug"))
    {
        qInstallMessageHandler(devnullMsgHandler);
    }
    _quiet = parser.isSet("quiet");
    QByteArray initFormat = (parser.value("cloudinit-userdata").isEmpty()
                             && parser.value("cloudinit-networkconfig").isEmpty() ) ? "systemd" : "cloudinit";
    
    // Handle secure boot key if provided
    ImageOptions::AdvancedOptions advancedOptions = ImageOptions::NoAdvancedOptions;
    if (!parser.value("secure-boot-key").isEmpty())
    {
        QString keyPath = parser.value("secure-boot-key");
        QFileInfo keyFile(keyPath);
        if (!keyFile.exists())
        {
            std::cerr << "Error: secure boot key file does not exist: " << keyPath.toStdString() << std::endl;
            return 1;
        }
        if (!keyFile.isFile())
        {
            std::cerr << "Error: secure boot key path is not a regular file: " << keyPath.toStdString() << std::endl;
            return 1;
        }
        
        // Store key path in settings for ImageWriter to access
        _imageWriter->setSetting("secureboot_rsa_key", keyPath);
        advancedOptions = ImageOptions::EnableSecureBoot;
        
        if (!_quiet)
        {
            std::cerr << "Secure boot signing enabled with key: " << keyPath.toStdString() << std::endl;
        }
    }

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

    if (!parser.value("cloudinit-userdata").isEmpty() || !parser.value("cloudinit-networkconfig").isEmpty())
    {
        QByteArray userData, networkConfig;
        if (!parser.value("cloudinit-userdata").isEmpty())
        {
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
        }

        if (!parser.value("cloudinit-networkconfig").isEmpty())
        {
            QFile f(parser.value("cloudinit-networkconfig"));

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
        }

        _imageWriter->setImageCustomisation("", "", "", userData, networkConfig, advancedOptions, initFormat);
    }
    else if (!parser.value("first-run-script").isEmpty())
    {
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

        _imageWriter->setImageCustomisation("", "", firstRunScript, "", "", ImageOptions::UserDefinedFirstRun | advancedOptions, initFormat);
    }
    else if (advancedOptions != ImageOptions::NoAdvancedOptions)
    {
        // Secure boot key provided without customization scripts
        _imageWriter->setImageCustomisation("", "", "", "", "", advancedOptions, initFormat);
    }

    _imageWriter->setDst(args[1]);
    _imageWriter->setVerifyEnabled(!parser.isSet("disable-verify"));
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
    // ANSI "Erase in Line" escape sequence
    std::cerr << "\e[0K";
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
