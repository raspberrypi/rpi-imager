/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "drivelistmodel.h"
#include "config.h"
#include "drivelist/drivelist.h"
#include <QSet>
#include <QDebug>
#include <QFile>

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
        {childDevicesRole, "childDevices"},
        {isRpibootRole, "isRpiboot"},
        {isFastbootStorageRole, "isFastbootStorage"},
        {fastbootBlockDeviceRole, "fastbootBlockDevice"},
        {fastbootStorageTypeRole, "fastbootStorageType"}
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
        bool isRpiboot = false;
        bool isFastbootStorage = false;
        QString fastbootBlockDevice;
        QString fastbootStorageType;
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

        // Hide drives that have an .rpiignore file on any mountpoint
        bool hasRpiIgnore = false;
        for (const auto &mountpoint : mountpoints) {
            if (QFile::exists(mountpoint + "/.rpiignore")) {
                hasRpiIgnore = true;
                break;
            }
        }
        if (hasRpiIgnore)
            continue;

        bool isRpibootDevice = i.isRpiboot;
        bool isFastbootStorage = i.isFastbootStorage;

        // Skip zero-sized devices (but not rpiboot/fastboot devices)
        if (i.size == 0 && !isRpibootDevice && !isFastbootStorage)
            continue;

        // Filter virtual devices (loop devices, APFS volumes, VHDs, Storage Spaces).
        // Read-only and system virtual devices are always hidden.
        // On macOS/Windows, also require removable/ejectable — isSystem detection
        // can miss edge cases (e.g., APFS on non-ejectable external enclosures,
        // non-system Storage Spaces pools).
        // On Linux, isSystem is set by explicit mountpoint checks (/, /usr, /var,
        // /home, /boot, /snap/*) which is sufficient — and loop devices created
        // via losetup are never removable, so requiring it would hide them.
        if (i.isVirtual) {
            if (i.isReadOnly || i.isSystem)
                continue;
#ifndef Q_OS_LINUX
            if (!i.isRemovable)
                continue;
#endif
        }

        QString deviceNamePlusSize = QString::fromStdString(i.uniqueKey());
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
            info.isRpiboot = isRpibootDevice;
            info.isFastbootStorage = isFastbootStorage;
            info.fastbootBlockDevice = QString::fromStdString(i.fastbootBlockDevice);
            info.fastbootStorageType = QString::fromStdString(i.fastbootStorageType);
            drivesToAdd.append(info);

            // When a new rpiboot device appears, emit signal for auto-bootstrap
            if (isRpibootDevice) {
                QList<uint8_t> portPath(i.usbPortPath.begin(), i.usbPortPath.end());
                // Parse bus:addr from the synthetic device URI (rpiboot://bus:addr:portpath:pid)
                QString devUri = QString::fromStdString(i.device);
                uint8_t bus = 0, addr = 0;
                QString devPath = devUri.startsWith("rpiboot://") ? devUri.mid(10) : devUri;
                QStringList uriParts = devPath.split(':');
                if (uriParts.size() >= 2) {
                    bus = static_cast<uint8_t>(uriParts[0].toUInt());
                    addr = static_cast<uint8_t>(uriParts[1].toUInt());
                }
                emit rpibootDeviceDetected(devUri, bus, addr, portPath, i.rpibootPid);
            }
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
            info.mountpoints, info.childDevices,
            info.isRpiboot,
            info.isFastbootStorage, info.fastbootBlockDevice, info.fastbootStorageType,
            this);
        endInsertRows();

        qDebug() << "Drive added:" << info.device;
    }

    // Extract connected rpiboot chip names and notify if changed
    QStringList newChips;
    for (const auto &i : l) {
        if (i.isRpiboot && !i.rpibootChipName.empty()) {
            QString chip = QString::fromStdString(i.rpibootChipName);
            if (!newChips.contains(chip))
                newChips.append(chip);
        }
    }
    newChips.sort();
    if (newChips != _connectedRpibootChips) {
        _connectedRpibootChips = newChips;
        emit connectedRpibootChipsChanged(_connectedRpibootChips);
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

void DriveListModel::setRpibootEnabled(bool enabled)
{
    _thread.setRpibootEnabled(enabled);
}

void DriveListModel::setFastbootScanEnabled(bool enabled)
{
    _thread.setFastbootScanEnabled(enabled);
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