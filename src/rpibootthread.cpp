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

RpibootThread::RpibootThread(const DeviceInfo& device,
                               rpiboot::SideloadMode mode,
                               QObject* parent)
    : QThread(parent), _device(device), _mode(mode)
{
}

RpibootThread::~RpibootThread()
{
    cancel();
    // Do NOT terminate() — forceful thread termination skips destructors
    // for local variables (LibusbContext, unique_ptr<LibusbTransport>),
    // leaking USB device handles and preventing future libusb_open calls.
    // With all blocking points respecting _cancelled or using bounded
    // timeouts, the thread should exit within a few seconds.
    if (!wait(10000)) {
        qWarning() << "RpibootThread: thread did not finish within 10s after cancel";
    }
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
                            QString::fromStdString(fwDir.string()));

    if (_cancelled.load()) return;

    // ── Step 2: Upload bootcode, reconnect, serve files ─────────────────
    emit preparationStatusUpdate(tr("Connecting to device..."));

    phaseTimer.restart();

    RpibootProtocol protocol;
    auto progressCb = [this](uint64_t current, uint64_t total, const std::string& status) {
        emit preparationStatusUpdate(QString::fromStdString(status));
        emit progressChanged(current, total);
    };
    QString bootcodeDiag;   // USB init diagnostics from the bootcode-upload transport
    QString fileServeDiag;  // USB init diagnostics from the file-server transport

    // Scan to determine the device's current boot stage (iSerialNumber).
    // The upstream rpiboot dispatches per device based on iSerialNumber:
    //   iSerialNumber == 0 or 3  →  second_stage_boot() (upload bootcode)
    //   iSerialNumber > 3        →  file_server()        (serve files directly)
    // After a previous partial run the device may already be in second-stage
    // mode; attempting a bootcode upload against it would cause the control
    // transfer to be stalled/rejected.
    UsbDeviceInfo fileServerDevice;
    {
        // Populate from stored fields as a fallback (device not yet found).
        fileServerDevice.busNumber      = _device.busNumber;
        fileServerDevice.deviceAddress  = _device.deviceAddress;
        fileServerDevice.vendorId       = BROADCOM_VID;
        fileServerDevice.productId      = _device.productId;
        fileServerDevice.portPath       = _device.portPath;
        fileServerDevice.chipGeneration = _device.chipGeneration;

        LibusbContext ctx;
        for (const auto& dev : ctx.scanBootDevices()) {
            // Match by port path when available (survives address changes).
            bool matches = (!_device.portPath.empty())
                ? (dev.portPath == _device.portPath)
                : (dev.busNumber == _device.busNumber &&
                   dev.deviceAddress == _device.deviceAddress);
            if (matches) {
                fileServerDevice = dev;
                // Preserve the original chip generation — the second-stage
                // may present a different PID that maps to a different gen.
                fileServerDevice.chipGeneration = _device.chipGeneration;
                break;
            }
        }
    }

    // iSerialNumber 0 = ROM mode (needs bootcode); >3 = second-stage (ready for file server).
    const bool needsBootcode = (fileServerDevice.serialNumberIndex == 0 ||
                                 fileServerDevice.serialNumberIndex == 3);
    qDebug() << "rpiboot: device serial#" << fileServerDevice.serialNumberIndex
             << (needsBootcode ? "→ upload bootcode" : "→ skip to file server");

    if (needsBootcode) {
        // 2a: Upload bootcode (scoped transport — released after upload)
        {
            try {
                LibusbContext ctx;
                auto transport = ctx.openDevice(fileServerDevice);
                if (!transport || !transport->isOpen()) {
                    emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                              QStringLiteral("Failed to open USB device"));
                    emit error(tr("Failed to open USB device"));
                    return;
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
                    return;
                }
                // transport + ctx destroyed here — USB handle released cleanly
            } catch (const std::exception& e) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QString::fromUtf8(e.what()));
                emit error(tr("USB error: %1").arg(e.what()));
                return;
            }
        }

        if (_cancelled.load()) return;

        // 2b: Wait for device to re-enumerate after bootcode upload.
        emit preparationStatusUpdate(tr("Waiting for device to restart..."));
        if (!waitForBootDeviceReEnum(fileServerDevice))
            return;
    }

    if (_cancelled.load()) return;

    // 2c: Open transport and run file server (on the second-stage device).
    //
    // For Fastboot sideloading, the device reboots into the fastboot gadget
    // after receiving the firmware files — it does NOT send a "Done" command.
    // On Windows/WinUSB, this means the file server never gets a clean
    // disconnect signal; it just sees PIPE errors and garbage reads.
    //
    // To avoid blocking on pointless retries, we start scanning for the
    // fastboot device in parallel.  Once it appears the file server can
    // stop — the _fastbootFound flag is OR'd with _cancelled so the
    // file server's cancellation check picks it up.
    _fastbootFound.store(false);
    QString fastbootId;

    // Launch fastboot scanner in background for Fastboot mode
    std::thread fastbootScanner;
    if (_mode == SideloadMode::Fastboot) {
        fastbootScanner = std::thread([this, &fastbootId]() {
            pollForFastbootDevice(_fastbootFound, fastbootId);
        });
    }

    {
        bool fileServerOk = false;
        try {
            LibusbContext ctx;
            auto transport = ctx.openDevice(fileServerDevice);
            if (!transport || !transport->isOpen()) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QStringLiteral("Failed to open USB device after re-enumeration"));
                emit error(tr("Failed to open USB device after re-enumeration"));
                if (fastbootScanner.joinable()) {
                    _cancelled.store(true);
                    fastbootScanner.join();
                }
                return;
            }

            fileServeDiag = transport->initDiagnostics();

            // The file server checks cancelled.load() each iteration.
            // Use a local atomic that we set when fastboot appears.
            std::atomic<bool> combinedCancel{false};
            auto wrappedProgress = [&](uint64_t current, uint64_t total, const std::string& status) {
                // Check if fastboot appeared; if so, signal the file server to stop
                if (_fastbootFound.load())
                    combinedCancel.store(true);
                progressCb(current, total, status);
            };
            fileServerOk = protocol.serveFiles(*transport, _device.chipGeneration, _mode,
                                                fwDir, wrappedProgress, combinedCancel);
        } catch (const std::exception& e) {
            // If the fastboot device already appeared, this exception is expected
            // (the transport died because the device rebooted).
            if (!_fastbootFound.load()) {
                emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                          QString::fromUtf8(e.what()));
                emit error(tr("USB error: %1").arg(e.what()));
                if (fastbootScanner.joinable()) {
                    _cancelled.store(true);
                    fastbootScanner.join();
                }
                return;
            }
            fileServerOk = true;  // Fastboot found — the exception is benign
        }

        // If fastboot was found while the file server was running, that's success
        if (_fastbootFound.load())
            fileServerOk = true;

        if (!fileServerOk) {
            emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), false,
                                      QStringLiteral("fs_usb=[%1] %2").arg(fileServeDiag,
                                      QString::fromStdString(protocol.lastError())));
            if (!_cancelled.load())
                emit error(tr("rpiboot protocol failed: %1")
                           .arg(QString::fromStdString(protocol.lastError())));
            if (fastbootScanner.joinable()) {
                _cancelled.store(true);
                fastbootScanner.join();
            }
            return;
        }
    }

    emit eventRpibootProtocol(static_cast<quint32>(phaseTimer.elapsed()), true,
                              QStringLiteral("chip=%1; mode=%2; bc_usb=[%3]; fs_usb=[%4]")
                                  .arg(static_cast<int>(_device.chipGeneration))
                                  .arg(static_cast<int>(_mode))
                                  .arg(bootcodeDiag)
                                  .arg(fileServeDiag));

    if (_cancelled.load()) {
        if (fastbootScanner.joinable()) {
            _cancelled.store(true);
            fastbootScanner.join();
        }
        return;
    }

    // ── Step 3: Wait for the target device to appear ───────────────────
    switch (_mode) {
    case SideloadMode::Fastboot:
        if (_fastbootFound.load()) {
            // Already found during the file server phase
            qDebug() << "Fastboot device already detected during file server phase";
        } else {
            // File server exited cleanly (Done command) before fastboot appeared
            emit preparationStatusUpdate(tr("Waiting for fastboot device..."));
            phaseTimer.restart();
        }
        {
            // Wait for the background scanner to finish
            if (fastbootScanner.joinable())
                fastbootScanner.join();

            bool found = _fastbootFound.load();
            emit eventFastbootWait(static_cast<quint32>(phaseTimer.elapsed()), found);
            if (found) {
                emit fastbootDeviceReady(fastbootId);
            } else if (!_cancelled.load()) {
                emit error(tr("Timed out waiting for fastboot device to appear."));
                return;
            }
        }
        break;

    case SideloadMode::SecureBootRecovery:
        // Recovery mode doesn't produce a new device to wait for
        emit success();
        break;
    }
}

bool RpibootThread::waitForBootDeviceReEnum(rpiboot::UsbDeviceInfo& outDevice)
{
    using namespace rpiboot;

    auto matchesPort = [this](const UsbDeviceInfo& dev) {
        if (!_device.portPath.empty())
            return dev.portPath == _device.portPath;
        // No port path info — fall back to bus+address matching.
        // This is less reliable (address can change across re-enumeration)
        // but avoids accepting the wrong device in multi-CM setups.
        if (_device.busNumber != 0)
            return dev.busNumber == _device.busNumber &&
                   dev.deviceAddress == _device.deviceAddress;
        qWarning() << "rpiboot: no port path or bus info — accepting any Broadcom boot device";
        return true;
    };

    // Create a single libusb context for the entire polling sequence to
    // avoid the overhead of libusb_init/libusb_exit on every 500ms poll.
    LibusbContext pollCtx;

    // Phase 1: Wait for the device to disconnect from the bus (up to 3s).
    // For chips that don't re-enumerate (BCM2835), this times out
    // harmlessly and we find the device immediately in phase 2.
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
                // The ROM device has iSerialNumber == 0 (no serial string).
                // Once it re-enumerates as the second stage, iSerialNumber > 3.
                if (matchesPort(dev) &&
                    (dev.serialNumberIndex == 0 || dev.serialNumberIndex == 3)) {
                    stillPresent = true;
                    break;
                }
            }
            if (!stillPresent) {
                emit preparationStatusUpdate(tr("Device disconnected, waiting for reconnect..."));
                break;  // Device disconnected
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during disconnect wait:" << e.what();
            break;  // Can't scan — proceed to phase 2
        }
    }

    if (_cancelled.load()) return false;

    // Phase 2: Wait for a Broadcom boot device to appear on the same
    // physical port (up to 15s).
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
                // The ROM/first-stage device has iSerialNumber == 0 or 3.
                // The genuine second-stage (ready for file server) has iSerialNumber > 3.
                // This mirrors the upstream rpiboot dispatch: iSerialNumber 0/3 triggers
                // another bootcode upload, while > 3 triggers file_server().
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

    // Poll for a fastboot device on the same physical USB port.
    // Sets `found` to true and writes the bus:addr string to `fastbootId`.
    // Respects _cancelled for clean shutdown.
    LibusbContext pollCtx;

    constexpr int FB_POLLS = 120;  // 60s — device may take 20s+ to reboot into fastboot
    for (int attempt = 0; attempt < FB_POLLS; ++attempt) {
        if (_cancelled.load()) return false;

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

            if (_device.portPath.empty() && !devices.empty()) {
                const auto& dev = devices[0];
                fastbootId = QStringLiteral("%1:%2")
                    .arg(dev.busNumber)
                    .arg(dev.deviceAddress);
                qDebug() << "Fastboot device appeared (no port matching):" << fastbootId;
                found.store(true);
                return true;
            }
        } catch (const std::exception& e) {
            qWarning() << "rpiboot: USB scan failed during fastboot wait:" << e.what();
            continue;
        }
    }

    return false;
}

bool RpibootThread::waitForFastbootDevice()
{
    QString fastbootId;
    std::atomic<bool> found{false};
    if (pollForFastbootDevice(found, fastbootId)) {
        emit fastbootDeviceReady(fastbootId);
        return true;
    }
    if (!_cancelled.load())
        emit error(tr("Timed out waiting for fastboot device to appear."));
    return false;
}
