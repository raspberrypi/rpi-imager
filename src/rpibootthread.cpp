/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "rpibootthread.h"
#include "rpiboot/libusb_transport.h"
#include "rpiboot/rpiboot_protocol.h"
#include "rpiboot/firmware_manager.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>

// Fastboot VID/PID for the RPi fastboot gadget
static constexpr uint16_t FASTBOOT_VID = 0x18d1;  // Google
static constexpr uint16_t FASTBOOT_PID = 0x4ee0;  // Fastboot

RpibootThread::RpibootThread(const DeviceInfo& device,
                               rpiboot::SideloadMode mode,
                               QObject* parent)
    : QThread(parent), _device(device), _mode(mode)
{
}

RpibootThread::~RpibootThread()
{
    cancel();
    if (!wait(5000))
        terminate();
}

void RpibootThread::cancel()
{
    _cancelled.store(true);
}

void RpibootThread::run()
{
    using namespace rpiboot;

    // ── Step 1: Ensure firmware is available ───────────────────────────
    emit preparationStatusUpdate(tr("Downloading firmware..."));

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    FirmwareManager fwMgr;
    auto fwDir = fwMgr.ensureAvailable(_mode, _device.chipGeneration,
        [this](uint64_t current, uint64_t total, const std::string& status) {
            emit preparationStatusUpdate(QString::fromStdString(status));
            emit progressChanged(current, total);
        }, _cancelled);

    if (fwDir.empty()) {
        emit eventFirmwareSetup(static_cast<quint32>(phaseTimer.elapsed()), false,
                                QString::fromStdString(fwMgr.lastError()));
        emit error(tr("Failed to obtain rpiboot firmware: %1")
                   .arg(QString::fromStdString(fwMgr.lastError())));
        return;
    }

    emit eventFirmwareSetup(static_cast<quint32>(phaseTimer.elapsed()), true,
                            QString::fromStdString(fwDir));

    if (_cancelled.load()) return;

    // ── Step 2: Open USB device and execute protocol ───────────────────
    emit preparationStatusUpdate(tr("Connecting to device..."));

    phaseTimer.restart();

    try {
        LibusbContext ctx;
        UsbDeviceInfo usbInfo;
        usbInfo.busNumber = _device.busNumber;
        usbInfo.deviceAddress = _device.deviceAddress;
        usbInfo.vendorId = BROADCOM_VID;
        usbInfo.productId = _device.productId;
        usbInfo.portPath = _device.portPath;
        usbInfo.chipGeneration = _device.chipGeneration;

        auto transport = ctx.openDevice(usbInfo);
        if (!transport || !transport->isOpen()) {
            emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                      QStringLiteral("Failed to open USB device"));
            emit error(tr("Failed to open USB device"));
            return;
        }

        RpibootProtocol protocol;
        bool ok = protocol.execute(*transport, _device.chipGeneration, _mode, fwDir,
            [this](uint64_t current, uint64_t total, const std::string& status) {
                emit preparationStatusUpdate(QString::fromStdString(status));
                emit progressChanged(current, total);
            }, _cancelled);

        if (!ok) {
            emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                      QString::fromStdString(protocol.lastError()));
            if (!_cancelled.load())
                emit error(tr("rpiboot protocol failed: %1")
                           .arg(QString::fromStdString(protocol.lastError())));
            return;
        }

        emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), true,
                                  QStringLiteral("chip=%1; mode=%2")
                                      .arg(static_cast<int>(_device.chipGeneration))
                                      .arg(static_cast<int>(_mode)));
    } catch (const std::exception& e) {
        emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                  QString::fromUtf8(e.what()));
        emit error(tr("USB error: %1").arg(e.what()));
        return;
    }

    if (_cancelled.load()) return;

    // ── Step 3: Wait for the target device to appear ───────────────────
    switch (_mode) {
    case SideloadMode::Fastboot:
        emit preparationStatusUpdate(tr("Waiting for fastboot device..."));
        phaseTimer.restart();
        {
            bool found = waitForFastbootDevice();
            emit eventFastbootWait(static_cast<quint32>(phaseTimer.elapsed()), found);
            if (!found)
                return;
        }
        break;

    case SideloadMode::SecureBootRecovery:
        // Recovery mode doesn't produce a new device to wait for
        emit success();
        break;
    }
}

bool RpibootThread::waitForFastbootDevice()
{
    using namespace rpiboot;

    // After rpiboot sideloads the fastboot gadget, the CM re-enumerates
    // with Google's fastboot VID/PID (0x18d1:0x4ee0) on the same physical
    // USB port. Match by port path to correlate the new device.
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (_cancelled.load()) return false;

        QThread::msleep(500);

        try {
            LibusbContext ctx;
            auto devices = ctx.scanFastbootDevices();

            for (const auto& dev : devices) {
                // Match by USB port path — same physical port, different VID/PID
                if (!_device.portPath.empty() && dev.portPath == _device.portPath) {
                    QString busAddr = QStringLiteral("%1:%2")
                        .arg(dev.busNumber)
                        .arg(dev.deviceAddress);
                    qDebug() << "Fastboot device appeared on same port:" << busAddr;
                    emit fastbootDeviceReady(busAddr);
                    return true;
                }
            }

            // If we don't have port path info, accept any fastboot device
            if (_device.portPath.empty() && !devices.empty()) {
                const auto& dev = devices[0];
                QString busAddr = QStringLiteral("%1:%2")
                    .arg(dev.busNumber)
                    .arg(dev.deviceAddress);
                qDebug() << "Fastboot device appeared (no port matching):" << busAddr;
                emit fastbootDeviceReady(busAddr);
                return true;
            }
        } catch (...) {
            continue;
        }
    }

    emit error(tr("Timed out waiting for fastboot device to appear."));
    return false;
}
