/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "drivelistmodel.h"
#include "config.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include <QSet>
#include <QDebug>

DriveListModel::DriveListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    _rolenames = {
        {deviceRole, "device"},
        {descriptionRole, "description"},
        {sizeRole, "size"},
        {isUsbRole, "isUsb"},
        {isScsiRole, "isScsi"},
        {isReadOnlyRole, "isReadOnly"},
        {isSystemRole, "isSystem"},
        {mountpointsRole, "mountpoints"}
    };

    // Enumerate drives in seperate thread, but process results in UI thread
    connect(&_thread, SIGNAL(newDriveList(std::vector<Drivelist::DeviceDescriptor>)), SLOT(processDriveList(std::vector<Drivelist::DeviceDescriptor>)));
    
    // Forward performance event signal
    connect(&_thread, &DriveListModelPollThread::eventDriveListPoll,
            this, &DriveListModel::eventDriveListPoll);
}

int DriveListModel::rowCount(const QModelIndex &) const
{
    return _drivelist.count();
}

QHash<int, QByteArray> DriveListModel::roleNames() const
{
    return _rolenames;
}

QVariant DriveListModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if (row < 0 || row >= _drivelist.count())
        return QVariant();

    QByteArray propertyName = _rolenames.value(role);
    if (propertyName.isEmpty())
        return QVariant();
    else {
        auto it = _drivelist.cbegin();
        std::advance(it, row);
        return it.value()->property(propertyName);
    }
}

void DriveListModel::processDriveList(std::vector<Drivelist::DeviceDescriptor> l)
{
    bool changes = false;
    QSet<QString> drivesInNewList;

    for (auto &i: l)
    {
        // Convert STL vector<string> to Qt QStringList
        QStringList mountpoints;
        for (auto &s: i.mountpoints)
        {
            mountpoints.append(QString::fromStdString(s));
        }

        // Should already be caught by isSystem variable, but just in case...
        if (mountpoints.contains("/") || mountpoints.contains("C://"))
            continue;

        // Skip zero-sized devices
        if (i.size == 0)
            continue;

#ifdef Q_OS_DARWIN
        // Allow read/write virtual devices (mounted disk images) but filter out:
        // - Read-only virtual devices
        // - System virtual devices (like APFS volumes)
        // - Virtual devices that are not removable/ejectable (likely system virtual devices)
        if (i.isVirtual && (i.isReadOnly || i.isSystem || !i.isRemovable))
            continue;
#endif

        QString deviceNamePlusSize = QString::fromStdString(i.device)+":"+QString::number(i.size);
        if (i.isReadOnly)
            deviceNamePlusSize += "ro";
        drivesInNewList.insert(deviceNamePlusSize);

        if (!_drivelist.contains(deviceNamePlusSize))
        {
            // Found new drive
            if (!changes)
            {
                beginResetModel();
                changes = true;
            }

            // TEMPORARY TEST ONLY: Mark Apple Disk Image Media as system drives on macOS
            // Reason: to exercise the wizard's system-drive confirmation flow.
            // To undo: remove this override block and pass i.isSystem directly.
#ifdef Q_OS_DARWIN
            const QString desc = QString::fromStdString(i.description);
            const bool isSystemOverride = i.isSystem || desc.contains("Apple Disk Image Media", Qt::CaseInsensitive);
#else
            const bool isSystemOverride = i.isSystem;
#endif

            // Treat NVMe drives like SCSI for icon purposes (use storage icon instead of SD card icon)
            QString busType = QString::fromStdString(i.busType);
            QString devicePath = QString::fromStdString(i.device);
            bool isNvme = (busType.compare("NVME", Qt::CaseInsensitive) == 0) || devicePath.startsWith("/dev/nvme");
            bool isScsiForIcon = i.isSCSI || isNvme;

            _drivelist[deviceNamePlusSize] = new DriveListItem(QString::fromStdString(i.device), QString::fromStdString(i.description), i.size, i.isUSB, isScsiForIcon, i.isReadOnly, isSystemOverride, mountpoints, this);
        }
    }

    // Look for drives removed
    QStringList drivesInOldList = _drivelist.keys();
    for (auto &device: drivesInOldList)
    {
        if (!drivesInNewList.contains(device))
        {
            if (!changes)
            {
                beginResetModel();
                changes = true;
            }

            // Extract device path before deleting the item
            QString devicePath = _drivelist.value(device)->property("device").toString();
            
            qDebug() << "Drive removed:" << devicePath;
            
            _drivelist.value(device)->deleteLater();
            _drivelist.remove(device);
            
            // Emit signal for this specific device removal
            emit deviceRemoved(devicePath);
        }
    }

    if (changes)
        endResetModel();
}

void DriveListModel::startPolling()
{
    _thread.start();
}

void DriveListModel::stopPolling()
{
    _thread.stop();
}

void DriveListModel::pausePolling()
{
    _thread.pause();
}

void DriveListModel::resumePolling()
{
    _thread.resume();
}

void DriveListModel::setSlowPolling()
{
    _thread.setScanMode(DriveListModelPollThread::ScanMode::Slow);
}