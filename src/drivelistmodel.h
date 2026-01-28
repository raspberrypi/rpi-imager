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
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit DriveListModel(QObject *parent = nullptr);
    
    // QAbstractListModel overrides
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
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
    
    /**
     * @brief Get the last enumeration error message
     * 
     * Returns the error message from the most recent failed drive enumeration,
     * or empty string if enumeration succeeded.
     */
    QString lastError() const { return _lastError; }

    enum driveListRoles {
        deviceRole = Qt::UserRole + 1, descriptionRole, sizeRole, isUsbRole, isScsiRole, isReadOnlyRole, isSystemRole, mountpointsRole, childDevicesRole
    };

signals:
    void deviceRemoved(const QString &device);
    void eventDriveListPoll(quint32 durationMs);
    
    /**
     * @brief Emitted when the lastError property changes
     * 
     * Used by Qt's property binding system. For UI notifications,
     * connect to enumerationError() instead which includes the message.
     */
    void lastErrorChanged();
    
    /**
     * @brief Emitted when drive enumeration fails or recovers
     * 
     * @param errorMessage Human-readable error description, or empty if recovered
     * 
     * The UI should display this error to the user and potentially
     * offer troubleshooting steps (e.g., "Try running as administrator").
     */
    void enumerationError(const QString &errorMessage);

public slots:
    void processDriveList(std::vector<Drivelist::DeviceDescriptor> l);

protected:
    QMap<QString,DriveListItem *> _drivelist;
    QHash<int, QByteArray> _rolenames;
    DriveListModelPollThread _thread;
    QString _lastError;  // Last enumeration error message (empty if successful)
};

#endif // DRIVELISTMODEL_H
