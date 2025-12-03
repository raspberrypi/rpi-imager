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
        {mountpointsRole, "mountpoints"},
        {childDevicesRole, "childDevices"}
    };

    // Enumerate drives in separate thread, but process results in UI thread
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

        // Allow read/write virtual devices (mounted disk images) but filter out:
        // - Read-only virtual devices
        // - System virtual devices (like APFS volumes on macOS)
        // - Virtual devices that are not removable/ejectable (likely system virtual devices)
        if (i.isVirtual && (i.isReadOnly || i.isSystem || !i.isRemovable))
            continue;

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

            // Mark virtual disks as system drives to trigger confirmation dialog
            // This allows virtual disks (disk images, VHDs, etc.) to appear in the list
            // but ensures users must confirm before writing to them
            const bool isSystemOverride = i.isSystem || i.isVirtual;

            // Treat NVMe drives like SCSI for icon purposes (use storage icon instead of SD card icon)
            QString busType = QString::fromStdString(i.busType);
            QString devicePath = QString::fromStdString(i.device);
            bool isNvme = (busType.compare("NVME", Qt::CaseInsensitive) == 0) || devicePath.startsWith("/dev/nvme");
            bool isScsiForIcon = i.isSCSI || isNvme;

            // Convert child devices (APFS volumes on macOS) to QStringList
            QStringList childDevices;
            for (auto &s: i.childDevices)
            {
                childDevices.append(QString::fromStdString(s));
            }

            _drivelist[deviceNamePlusSize] = new DriveListItem(QString::fromStdString(i.device), QString::fromStdString(i.description), i.size, i.isUSB, isScsiForIcon, i.isReadOnly, isSystemOverride, mountpoints, childDevices, this);
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

QStringList DriveListModel::getChildDevices(const QString &device) const
{
    // Search through cached drive list for matching device
    for (auto it = _drivelist.cbegin(); it != _drivelist.cend(); ++it)
    {
        DriveListItem *item = it.value();
        if (item && item->property("device").toString() == device)
        {
            return item->property("childDevices").toStringList();
        }
    }
    return QStringList();
}