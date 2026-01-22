/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "diskpart_util.h"
#include "../drivelist/drivelist.h"
#include "../platformquirks.h"
#include "winfile.h"
#include <QDebug>
#include <QProcess>
#include <QThread>
#include <QElapsedTimer>
#include <regex>
#include <chrono>

#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>

namespace DiskpartUtil {

// Notify Windows Explorer that a drive has changed/been removed
// This prevents Explorer from showing "Insert a disk" dialogs
static void notifyShellDriveRemoved(const QString &driveLetter)
{
    if (driveLetter.isEmpty())
        return;
    
    // Get the drive path (e.g., "E:\")
    QString drivePath = driveLetter;
    if (!drivePath.endsWith("\\"))
        drivePath += "\\";
    
    wchar_t pathW[MAX_PATH];
    drivePath.toWCharArray(pathW);
    pathW[drivePath.length()] = 0;
    
    // Notify shell that media was removed - this tells Explorer to stop trying to access the drive
    SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATH, pathW, NULL);
    SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATH, pathW, NULL);
    
    qDebug() << "Notified Explorer that drive" << driveLetter << "was removed";
}

// Helper to extract disk number from path like \\.\PHYSICALDRIVE0
static bool extractDiskNumber(const QByteArray &device, int &diskNumber)
{
    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;
    
    if (!std::regex_match(device.constData(), m, windriveregex))
    {
        return false;
    }
    
    diskNumber = std::stoi(m[1].str());
    return true;
}

DiskpartResult unmountVolumes(const QByteArray &device, TimingCallback timingCallback)
{
    QElapsedTimer timer;
    timer.start();
    
    int diskNumber;
    if (!extractDiskNumber(device, diskNumber))
    {
        return DiskpartResult{false, QObject::tr("Invalid Windows physical drive path: %1").arg(QString(device))};
    }
    
    // Get list of storage devices to find volumes on this disk
    auto deviceList = Drivelist::ListStorageDevices();
    QByteArray canonicalDevice = PlatformQuirks::getEjectDevicePath(device).toLower().toUtf8();
    int volumesProcessed = 0;
    
    for (const auto &dev : deviceList)
    {
        if (QByteArray::fromStdString(dev.device).toLower() == canonicalDevice)
        {
            for (const auto &mountpoint : dev.mountpoints)
            {
                QString driveLetter = QString::fromStdString(mountpoint);
                if (driveLetter.endsWith("\\"))
                    driveLetter.chop(1);
                
                qDebug() << "Unmounting volume" << driveLetter;
                
                // Notify Explorer BEFORE we start - this helps prevent "Insert a disk" dialogs
                // by telling Explorer to release handles and stop monitoring the drive
                notifyShellDriveRemoved(driveLetter);
                
                // Open the volume
                QString volumePath = "\\\\.\\" + driveLetter;
                HANDLE hVolume = CreateFileW(
                    reinterpret_cast<LPCWSTR>(volumePath.utf16()),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    OPEN_EXISTING,
                    0,
                    nullptr
                );
                
                if (hVolume == INVALID_HANDLE_VALUE)
                {
                    qDebug() << "Could not open volume" << driveLetter << "- may already be unmounted";
                    continue;
                }
                
                DWORD bytesReturned;
                
                // Lock the volume (prevents other processes from accessing it)
                for (int attempt = 0; attempt < 10; attempt++)
                {
                    if (DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
                    {
                        qDebug() << "Locked volume" << driveLetter;
                        break;
                    }
                    QThread::msleep(50);  // Reduced from 100ms
                }
                
                // Dismount the volume (flushes buffers and invalidates handles)
                if (DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
                {
                    qDebug() << "Dismounted volume" << driveLetter;
                }
                else
                {
                    qDebug() << "Failed to dismount volume" << driveLetter << "- continuing anyway";
                }
                
                // Unlock and close the volume handle BEFORE removing mount point
                DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
                CloseHandle(hVolume);
                
                // Remove the drive letter assignment using DeleteVolumeMountPoint
                // This is the KEY step that prevents "Insert a disk" dialogs!
                // Unlike FSCTL_DISMOUNT_VOLUME which just unmounts the filesystem,
                // DeleteVolumeMountPoint removes the drive letter entirely so
                // Windows Explorer won't try to access it after we clean the disk.
                QString mountPoint = driveLetter + "\\";  // Must end with backslash
                if (DeleteVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16())))
                {
                    qDebug() << "Removed drive letter" << driveLetter;
                }
                else
                {
                    DWORD error = GetLastError();
                    qDebug() << "Failed to remove drive letter" << driveLetter << "error:" << error << "- continuing anyway";
                }
                
                // Notify Explorer that the drive has been removed
                // This is a secondary notification to help Explorer update its view
                notifyShellDriveRemoved(driveLetter);
                
                volumesProcessed++;
                
                // Brief pause between volumes (reduced from 500ms)
                if (volumesProcessed > 0)
                {
                    QThread::msleep(100);
                }
            }
            break;
        }
    }
    
    quint32 elapsed = static_cast<quint32>(timer.elapsed());
    if (timingCallback)
    {
        timingCallback("driveUnmountVolumes", elapsed, true);
    }
    
    qDebug() << "Unmounted" << volumesProcessed << "volumes in" << elapsed << "ms";
    return DiskpartResult{true, QString()};
}

DiskpartResult cleanDiskFast(const QByteArray &device, TimingCallback timingCallback)
{
    QElapsedTimer cleanTimer;
    cleanTimer.start();
    
    int diskNumber;
    if (!extractDiskNumber(device, diskNumber))
    {
        return DiskpartResult{false, QObject::tr("Invalid Windows physical drive path: %1").arg(QString(device))};
    }
    
    qDebug() << "cleanDiskFast: Cleaning disk" << diskNumber << "using direct IOCTLs";
    
    // Open the physical drive
    HANDLE hDisk = CreateFileA(
        device.constData(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr
    );
    
    if (hDisk == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        QString errMsg = QObject::tr("Failed to open disk for cleaning. Error code: %1").arg(error);
        qDebug() << errMsg;
        return DiskpartResult{false, errMsg};
    }
    
    DWORD bytesReturned;
    bool success = true;
    QString errorMessage;
    
    // Allow extended DASD I/O (required for raw disk access)
    DeviceIoControl(hDisk, FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
    
    // Delete the drive layout (removes all partitions)
    // This is equivalent to "diskpart clean"
    if (!DeviceIoControl(hDisk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
    {
        DWORD error = GetLastError();
        // ERROR_INVALID_FUNCTION (1) means there was no partition table - that's fine
        // ERROR_NOT_READY (21) can happen if disk is being accessed - retry
        if (error != ERROR_INVALID_FUNCTION && error != ERROR_FILE_NOT_FOUND)
        {
            qDebug() << "IOCTL_DISK_DELETE_DRIVE_LAYOUT failed with error" << error << "- will try zeroing MBR";
            
            // Fallback: zero out the first sector to clear partition table
            LARGE_INTEGER zero = {};
            SetFilePointerEx(hDisk, zero, nullptr, FILE_BEGIN);
            
            char emptyMBR[512] = {0};
            DWORD bytesWritten;
            if (!WriteFile(hDisk, emptyMBR, 512, &bytesWritten, nullptr) || bytesWritten != 512)
            {
                error = GetLastError();
                errorMessage = QObject::tr("Failed to clear partition table. Error code: %1").arg(error);
                success = false;
            }
            else
            {
                qDebug() << "Zeroed MBR as fallback";
            }
        }
        else
        {
            qDebug() << "No partition table to delete (error" << error << ")";
        }
    }
    else
    {
        qDebug() << "Successfully deleted drive layout";
    }
    
    quint32 cleanElapsed = static_cast<quint32>(cleanTimer.elapsed());
    if (timingCallback && success)
    {
        timingCallback("driveDiskClean", cleanElapsed, true);
    }
    
    // Rescan disk to update Windows' view of the partition table
    QElapsedTimer rescanTimer;
    rescanTimer.start();
    
    if (success)
    {
        // Update disk properties - tells Windows to re-read disk info
        if (!DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
        {
            DWORD error = GetLastError();
            qDebug() << "IOCTL_DISK_UPDATE_PROPERTIES failed with error" << error << "- continuing anyway";
        }
        else
        {
            qDebug() << "Updated disk properties";
        }
    }
    
    CloseHandle(hDisk);
    
    if (success)
    {
        // Brief pause to let Windows process the changes (reduced from 1 second)
        QThread::msleep(200);
    }
    
    quint32 rescanElapsed = static_cast<quint32>(rescanTimer.elapsed());
    if (timingCallback && success)
    {
        timingCallback("driveRescan", rescanElapsed, true);
    }
    
    qDebug() << "cleanDiskFast completed:" << (success ? "success" : "failed") 
             << "clean=" << cleanElapsed << "ms, rescan=" << rescanElapsed << "ms";
    
    return DiskpartResult{success, errorMessage};
}

DiskpartResult cleanDisk(const QByteArray &device, std::chrono::milliseconds timeout, int maxRetries, VolumeHandling volumeHandling)
{
    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (!std::regex_match(device.constData(), m, windriveregex))
    {
        return DiskpartResult{false, QObject::tr("Invalid Windows physical drive path: %1").arg(QString(device))};
    }

    QByteArray diskNumber = QByteArray::fromStdString(m[1]);
    
    // Check for mounted volumes and optionally unmount them first
    if (volumeHandling == VolumeHandling::UnmountFirst)
    {
        auto l = Drivelist::ListStorageDevices();
        QByteArray canonicalDevice = PlatformQuirks::getEjectDevicePath(device).toLower().toUtf8();
        
        for (auto i : l)
        {
            if (QByteArray::fromStdString(i.device).toLower() == canonicalDevice)
            {
                for (const auto& mountpoint : i.mountpoints)
                {
                    QString driveLetter = QString::fromStdString(mountpoint);
                    if (driveLetter.endsWith("\\"))
                        driveLetter.chop(1);
                    
                    qDebug() << "Attempting to unmount drive" << driveLetter;
                    
                    // Try to lock and unlock the volume to force unmount
                    WinFile tempFile;
                    tempFile.setFileName("\\\\.\\" + driveLetter);
                    if (tempFile.open(QIODevice::ReadWrite))
                    {
                        if (tempFile.lockVolume())
                        {
                            tempFile.unlockVolume();
                            qDebug() << "Successfully unlocked volume" << driveLetter;
                        }
                        tempFile.close();
                    }
                    
                    // Give the system time to process the unmount
                    QThread::msleep(std::chrono::milliseconds(500).count());
                }
                break;
            }
        }
    }

    // Run diskpart with retry logic
    bool diskpartSuccess = false;
    QString lastError;
    
    for (int attempt = 1; attempt <= maxRetries; attempt++)
    {
        qDebug() << "Running diskpart attempt" << attempt << "of" << maxRetries;
        
        QProcess diskpartProcess;
        diskpartProcess.start("diskpart", QStringList());
        if (!diskpartProcess.waitForStarted(std::chrono::seconds(5).count()))
        {
            lastError = QObject::tr("Failed to start disk cleanup utility. Please ensure you have administrator privileges.");
            qDebug() << "Failed to start diskpart on attempt" << attempt;
            if (attempt < maxRetries) {
                QThread::msleep(std::chrono::seconds(attempt).count()); // Progressive delay
                continue;
            }
            break;
        }
        
        QString script = QString("select disk %1\r\nclean\r\nrescan\r\n").arg(diskNumber);
        diskpartProcess.write(script.toLatin1());
        diskpartProcess.closeWriteChannel();
        
        if (!diskpartProcess.waitForFinished(timeout.count()))
        {
            diskpartProcess.kill();
            lastError = QObject::tr("Disk cleaning operation timed out. The disk may be in use by another application.");
            qDebug() << "diskpart timed out on attempt" << attempt;
            if (attempt < maxRetries) {
                QThread::msleep(std::chrono::seconds(attempt).count()); // Progressive delay
                continue;
            }
            break;
        }
        
        if (diskpartProcess.exitCode() == 0)
        {
            diskpartSuccess = true;
            qDebug() << "diskpart succeeded on attempt" << attempt;
            break;
        }
        else
        {
            QString errorOutput = QString(diskpartProcess.readAllStandardError());
            lastError = QObject::tr("Failed to clean disk. Error: %1").arg(errorOutput.isEmpty() ? QObject::tr("Unknown error") : errorOutput);
            qDebug() << "diskpart failed on attempt" << attempt << "with error:" << errorOutput;
            
            if (attempt < maxRetries) {
                QThread::msleep(std::chrono::seconds(attempt).count()); // Progressive delay
            }
        }
    }

    if (!diskpartSuccess)
    {
        if (maxRetries > 1) {
            return DiskpartResult{false, QObject::tr("Failed to clean disk after %1 attempts. %2").arg(maxRetries).arg(lastError)};
        } else {
            return DiskpartResult{false, lastError};
        }
    }
    
    // Brief pause to let system settle
    QThread::msleep(std::chrono::seconds(1).count());
    
    return DiskpartResult{true, QString()};
}

} // namespace DiskpartUtil 