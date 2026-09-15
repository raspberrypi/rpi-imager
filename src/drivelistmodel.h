#ifndef DRIVELISTMODEL_H
#define DRIVELISTMODEL_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QAbstractItemModel>
#include <QMap>
#include <QHash>
#include <QSet>
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
     * @brief The scanning state pausePolling()/resumePolling() set
     *
     * The setters above are forwarded to the poll thread; this is the
     * matching reader, so a caller -- or a test -- can tell whether scanning
     * actually came back.
     */
    DriveListModelPollThread::ScanMode scanMode() const { return _thread.scanMode(); }

    /** @brief Whether the poll is looking for fastboot storage devices */
    bool fastbootScanEnabled() const { return _thread.fastbootScanEnabled(); }
    
    /**
     * @brief Set slow polling mode (reduced frequency)
     * 
     * Use this after write completion when user is viewing results
     * but doesn't need frequent drive list updates.
     */
    void setSlowPolling();
    void setRpibootEnabled(bool enabled);

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

    void setFastbootScanEnabled(bool enabled);

    /**
     * @brief Note that a device at this USB port path is mid-bootstrap
     *
     * A device being bootstrapped leaves the bus entirely between rpiboot
     * finishing and the fastboot gadget enumerating — up to a minute, on the
     * gadget's own re-enumerate budget — and it is neither an rpiboot device
     * nor a fastboot one while it boots. Told that a bootstrap is in flight,
     * the model holds the chip it learned for that port rather than dropping
     * it, so the "Connected via USB" annotation stays put across the handover
     * instead of blinking off at the point the device is busiest.
     *
     * The hold is released by the model itself, on the first poll that sees
     * the port again in either mode — waiting for that rather than for the
     * bootstrap's own completion signal, which fires before the poll thread
     * has had a chance to probe the new gadget. A bootstrap that fails clears
     * the hold through this same call with @p inFlight false, and the hold
     * expires anyway after a few polls that miss the port, so a device
     * unplugged mid-bootstrap cannot leave the annotation stuck on. Polling is
     * paused for the duration of a bootstrap, so that budget is only spent
     * once the device is genuinely expected back.
     *
     * @param portPathKey Dotted USB port path (e.g. "1.2")
     * @param inFlight    true when the bootstrap starts, false when it fails
     */
    void setBootstrapInFlight(const QString &portPathKey, bool inFlight);

    enum driveListRoles {
        deviceRole = Qt::UserRole + 1, descriptionRole, sizeRole, isUsbRole, isScsiRole, isReadOnlyRole, isSystemRole, mountpointsRole, childDevicesRole,
        isRpibootRole,
        isFastbootStorageRole, fastbootBlockDeviceRole, fastbootStorageTypeRole
    };

signals:
    void deviceRemoved(const QString &device);
    void eventDriveListPoll(quint32 durationMs);
    void connectedRpibootChipsChanged(const QStringList &chips);
    
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

    void rpibootDeviceDetected(const QString &deviceId,
                               uint8_t busNumber, uint8_t deviceAddress,
                               const QList<uint8_t> &portPath, uint16_t productId);

public slots:
    void processDriveList(std::vector<Drivelist::DeviceDescriptor> l);

private slots:
    // What the poll thread's results come in through.
    //
    // stop() only raises a flag: a poll already under way finishes and emits,
    // and that emission is queued to this thread, so it arrives after
    // stopPolling() has returned. This drops it, which is what makes
    // "stopped" mean the list cannot change again -- during a write, where
    // polling is stopped to keep off the device, and in the tests, where a
    // cleared list quietly refilled itself with the machine's own disks.
    void onPolledDriveList(std::vector<Drivelist::DeviceDescriptor> l);

protected:
    bool _polling = false;
    QMap<QString,DriveListItem *> _drivelist;
    QHash<int, QByteArray> _rolenames;
    DriveListModelPollThread _thread;
    QString _lastError;  // Last enumeration error message (empty if successful)
    QStringList _connectedRpibootChips;
    // Chip generation last seen at each USB port path, keyed by dotted port
    // path.  Learned from a device's USB PID in rpiboot mode and from the
    // gadget's revision-processor getvar in fastboot mode, so one connected
    // device keeps naming its silicon across the transition between the two.
    QHash<QString, QString> _chipByPort;
    // Port paths whose device is mid-bootstrap and so briefly on neither side
    // of that transition, against the number of further polls that may miss
    // the port before the hold is given up.  See setBootstrapInFlight().
    QHash<QString, int> _bootstrapPorts;
    // Polls a mid-bootstrap port may be absent for before its chip is
    // forgotten.  The gadget only has to enumerate and answer one getvar, so
    // this is generous; polling ticks once a second.
    static constexpr int kBootstrapHoldPolls = 10;
    // Tracks naked rpiboot devices we've already emitted rpibootDeviceDetected
    // for.  We deliberately keep these out of _drivelist (they aren't writable
    // storage until bootstrap converts them to fastboot mode) but still need
    // a per-device "have we seen this one already" signal to avoid spamming
    // auto-bootstrap on every poll.
    QSet<QString> _seenRpibootDevices;
};

#endif // DRIVELISTMODEL_H
