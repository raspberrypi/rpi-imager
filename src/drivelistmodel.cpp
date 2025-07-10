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

            _drivelist[deviceNamePlusSize] = new DriveListItem(QString::fromStdString(i.device), QString::fromStdString(i.description), i.size, i.isUSB, i.isSCSI, i.isReadOnly, i.isSystem, mountpoints, this);
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

            _drivelist.value(device)->deleteLater();
            _drivelist.remove(device);
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
