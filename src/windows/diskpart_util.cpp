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
#include <utility>

#include <windows.h>
#include <winioctl.h>
#include <ntdddisk.h>
#include <shlobj.h>

// IOCTL_DISK_ARE_VOLUMES_READY (Windows 8+) waits for the OS to finish
// bringing volumes online after partition table changes.
// Define it ourselves in case the SDK headers are too old.
#ifndef IOCTL_DISK_ARE_VOLUMES_READY
#define IOCTL_DISK_ARE_VOLUMES_READY CTL_CODE(IOCTL_DISK_BASE, 0x0087, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

namespace DiskpartUtil {

LockedVolumes::~LockedVolumes()
{
    release();
}

LockedVolumes::LockedVolumes(LockedVolumes&& other) noexcept
    : _handles(std::move(other._handles))
{
    other._handles.clear();
}

LockedVolumes& LockedVolumes::operator=(LockedVolumes&& other) noexcept
{
    if (this != &other)
    {
        release();
        _handles = std::move(other._handles);
        other._handles.clear();
    }
    return *this;
}

void LockedVolumes::release()
{
    for (void* h : _handles)
    {
        HANDLE handle = static_cast<HANDLE>(h);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            continue;
        // Best-effort unlock; closing the handle also releases the lock. The
        // underlying volume may already be gone (partition table wiped), in
        // which case this simply fails harmlessly.
        DWORD bytesReturned;
        DeviceIoControl(handle, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
        CloseHandle(handle);
    }
    _handles.clear();
}

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

DiskpartResult unmountVolumes(const QByteArray &device, LockedVolumes &locked, TimingCallback timingCallback)
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
                // Use geometric backoff — Windows 11 25H2+ may hold handles longer
                {
                    bool lockAcquired = false;
                    int lockDelayMs = 100;
                    for (int attempt = 0; attempt < 8; attempt++)
                    {
                        if (DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
                        {
                            qDebug() << "Locked volume" << driveLetter;
                            lockAcquired = true;
                            break;
                        }
                        qDebug() << "FSCTL_LOCK_VOLUME failed for" << driveLetter
                                 << "- retrying in" << lockDelayMs << "ms";
                        QThread::msleep(lockDelayMs);
                        lockDelayMs *= 2;
                    }
                    if (!lockAcquired)
                        qDebug() << "Could not lock volume" << driveLetter << "- proceeding with dismount anyway";
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
                
                // Keep the volume LOCKED and OPEN. Holding the lock stops Windows
                // from re-mounting the volume (and Explorer from re-grabbing it)
                // while we wipe the partition table and write the raw image to the
                // physical drive. Ownership of the handle passes to the caller-owned
                // LockedVolumes, which releases it once the physical drive is open.
                //
                // We deliberately do NOT call DeleteVolumeMountPoint here. Deleting
                // the mount point permanently removed the drive-letter binding from
                // the Mount Manager, which stranded card readers that Windows treats
                // as fixed disks: they reappeared with no drive letter and needed
                // manual reassignment. Holding the lock keeps the write working while
                // letting the drive letter return on its own afterwards. See #1665.
                locked.adopt(hVolume);

                // Notify Explorer that the drive has been removed so it releases any
                // cached handles and stops polling the (now dismounted) volume.
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

    CloseHandle(hDisk);

    if (success)
    {
        // Refresh Windows' view of the partition table now that it has been wiped.
        rescanDisk(device, timingCallback);
    }

    qDebug() << "cleanDiskFast completed:" << (success ? "success" : "failed")
             << "clean=" << cleanElapsed << "ms";

    return DiskpartResult{success, errorMessage};
}

DiskpartResult rescanDisk(const QByteArray &device, TimingCallback timingCallback)
{
    int diskNumber;
    if (!extractDiskNumber(device, diskNumber))
    {
        // Not a Windows physical drive path — nothing to rescan. Treat as a no-op.
        return DiskpartResult{true, QString()};
    }

    QElapsedTimer rescanTimer;
    rescanTimer.start();

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
        QString errMsg = QObject::tr("Failed to open disk for rescan. Error code: %1").arg(error);
        qDebug() << errMsg;
        if (timingCallback)
        {
            timingCallback("driveRescan", static_cast<quint32>(rescanTimer.elapsed()), false);
        }
        return DiskpartResult{false, errMsg};
    }

    DWORD bytesReturned;

    // Tells Windows to re-read partition info from the disk. Without this,
    // Explorer keeps showing whatever state the disk was in before we touched
    // it (or — if we previously wiped the partition table — nothing at all),
    // and no drive letter is assigned to the new partitions.
    if (!DeviceIoControl(hDisk, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
    {
        DWORD error = GetLastError();
        qDebug() << "IOCTL_DISK_UPDATE_PROPERTIES failed with error" << error << "- continuing anyway";
    }
    else
    {
        qDebug() << "Updated disk properties";
    }

    // Wait for the OS to finish processing volume changes (Windows 8+).
    // Blocks until all volumes on the disk have been brought online (or torn
    // down) rather than relying on an arbitrary sleep. Falls back to a fixed
    // delay if the IOCTL is not supported.
    QElapsedTimer volumeReadyTimer;
    volumeReadyTimer.start();
    if (DeviceIoControl(hDisk, IOCTL_DISK_ARE_VOLUMES_READY, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
    {
        qDebug() << "IOCTL_DISK_ARE_VOLUMES_READY completed in" << volumeReadyTimer.elapsed() << "ms";
    }
    else
    {
        DWORD error = GetLastError();
        qDebug() << "IOCTL_DISK_ARE_VOLUMES_READY failed with error" << error << "- falling back to fixed delay";
        QThread::msleep(500);
    }

    CloseHandle(hDisk);

    // Nudge Explorer to refresh its view of available drives.
    SHChangeNotify(SHCNE_DRIVEADD, SHCNF_IDLIST, NULL, NULL);
    SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_IDLIST, NULL, NULL);

    quint32 rescanElapsed = static_cast<quint32>(rescanTimer.elapsed());
    if (timingCallback)
    {
        timingCallback("driveRescan", rescanElapsed, true);
    }

    qDebug() << "rescanDisk completed for disk" << diskNumber << "in" << rescanElapsed << "ms";

    return DiskpartResult{true, QString()};
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