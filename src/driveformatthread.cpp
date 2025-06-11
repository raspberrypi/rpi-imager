/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "driveformatthread.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/mountutils/src/mountutils.hpp"
#include "disk_formatter.h"
#include <regex>
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <QCoreApplication>

#ifdef Q_OS_LINUX
#include "linux/udisks2api.h"
#include <unistd.h>
#endif

DriveFormatThread::DriveFormatThread(const QByteArray &device, QObject *parent)
    : QThread(parent), _device(device)
{

}

DriveFormatThread::~DriveFormatThread()
{
    wait();
}

std::uint64_t DriveFormatThread::getDeviceSize(const QByteArray &device)
{
    auto driveList = Drivelist::ListStorageDevices();
    for (const auto &drive : driveList) {
        if (QByteArray::fromStdString(drive.device) == device) {
            return drive.size;
        }
    }
    
    qDebug() << "Warning: Could not find device size for" << device << ", using default";
    return 64ULL * 1024 * 1024 * 1024;  // Default to 64GB if not found
}

void DriveFormatThread::run()
{
#ifdef Q_OS_WIN
    qDebug() << "Formatting Windows device" << _device << "with cross-platform implementation";

    // Use our cross-platform disk formatter
    rpi_imager::DiskFormatter formatter;
    auto result = formatter.FormatDrive(_device.toStdString());

    if (!result) {
#ifdef HAVE_FAT32FORMAT_FALLBACK
        // Fall back to the old diskpart + fat32format method for compatibility
        qDebug() << "Cross-platform formatter failed, falling back to diskpart";
        
        std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
        std::cmatch m;

        if (std::regex_match(_device.constData(), m, windriveregex))
        {
            QByteArray nr = QByteArray::fromStdString(m[1]);

            qDebug() << "Formatting Windows drive #" << nr << "(" << _device << ")";

            QProcess proc;
            QByteArray diskpartCmds =
                    "select disk "+nr+"\r\n"
                    "clean\r\n"
                    "create partition primary\r\n"
                    "select partition 1\r\n"
                    "set id=0e\r\n"
                    "assign\r\n";
            proc.start("diskpart", QStringList());
            proc.waitForStarted();
            proc.write(diskpartCmds);
            proc.closeWriteChannel();
            proc.waitForFinished();

            QByteArray output = proc.readAllStandardError();
            qDebug() << output;
            qDebug() << "Done running diskpart. Exit status code =" << proc.exitCode();

            if (proc.exitCode())
            {
                emit error(tr("Error partitioning: %1").arg(QString(output)));
            }
            else
            {
                auto l = Drivelist::ListStorageDevices();
                QByteArray devlower = _device.toLower();
                for (auto i : l)
                {
                    if (QByteArray::fromStdString(i.device).toLower() == devlower && i.mountpoints.size() == 1)
                    {
                        QByteArray driveLetter = QByteArray::fromStdString(i.mountpoints.front());
                        if (driveLetter.endsWith("\\"))
                            driveLetter.chop(1);
                        qDebug() << "Drive letter of device:" << driveLetter;

                        QProcess f32format;
                        QStringList args;
                        args << "-y" << driveLetter;
                        f32format.start(QCoreApplication::applicationDirPath()+"/fat32format.exe", args);
                        if (!f32format.waitForStarted())
                        {
                            emit error(tr("Error starting fat32format"));
                            return;
                        }

                        //f32format.write("y\r\n");
                        f32format.closeWriteChannel();
                        f32format.waitForFinished(120000);

                        if (f32format.exitStatus() || f32format.exitCode())
                        {
                            emit error(tr("Error running fat32format: %1").arg(QString(f32format.readAll())));
                        }
                        else
                        {
                            emit success();
                        }
                        return;
                    }
                }

                emit error(tr("Error determining new drive letter"));
            }
        }
        else
        {
            emit error(tr("Invalid device: %1").arg(QString(_device)));
        }
#else
        // No fallback available, report the original error
        QString errorMessage;
        switch (result.error()) {
            case rpi_imager::FormatError::kFileOpenError:
                errorMessage = tr("Error opening device for formatting");
                break;
            case rpi_imager::FormatError::kFileWriteError:
                errorMessage = tr("Error writing to device during formatting");
                break;
            case rpi_imager::FormatError::kFileSeekError:
                errorMessage = tr("Error seeking on device during formatting");
                break;
            case rpi_imager::FormatError::kInvalidParameters:
                errorMessage = tr("Invalid parameters for formatting");
                break;
            case rpi_imager::FormatError::kInsufficientSpace:
                errorMessage = tr("Insufficient space on device");
                break;
            default:
                errorMessage = tr("Unknown formatting error");
                break;
        }
        
        qDebug() << "Cross-platform formatting failed:" << errorMessage;
        emit error(errorMessage);
#endif
    }
    else
    {
        qDebug() << "Cross-platform disk formatter succeeded";
        emit success();
    }
#elif defined(Q_OS_DARWIN)
    QProcess proc;
    QStringList args;
    args << "eraseDisk" << "FAT32" << "SDCARD" << "MBRFormat" << _device;
    proc.start("diskutil", args);
    proc.waitForFinished();

    QByteArray output = proc.readAllStandardError();
    qDebug() << args;
    qDebug() << "diskutil output:" << output;

    if (proc.exitCode())
    {
        emit error(tr("Error partitioning: %1").arg(QString(output)));
    }
    else
    {
        emit success();
    }

#elif defined(Q_OS_LINUX)

    if (::access(_device.constData(), W_OK) != 0)
    {
        /* Not running as root, try to outsource formatting to udisks2 */

#ifndef QT_NO_DBUS
        UDisks2Api udisks2;
        if (udisks2.formatDrive(_device))
        {
            emit success();
        }
        else
        {
#endif
            emit error(tr("Error formatting (through udisks2)"));
#ifndef QT_NO_DBUS
        }
#endif

        return;
    }

    // Unmount the device before formatting
    unmount_disk(_device);

    qDebug() << "Formatting device" << _device << "with clean-room implementation";

    // Get device size
    std::uint64_t deviceSize = getDeviceSize(_device);
    qDebug() << "Device size:" << deviceSize << "bytes";

    // Use our clean-room disk formatter
    rpi_imager::DiskFormatter formatter;
    auto result = formatter.FormatDrive(_device.toStdString());

    if (!result) {
        QString errorMessage;
        switch (result.error()) {
            case rpi_imager::FormatError::kFileOpenError:
                errorMessage = tr("Error opening device for formatting");
                break;
            case rpi_imager::FormatError::kFileWriteError:
                errorMessage = tr("Error writing to device during formatting");
                break;
            case rpi_imager::FormatError::kFileSeekError:
                errorMessage = tr("Error seeking on device during formatting");
                break;
            case rpi_imager::FormatError::kInvalidParameters:
                errorMessage = tr("Invalid parameters for formatting");
                break;
            case rpi_imager::FormatError::kInsufficientSpace:
                errorMessage = tr("Insufficient space on device");
                break;
            default:
                errorMessage = tr("Unknown formatting error");
                break;
        }
        
        qDebug() << "Formatting failed:" << errorMessage;
        emit error(errorMessage);
        return;
    }

    qDebug() << "Formatting completed successfully";
    emit success();

#else
    emit error(tr("Formatting not implemented for this platform"));
#endif
}
