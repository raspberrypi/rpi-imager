/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "drivelistmodel.h"
#include "config.h"
#include "drivelist/drivelist.h"
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
    connect(&_thread, &DriveListModelPollThread::newDriveList,
            this, &DriveListModel::processDriveList);
    
    // Forward performance event signal
    connect(&_thread, &DriveListModelPollThread::eventDriveListPoll,
            this, &DriveListModel::eventDriveListPoll);
}

int DriveListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
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
    // Check for enumeration error sentinel
    // If present, emit error signal and don't process further
    // The sentinel has device == "__error__" and error contains the message
    if (!l.empty() && l[0].device == "__error__") {
        QString errorMsg = QString::fromStdString(l[0].error);
        if (_lastError != errorMsg) {
            _lastError = errorMsg;
            qWarning() << "Drive enumeration failed:" << errorMsg;
            emit lastErrorChanged();        // For Q_PROPERTY binding
            emit enumerationError(errorMsg); // For QML signal handler
        }
        // Don't clear the existing drive list - keep showing what we had
        // This prevents UI flicker during transient errors
        return;
    }
    
    // Clear any previous error since enumeration succeeded
    if (!_lastError.isEmpty()) {
        _lastError.clear();
        emit lastErrorChanged();         // For Q_PROPERTY binding
        emit enumerationError(QString()); // For QML signal handler (empty = cleared)
    }
    
    QSet<QString> drivesInNewList;

    // First pass: collect all valid drives from the new list
    // We need to do this before modifications to correctly calculate row indices
    struct NewDriveInfo {
        QString key;
        QString device;
        QString description;
        quint64 size;
        bool isUSB;
        bool isScsi;
        bool isReadOnly;
        bool isSystem;
        QStringList mountpoints;
        QStringList childDevices;
    };
    QList<NewDriveInfo> drivesToAdd;

    for (const auto &i : l)
    {
        // Convert STL vector<string> to Qt QStringList
        QStringList mountpoints;
        for (const auto &s : i.mountpoints)
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
            // Mark virtual disks as system drives to trigger confirmation dialog
            const bool isSystemOverride = i.isSystem || i.isVirtual;

            // Treat NVMe drives like SCSI for icon purposes
            QString busType = QString::fromStdString(i.busType);
            QString devicePath = QString::fromStdString(i.device);
            bool isNvme = (busType.compare("NVME", Qt::CaseInsensitive) == 0) || devicePath.startsWith("/dev/nvme");
            bool isScsiForIcon = i.isSCSI || isNvme;

            // Convert child devices (APFS volumes on macOS) to QStringList
            QStringList childDevices;
            for (const auto &s : i.childDevices)
            {
                childDevices.append(QString::fromStdString(s));
            }

            NewDriveInfo info;
            info.key = deviceNamePlusSize;
            info.device = QString::fromStdString(i.device);
            info.description = QString::fromStdString(i.description);
            info.size = i.size;
            info.isUSB = i.isUSB;
            info.isScsi = isScsiForIcon;
            info.isReadOnly = i.isReadOnly;
            info.isSystem = isSystemOverride;
            info.mountpoints = mountpoints;
            info.childDevices = childDevices;
            drivesToAdd.append(info);
        }
    }

    // Remove drives that are no longer present (iterate in reverse to maintain valid indices)
    QStringList drivesInOldList = _drivelist.keys();
    for (int i = drivesInOldList.size() - 1; i >= 0; --i)
    {
        const QString &key = drivesInOldList.at(i);
        if (!drivesInNewList.contains(key))
        {
            // Find the row index for this key
            int row = _drivelist.keys().indexOf(key);
            if (row >= 0)
            {
                QString devicePath = _drivelist.value(key)->property("device").toString();
                qDebug() << "Drive removed:" << devicePath;

                beginRemoveRows(QModelIndex(), row, row);
                _drivelist.value(key)->deleteLater();
                _drivelist.remove(key);
                endRemoveRows();

                // Emit signal for this specific device removal
                emit deviceRemoved(devicePath);
            }
        }
    }

    // Add new drives
    for (const auto &info : drivesToAdd)
    {
        // Calculate the row index where this key will be inserted
        // QMap is sorted, so we need to find where this key fits
        int row = 0;
        for (auto it = _drivelist.constBegin(); it != _drivelist.constEnd(); ++it)
        {
            if (it.key() >= info.key)
                break;
            ++row;
        }

        beginInsertRows(QModelIndex(), row, row);
        _drivelist[info.key] = new DriveListItem(
            info.device, info.description, info.size,
            info.isUSB, info.isScsi, info.isReadOnly, info.isSystem,
            info.mountpoints, info.childDevices, this);
        endInsertRows();

        qDebug() << "Drive added:" << info.device;
    }
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