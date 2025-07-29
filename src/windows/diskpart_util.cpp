/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "diskpart_util.h"
#include "../dependencies/drivelist/src/drivelist.hpp"
#include "winfile.h"
#include <QDebug>
#include <QProcess>
#include <QThread>
#include <regex>
#include <chrono>

namespace DiskpartUtil {

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
        QByteArray devlower = device.toLower();
        
        for (auto i : l)
        {
            if (QByteArray::fromStdString(i.device).toLower() == devlower)
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