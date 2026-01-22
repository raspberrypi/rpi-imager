/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "driveformatthread.h"
#include "drivelist/drivelist.h"
#include "disk_formatter.h"
#include "platformquirks.h"
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <QElapsedTimer>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <regex>
#include <chrono>
#include "windows/diskpart_util.h"
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
    // Suppress Windows "Insert a disk" / "not accessible" system error dialogs
    // for this thread. Error mode is per-thread, so we set it once at thread start.
    DWORD oldMode;
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode)) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    }
#endif

#ifdef Q_OS_LINUX
    // Linux-specific: Check permissions
    if (::access(_device.constData(), W_OK) != 0)
    {
        emit error(tr("Cannot format device: insufficient permissions. Please run with elevated privileges (sudo)."));
        return;
    }
#endif

#ifdef Q_OS_WIN
    // Windows-specific disk preparation
    emit preparationStatusUpdate(tr("Preparing disk for formatting..."));
    
    // Clean disk with diskpart utility (standardized to 60s timeout with 3 retries, always unmount volumes)
    emit preparationStatusUpdate(tr("Cleaning disk..."));
    auto diskpartResult = DiskpartUtil::cleanDisk(_device, std::chrono::seconds(60), 3, DiskpartUtil::VolumeHandling::UnmountFirst);
    if (!diskpartResult.success)
    {
        emit error(diskpartResult.errorMessage);
        return;
    }
#endif

    // Common formatting logic for all platforms
    qDebug() << "Formatting device" << _device << "with cross-platform implementation";
    emit preparationStatusUpdate(tr("Writing filesystem..."));

    // Unmount the device before formatting (needed for macOS and Linux)
#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
    // Use block device path for unmount (e.g., /dev/disk on macOS, not /dev/rdisk)
    QString unmountPath = PlatformQuirks::getEjectDevicePath(QString::fromLatin1(_device));
    qDebug() << "Unmounting:" << unmountPath;
    PlatformQuirks::DiskResult unmountResult = PlatformQuirks::unmountDisk(unmountPath);
    if (unmountResult != PlatformQuirks::DiskResult::Success) {
        qDebug() << "Unmount failed with result:" << static_cast<int>(unmountResult);
#ifdef Q_OS_DARWIN
        emit error(tr("Failed to unmount disk '%1'. Please close any applications using the disk and try again.").arg(unmountPath));
#else
        emit error(tr("Failed to unmount disk '%1'.").arg(unmountPath));
#endif
        return;
    }
#endif

#ifdef Q_OS_LINUX
    // Get device size for logging
    std::uint64_t deviceSize = getDeviceSize(_device);
    qDebug() << "Device size:" << deviceSize << "bytes";
#endif

    // Use cross-platform disk formatter
    QElapsedTimer formatTimer;
    formatTimer.start();
    
    rpi_imager::DiskFormatter formatter;
    auto formatResult = formatter.FormatDrive(_device.toStdString());

    quint32 formatDurationMs = static_cast<quint32>(formatTimer.elapsed());
    
    if (!formatResult) {
        emit eventDriveFormat(formatDurationMs, false);
        emit error(formatErrorToString(formatResult.error()));
    } else {
        emit eventDriveFormat(formatDurationMs, true);
        qDebug() << "Cross-platform disk formatter succeeded in" << formatDurationMs << "ms";
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
