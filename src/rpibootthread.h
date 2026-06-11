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

namespace rpiboot { struct UsbDeviceInfo; }

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
    void setCustomFastbootGadget(const QString &path) { _customFastbootGadget = path; }
    void setSignFastbootGadgetKey(const QString &keyPath) { _signFastbootGadgetKey = keyPath; }
    // Match rpi-sb-provisioner's special-reprovision-device: run the SBR
    // phase (re-sign recovery.bin, reuse cached pieeprom) before fastboot.
    void setReprovisionDevice(bool enabled) { _reprovisionDevice = enabled; }

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
    bool pollForFastbootDevice(std::atomic<bool>& found, QString& fastbootId);
    // SBR equivalent of pollForFastbootDevice: watches for the original
    // rpiboot device returning on the same port path after the EEPROM
    // write + reboot.  `priorDeviceAddress` is the address the device had
    // while file_server was running — a fresh enumeration assigns a
    // different address, which is how we tell "device returned" from
    // "device hasn't disappeared yet".
    bool pollForRpibootReturn(std::atomic<bool>& found, uint8_t priorDeviceAddress);
    bool waitForBootDeviceReEnum(rpiboot::UsbDeviceInfo& outDevice);
    bool runPhase(rpiboot::SideloadMode mode,
                  QString& fastbootId,
                  QString& bootcodeDiag,
                  QString& fileServeDiag);

    DeviceInfo _device;
    rpiboot::SideloadMode _mode;
    std::atomic<bool> _cancelled{false};
    std::atomic<bool> _stopScanner{false};
    // Next-stage device observed by the mode-specific scanner:
    //   Fastboot  → fastboot gadget enumerated on the same port path.
    //   SBR       → rpiboot device re-enumerated after the recovery reboot.
    // Either way, set by the scanner thread and read via the wrappedProgress
    // bridge that feeds file_server's cancellation flag.
    std::atomic<bool> _nextStageFound{false};
    QString _customFastbootGadget;
    QString _signFastbootGadgetKey;
    bool _reprovisionDevice = false;
};

#endif // RPIBOOTTHREAD_H
