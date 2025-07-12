/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "driveformatthread.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/mountutils/src/mountutils.hpp"
#include "disk_formatter.h"
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>

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
        // Report the original error from the cross-platform formatter
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
