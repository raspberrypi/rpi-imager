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

#ifdef Q_OS_WIN
#include <regex>
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
#ifdef Q_OS_LINUX
    // Linux-specific: Check if we need to use udisks2 fallback
    if (::access(_device.constData(), W_OK) != 0)
    {
        /* Not running as root, try to outsource formatting to udisks2 */
#ifndef QT_NO_DBUS
        UDisks2Api udisks2;
        if (udisks2.formatDrive(_device))
        {
            emit success();
            return;
        } else {
            emit error(tr("Error formatting (through udisks2)"));
        }
#else // QT_NO_DBUS
        emit error(tr("Cannot format device: insufficient permissions and udisks2 not available"));
#endif
        return;
    }
#endif

#ifdef Q_OS_WIN
    // Windows-specific disk preparation
    emit preparationStatusUpdate(tr("Preparing disk for formatting..."));
    
    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (std::regex_match(_device.constData(), m, windriveregex))
    {
        QByteArray diskNumber = QByteArray::fromStdString(m[1]);
        
        // Check for mounted volumes
        auto l = Drivelist::ListStorageDevices();
        QByteArray devlower = _device.toLower();
        bool hasVolumes = false;
        
        for (auto i : l)
        {
            if (QByteArray::fromStdString(i.device).toLower() == devlower)
            {
                hasVolumes = !i.mountpoints.empty();
                break;
            }
        }

        // Clean disk with diskpart (required for mounted volumes, safe for clean disks)
        emit preparationStatusUpdate(tr("Cleaning disk..."));
        QProcess diskpartProcess;
        diskpartProcess.start("diskpart", QStringList());
        if (!diskpartProcess.waitForStarted(5000))
        {
            emit error(tr("Failed to start disk cleanup utility. Please ensure you have administrator privileges."));
            return;
        }
        
        QString script = QString("select disk %1\nclean\nrescan\n").arg(diskNumber);
        diskpartProcess.write(script.toLatin1());
        diskpartProcess.closeWriteChannel();
        
        if (!diskpartProcess.waitForFinished(60000))
        {
            diskpartProcess.kill();
            emit error(tr("Disk cleaning operation timed out. The disk may be in use by another application."));
            return;
        }
        
        if (diskpartProcess.exitCode() != 0)
        {
            QString errorOutput = QString(diskpartProcess.readAllStandardError());
            emit error(tr("Failed to clean disk. Error: %1").arg(errorOutput.isEmpty() ? tr("Unknown error") : errorOutput));
            return;
        }
        
        // Brief pause to let system settle
        QThread::msleep(1000);
    }
#endif

    // Common formatting logic for all platforms
    qDebug() << "Formatting device" << _device << "with cross-platform implementation";
    emit preparationStatusUpdate(tr("Writing filesystem..."));

    // Unmount the device before formatting (needed for macOS and Linux)
#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
    unmount_disk(_device.constData());
#endif

#ifdef Q_OS_LINUX
    // Get device size for logging
    std::uint64_t deviceSize = getDeviceSize(_device);
    qDebug() << "Device size:" << deviceSize << "bytes";
#endif

    // Use cross-platform disk formatter
    rpi_imager::DiskFormatter formatter;
    auto result = formatter.FormatDrive(_device.toStdString());

    if (!result) {
        emit error(formatErrorToString(result.error()));
    } else {
        qDebug() << "Cross-platform disk formatter succeeded";
        emit success();
    }
}

QString DriveFormatThread::formatErrorToString(rpi_imager::FormatError error)
{
    switch (error) {
        case rpi_imager::FormatError::kFileOpenError:
            return tr("Error opening device for formatting");
        case rpi_imager::FormatError::kFileWriteError:
            return tr("Error writing to device during formatting");
        case rpi_imager::FormatError::kFileSeekError:
            return tr("Error seeking on device during formatting");
        case rpi_imager::FormatError::kInvalidParameters:
            return tr("Invalid parameters for formatting");
        case rpi_imager::FormatError::kInsufficientSpace:
            return tr("Insufficient space on device");
        default:
            return tr("Unknown formatting error");
    }
}
