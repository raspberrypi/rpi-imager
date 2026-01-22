#ifndef DRIVELISTMODELPOLLTHREAD_H
#define DRIVELISTMODELPOLLTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include "drivelist/drivelist.h"

/**
 * @brief Background thread for polling available storage devices
 * 
 * Supports adaptive scanning behavior:
 * - Normal mode: Scans every 1 second (for device selection UI)
 * - Paused mode: No scanning (during write operations to avoid contention)
 * - Slow mode: Scans every 5 seconds (during final stages, minimal UI updates)
 * 
 * Pausing scanning during write operations prevents:
 * - I/O contention on the target device
 * - Device lock conflicts on Windows
 * - Unnecessary CPU/disk activity during critical operations
 */
class DriveListModelPollThread : public QThread
{
    Q_OBJECT
public:
    /**
     * @brief Scanning mode for adaptive behavior
     */
    enum class ScanMode {
        Normal,     ///< Scan every 1 second (default, for UI)
        Slow,       ///< Scan every 5 seconds (low priority)
        Paused      ///< No scanning (during write/verify operations)
    };

    DriveListModelPollThread(QObject *parent = nullptr);
    ~DriveListModelPollThread();
    void start();
    void stop();
    
    /**
     * @brief Set the scanning mode
     * 
     * Thread-safe. Takes effect on next poll cycle.
     * 
     * @param mode New scanning mode
     */
    void setScanMode(ScanMode mode);
    
    /**
     * @brief Get current scanning mode
     * @return Current mode
     */
    ScanMode scanMode() const;
    
    /**
     * @brief Convenience method to pause scanning
     * 
     * Equivalent to setScanMode(ScanMode::Paused)
     */
    void pause();
    
    /**
     * @brief Convenience method to resume normal scanning
     * 
     * Equivalent to setScanMode(ScanMode::Normal)
     */
    void resume();

protected:
    bool _terminate;
    ScanMode _scanMode;
    mutable QMutex _mutex;
    QWaitCondition _modeChanged;
    
    virtual void run() override;

signals:
    void newDriveList(std::vector<Drivelist::DeviceDescriptor> list);
    
    /**
     * @brief Emitted when scan mode changes
     * @param mode New scan mode
     */
    void scanModeChanged(ScanMode mode);
    
    /**
     * @brief Emitted after each drive enumeration with timing
     * @param durationMs Time taken for drive enumeration
     */
    void eventDriveListPoll(quint32 durationMs);
};

#endif // DRIVELISTMODELPOLLTHREAD_H
