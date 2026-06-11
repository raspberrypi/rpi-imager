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

#include <thread>
#include <vector>

RpibootThread::RpibootThread(const DeviceInfo& device,
                               rpiboot::SideloadMode mode,
                               QObject* parent)
    : QThread(parent), _device(device), _mode(mode)
{
}

RpibootThread::~RpibootThread()
{
    cancel();
    if (!wait(10000)) {
        // See FastbootFlashThread::~FastbootFlashThread() for the same pattern.
        // Force-kill if stuck (e.g. libusb macOS deadlock during cleanup).
        qWarning() << "RpibootThread: thread did not finish within 10s after cancel, terminating";
        terminate();
        wait();
    }
}

void RpibootThread::cancel()
{
    _cancelled.store(true);
}

bool RpibootThread::runPhase(rpiboot::SideloadMode mode,
                              QString& fastbootId,
                              QString& bootcodeDiag,
                              QString& fileServeDiag)
{
    using namespace rpiboot;

    emit preparationStatusUpdate(tr("Downloading firmware..."));

    QElapsedTimer phaseTimer;
    phaseTimer.start();

    FirmwareManager fwMgr;
    if (!_customFastbootGadget.isEmpty())
        fwMgr.setCustomFastbootGadget(_customFastbootGadget.toStdString());
    if (!_signFastbootGadgetKey.isEmpty())
        fwMgr.setSignFastbootGadgetKey(_signFastbootGadgetKey.toStdString());
    auto fwDir = fwMgr.ensureAvailable(mode, _device.chipGeneration,
        [this](uint64_t current, uint64_t total, const std::string& status) {
            emit preparationStatusUpdate(QString::fromStdString(status));
            emit progressChanged(current, total);
        }, _cancelled);

    if (fwDir.empty()) {
        emit eventFirmwareSetup(static_cast<quint32>(phaseTimer.elapsed()), false,
                                QString::fromStdString(fwMgr.lastError()));
        emit error(tr("Failed to obtain rpiboot firmware: %1")
                   .arg(QString::fromStdString(fwMgr.lastError())));
        return false;
    }

    emit eventFirmwareSetup(static_cast<quint32>(phaseTimer.elapsed()), true,
                            QString::fromStdString(fwDir.string()));

    if (_cancelled.load()) return false;

    emit preparationStatusUpdate(tr("Connecting to device..."));

    phaseTimer.restart();

    RpibootProtocol protocol;
    auto progressCb = [this](uint64_t current, uint64_t total, const std::string& status) {
        emit preparationStatusUpdate(QString::fromStdString(status));
        emit progressChanged(current, total);
    };

    bootcodeDiag.clear();
    fileServeDiag.clear();

    UsbDeviceInfo fileServerDevice;
    {
        fileServerDevice.busNumber      = _device.busNumber;
        fileServerDevice.deviceAddress  = _device.deviceAddress;
        fileServerDevice.vendorId       = BROADCOM_VID;
        fileServerDevice.productId      = _device.productId;
        fileServerDevice.portPath       = _device.portPath;
        fileServerDevice.chipGeneration = _device.chipGeneration;

        LibusbContext ctx;
        for (const auto& dev : ctx.scanBootDevices()) {
            bool matches = (!_device.portPath.empty())
                ? (dev.portPath == _device.portPath)
                : (dev.busNumber == _device.busNumber &&
                   dev.deviceAddress == _device.deviceAddress);
            if (matches) {
                fileServerDevice = dev;
                fileServerDevice.chipGeneration = _device.chipGeneration;
                break;
            }
        }
    }

    const bool needsBootcode = (fileServerDevice.serialNumberIndex == 0 ||
                                 fileServerDevice.serialNumberIndex == 3);
    qDebug() << "rpiboot: device serial#" << fileServerDevice.serialNumberIndex
             << "mode" << static_cast<int>(mode)
             << (needsBootcode ? "→ upload bootcode" : "→ skip to file server");

    if (needsBootcode) {
        try {
            LibusbContext ctx;
            auto transport = ctx.openDevice(fileServerDevice);
            if (!transport || !transport->isOpen()) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QStringLiteral("Failed to open USB device"));
                emit error(tr("Failed to open USB device"));
                return false;
            }

            bootcodeDiag = transport->initDiagnostics();
            if (!protocol.uploadBootcode(*transport, _device.chipGeneration,
                                          fwDir, progressCb, _cancelled)) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QStringLiteral("bc_usb=[%1] %2").arg(bootcodeDiag,
                                          QString::fromStdString(protocol.lastError())));
                if (!_cancelled.load())
                    emit error(tr("rpiboot protocol failed: %1")
                               .arg(QString::fromStdString(protocol.lastError())));
                return false;
            }
        } catch (const std::exception& e) {
            emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                      QString::fromUtf8(e.what()));
            emit error(tr("USB error: %1").arg(e.what()));
            return false;
        }

        if (_cancelled.load()) return false;

        emit preparationStatusUpdate(tr("Waiting for device to restart..."));
        if (!waitForBootDeviceReEnum(fileServerDevice))
            return false;
    }

    if (_cancelled.load()) return false;

    _nextStageFound.store(false);
    _stopScanner.store(false);
    const uint8_t priorDeviceAddress = fileServerDevice.deviceAddress;

    std::thread nextStageScanner;
    if (mode == SideloadMode::Fastboot) {
        nextStageScanner = std::thread([this, &fastbootId]() {
            pollForFastbootDevice(_nextStageFound, fastbootId);
        });
    } else if (mode == SideloadMode::SecureBootRecovery) {
        nextStageScanner = std::thread([this, priorDeviceAddress]() {
            pollForRpibootReturn(_nextStageFound, priorDeviceAddress);
        });
    }

    struct ScannerGuard {
        std::thread& thread;
        std::atomic<bool>& stopScanner;
        ~ScannerGuard() {
            if (thread.joinable()) {
                stopScanner.store(true);
                thread.join();
            }
        }
    } scannerGuard{nextStageScanner, _stopScanner};

    {
        bool fileServerOk = false;
        try {
            LibusbContext ctx;
            auto transport = ctx.openDevice(fileServerDevice);
            if (!transport || !transport->isOpen()) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QStringLiteral("Failed to open USB device after re-enumeration"));
                emit error(tr("Failed to open USB device after re-enumeration"));
                return false;
            }

            fileServeDiag = transport->initDiagnostics();

            std::atomic<bool> combinedCancel{false};
            auto wrappedProgress = [&](uint64_t current, uint64_t total, const std::string& status) {
                if (_nextStageFound.load() || _cancelled.load())
                    combinedCancel.store(true);
                progressCb(current, total, status);
            };
            fileServerOk = protocol.serveFiles(*transport, _device.chipGeneration, mode,
                                                fwDir, wrappedProgress, combinedCancel);
        } catch (const std::exception& e) {
            if (!_nextStageFound.load()) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QString::fromUtf8(e.what()));
                emit error(tr("USB error: %1").arg(e.what()));
                return false;
            }
            fileServerOk = true;
        }

        if (_nextStageFound.load())
            fileServerOk = true;

        if (!fileServerOk) {
            emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                      QStringLiteral("fs_usb=[%1] %2").arg(fileServeDiag,
                                      QString::fromStdString(protocol.lastError())));
            if (!_cancelled.load())
                emit error(tr("rpiboot protocol failed: %1")
                           .arg(QString::fromStdString(protocol.lastError())));
            return false;
        }
    }

    emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), true,
                              QStringLiteral("chip=%1; mode=%2; bc_usb=[%3]; fs_usb=[%4]")
                                  .arg(static_cast<int>(_device.chipGeneration))
                                  .arg(static_cast<int>(mode))
                                  .arg(bootcodeDiag)
                                  .arg(fileServeDiag));

    if (_cancelled.load())
        return false;

    switch (mode) {
    case SideloadMode::Fastboot:
        if (_nextStageFound.load()) {
            qDebug() << "Fastboot device already detected during file server phase";
        } else {
            emit preparationStatusUpdate(tr("Waiting for fastboot device..."));
            phaseTimer.restart();
        }
        if (nextStageScanner.joinable())
            nextStageScanner.join();

        if (!_nextStageFound.load() && !_cancelled.load()) {
            emit eventFastbootWait(static_cast<quint32>(phaseTimer.elapsed()), false);
            emit error(tr("Timed out waiting for fastboot device to appear."));
            return false;
        }
        emit eventFastbootWait(static_cast<quint32>(phaseTimer.elapsed()), true);
        break;

    case SideloadMode::SecureBootRecovery:
        if (_nextStageFound.load())
            qDebug() << "SBR: device re-enumerated into rpiboot post-recovery";
        if (nextStageScanner.joinable())
            nextStageScanner.join();
        break;
    }

    return true;
}

void RpibootThread::run()
{
    using namespace rpiboot;

    std::vector<SideloadMode> phases;
    if (_reprovisionDevice && _device.chipGeneration == ChipGeneration::BCM2712) {
        phases = {SideloadMode::SecureBootRecovery, SideloadMode::Fastboot};
        qDebug() << "rpiboot: special re-provision — SBR then fastboot";
    } else {
        phases = {_mode};
    }

    QString fastbootId;
    QString bootcodeDiag;
    QString fileServeDiag;

    for (size_t i = 0; i < phases.size(); ++i) {
        const auto phase = phases[i];
        if (i > 0) {
            emit preparationStatusUpdate(tr("Re-provisioning complete, starting fastboot..."));
            _nextStageFound.store(false);
        }

        if (!runPhase(phase, fastbootId, bootcodeDiag, fileServeDiag))
            return;

        if (_cancelled.load())
            return;
    }

    const auto finalMode = phases.back();
    if (finalMode == SideloadMode::Fastboot) {
        emit fastbootDeviceReady(fastbootId);
    } else if (finalMode == SideloadMode::SecureBootRecovery) {
        emit success();
    }
}

bool RpibootThread::waitForBootDeviceReEnum(rpiboot::UsbDeviceInfo& outDevice)
{
    using namespace rpiboot;

    auto matchesPort = [this](const UsbDeviceInfo& dev) {
        if (!_device.portPath.empty())
            return dev.portPath == _device.portPath;
        if (_device.busNumber != 0)
            return dev.busNumber == _device.busNumber &&
                   dev.deviceAddress == _device.deviceAddress;
        qWarning() << "rpiboot: no port path or bus info — accepting any Broadcom boot device";
        return true;
    };

    LibusbContext pollCtx;

    constexpr int DISCONNECT_POLLS = 6;
    for (int i = 0; i < DISCONNECT_POLLS; ++i) {
        if (_cancelled.load()) return false;
        emit preparationStatusUpdate(tr("Waiting for device to disconnect (%1/%2)...")
                                         .arg(i + 1).arg(DISCONNECT_POLLS));
        QThread::msleep(500);

        try {
            auto devices = pollCtx.scanBootDevices();
            bool stillPresent = false;
            for (const auto& dev : devices) {
                if (matchesPort(dev) &&
                    (dev.serialNumberIndex == 0 || dev.serialNumberIndex == 3)) {
                    stillPresent = true;
                    break;
                }
            }
            if (!stillPresent) {
                emit preparationStatusUpdate(tr("Device disconnected, waiting for reconnect..."));
                break;
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during disconnect wait:" << e.what();
            break;
        }
    }

    if (_cancelled.load()) return false;

    constexpr int RECONNECT_POLLS = 30;
    for (int i = 0; i < RECONNECT_POLLS; ++i) {
        if (_cancelled.load()) return false;

        emit preparationStatusUpdate(tr("Waiting for device to reconnect (%1/%2s)...")
                                         .arg((i + 1) / 2).arg(RECONNECT_POLLS / 2));

        try {
            auto devices = pollCtx.scanBootDevices();
            for (const auto& dev : devices) {
                if (!matchesPort(dev))
                    continue;
                if (dev.serialNumberIndex == 0 || dev.serialNumberIndex == 3)
                    continue;
                qDebug() << "Device re-enumerated at bus" << dev.busNumber
                         << "addr" << dev.deviceAddress
                         << "serialNum#" << dev.serialNumberIndex;
                outDevice = dev;
                return true;
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during reconnect wait:" << e.what();
        }

        QThread::msleep(500);
    }

    emit error(tr("Timed out waiting for device to re-enumerate after bootcode upload (waited %1s).")
               .arg(RECONNECT_POLLS / 2));
    return false;
}

bool RpibootThread::pollForFastbootDevice(std::atomic<bool>& found, QString& fastbootId)
{
    using namespace rpiboot;

    LibusbContext pollCtx;

    constexpr int FB_POLLS = 120;
    for (int attempt = 0; attempt < FB_POLLS; ++attempt) {
        if (_cancelled.load() || _stopScanner.load()) return false;

        QThread::msleep(500);

        try {
            auto devices = pollCtx.scanFastbootDevices();

            for (const auto& dev : devices) {
                if (!_device.portPath.empty() && dev.portPath == _device.portPath) {
                    fastbootId = QStringLiteral("%1:%2")
                        .arg(dev.busNumber)
                        .arg(dev.deviceAddress);
                    qDebug() << "Fastboot device appeared on same port:" << fastbootId;
                    found.store(true);
                    return true;
                }
            }

            if (_device.portPath.empty() && devices.size() == 1) {
                const auto& dev = devices[0];
                fastbootId = QStringLiteral("%1:%2")
                    .arg(dev.busNumber)
                    .arg(dev.deviceAddress);
                qDebug() << "Fastboot device appeared (no port path, sole device):" << fastbootId;
                found.store(true);
                return true;
            } else if (_device.portPath.empty() && devices.size() > 1) {
                qWarning() << "rpiboot: multiple fastboot devices present but no port path"
                           << "— cannot identify which device to use";
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during fastboot wait:" << e.what();
            continue;
        }
    }

    return false;
}

bool RpibootThread::pollForRpibootReturn(std::atomic<bool>& found,
                                          uint8_t priorDeviceAddress)
{
    using namespace rpiboot;

    LibusbContext pollCtx;

    constexpr int POLLS = 120;
    for (int attempt = 0; attempt < POLLS; ++attempt) {
        if (_cancelled.load() || _stopScanner.load()) return false;

        QThread::msleep(500);

        try {
            auto devices = pollCtx.scanBootDevices();
            for (const auto& dev : devices) {
                const bool portMatches = !_device.portPath.empty() &&
                                         dev.portPath == _device.portPath;
                const bool isFreshAddress = (dev.deviceAddress != priorDeviceAddress);
                if (portMatches && isFreshAddress) {
                    qDebug() << "SBR: rpiboot device returned on same port path "
                                "(bus" << dev.busNumber
                             << "addr" << dev.deviceAddress
                             << "vs prior addr" << priorDeviceAddress << ")";
                    found.store(true);
                    return true;
                }
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during SBR re-enum wait:" << e.what();
            continue;
        }
    }

    return false;
}
