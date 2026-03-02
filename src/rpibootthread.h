/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * QThread that executes the rpiboot sideload protocol, then detects
 * the resulting fastboot device.
 */

#ifndef RPIBOOTTHREAD_H
#define RPIBOOTTHREAD_H

#include <QThread>
#include <QString>

#include "rpiboot/rpiboot_types.h"

class RpibootThread : public QThread
{
    Q_OBJECT
public:
    struct DeviceInfo {
        uint8_t  busNumber = 0;
        uint8_t  deviceAddress = 0;
        uint16_t productId = 0;
        std::vector<uint8_t> portPath;
        rpiboot::ChipGeneration chipGeneration = rpiboot::ChipGeneration::BCM2711;
    };

    explicit RpibootThread(const DeviceInfo& device,
                           rpiboot::SideloadMode mode,
                           QObject* parent = nullptr);
    ~RpibootThread() override;

    void cancel();

signals:
    void success();
    void error(QString msg);
    void preparationStatusUpdate(QString msg);
    void progressChanged(quint64 current, quint64 total);
    void fastbootDeviceReady(const QString& fastbootId);
    void eventFirmwareSetup(quint32 durationMs, bool success, QString metadata);
    void eventRpibootProtocol(quint32 durationMs, bool success, QString metadata);
    void eventFastbootWait(quint32 durationMs, bool success);

protected:
    void run() override;

private:
    bool waitForFastbootDevice();

    DeviceInfo _device;
    rpiboot::SideloadMode _mode;
    std::atomic<bool> _cancelled{false};
};

#endif // RPIBOOTTHREAD_H
