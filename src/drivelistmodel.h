#ifndef DRIVELISTMODEL_H
#define DRIVELISTMODEL_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QAbstractItemModel>
#include <QMap>
#include <QHash>
#ifndef CLI_ONLY_BUILD
#include <QQmlEngine>
#endif
#include "drivelistitem.h"
#include "drivelistmodelpollthread.h"

class DriveListModel : public QAbstractListModel
{
    Q_OBJECT
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
#endif
public:
    DriveListModel(QObject *parent = nullptr);
    virtual int rowCount(const QModelIndex &) const;
    virtual QHash<int, QByteArray> roleNames() const;
    virtual QVariant data(const QModelIndex &index, int role) const;
    void startPolling();
    void stopPolling();
    
    /**
     * @brief Pause drive scanning (during write operations)
     * 
     * Call this when starting a write operation to avoid I/O contention
     * and device lock conflicts, especially on Windows.
     */
    void pausePolling();
    
    /**
     * @brief Resume normal drive scanning
     * 
     * Call this after write operation completes and user returns to
     * device selection.
     */
    void resumePolling();
    
    /**
     * @brief Set slow polling mode (reduced frequency)
     * 
     * Use this after write completion when user is viewing results
     * but doesn't need frequent drive list updates.
     */
    void setSlowPolling();
    
    /**
     * @brief Get child devices (e.g., APFS volumes) for a given device path
     * 
     * This avoids re-scanning the drive list during unmount operations.
     * Returns cached child devices from the last drive list poll.
     * 
     * @param device Device path (e.g., "/dev/disk6")
     * @return List of child device paths, empty if device not found
     */
    Q_INVOKABLE QStringList getChildDevices(const QString &device) const;

    enum driveListRoles {
        deviceRole = Qt::UserRole + 1, descriptionRole, sizeRole, isUsbRole, isScsiRole, isReadOnlyRole, isSystemRole, mountpointsRole, childDevicesRole
    };

signals:
    void deviceRemoved(const QString &device);
    void eventDriveListPoll(quint32 durationMs);

public slots:
    void processDriveList(std::vector<Drivelist::DeviceDescriptor> l);

protected:
    QMap<QString,DriveListItem *> _drivelist;
    QHash<int, QByteArray> _rolenames;
    DriveListModelPollThread _thread;
};

#endif // DRIVELISTMODEL_H
