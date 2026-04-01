#include "drivelistmodelpollthread.h"
#include <QElapsedTimer>
#include <QDebug>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "rpiboot/rpiboot_scanner.h"
#include "rpiboot/libusb_transport.h"
#include "fastboot/fastboot_protocol.h"
#include <set>
#include <sstream>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

DriveListModelPollThread::DriveListModelPollThread(QObject *parent)
    : QThread(parent), _terminate(false), _scanMode(ScanMode::Normal)
{
    qRegisterMetaType< std::vector<Drivelist::DeviceDescriptor> >( "std::vector<Drivelist::DeviceDescriptor>" );
}

DriveListModelPollThread::~DriveListModelPollThread()
{
    _terminate = true;
    _modeChanged.wakeAll();  // Wake thread if it's waiting
    if (!wait(2000)) {
        // Thread is stuck (e.g. blocked in libusb_init due to a libusb macOS
        // deadlock).  Force-kill it and wait for it to die — QThread::~QThread()
        // calls qFatal() if the thread is still running when it runs, so we
        // must not return until the OS has confirmed the thread is gone.
        terminate();
        wait();
    }
}

void DriveListModelPollThread::stop()
{
    _terminate = true;
    _modeChanged.wakeAll();  // Wake thread to check terminate flag
}

void DriveListModelPollThread::start()
{
    _terminate = false;
    QThread::start();
}

void DriveListModelPollThread::setScanMode(ScanMode mode)
{
    QMutexLocker lock(&_mutex);
    if (_scanMode != mode) {
        ScanMode oldMode = _scanMode;
        _scanMode = mode;
        
        const char* modeStr = (mode == ScanMode::Normal) ? "Normal" :
                              (mode == ScanMode::Slow) ? "Slow" : "Paused";
        qDebug() << "Drive scan mode changed to:" << modeStr;
        
        // Wake thread if transitioning from paused to active
        if (oldMode == ScanMode::Paused && mode != ScanMode::Paused) {
            _modeChanged.wakeAll();
        }
        
        emit scanModeChanged(mode);
    }
}

DriveListModelPollThread::ScanMode DriveListModelPollThread::scanMode() const
{
    QMutexLocker lock(&_mutex);
    return _scanMode;
}

void DriveListModelPollThread::pause()
{
    setScanMode(ScanMode::Paused);
}

void DriveListModelPollThread::resume()
{
    setScanMode(ScanMode::Normal);
}

void DriveListModelPollThread::setRpibootEnabled(bool enabled)
{
    _rpibootEnabled.store(enabled, std::memory_order_relaxed);
    qDebug() << "Rpiboot scanning" << (enabled ? "enabled" : "disabled");
}

void DriveListModelPollThread::setFastbootScanEnabled(bool enabled)
{
    _fastbootScanEnabled.store(enabled, std::memory_order_relaxed);
    qDebug() << "Fastboot scanning" << (enabled ? "enabled" : "disabled");
}

void DriveListModelPollThread::run()
{
#ifdef Q_OS_WIN
    // Suppress Windows "Insert a disk" / "not accessible" system error dialogs
    // for this thread. Error mode is per-thread, so we set it once at thread start.
    DWORD oldMode;
    if (!SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode)) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    }
#endif

    QElapsedTimer t1;

    while (!_terminate)
    {
        // Check current scan mode
        ScanMode currentMode;
        {
            QMutexLocker lock(&_mutex);
            currentMode = _scanMode;
        }
        
        if (currentMode == ScanMode::Paused) {
            // Wait until mode changes or we're told to terminate
            QMutexLocker lock(&_mutex);
            while (_scanMode == ScanMode::Paused && !_terminate) {
                _modeChanged.wait(&_mutex, 500);  // Check every 500ms for terminate
            }
            continue;  // Re-check mode after waking
        }
        
        // Perform the scan
        t1.start();
        auto driveList = Drivelist::ListStorageDevices();
        if (_rpibootEnabled.load(std::memory_order_relaxed)) {
            auto rpibootDevices = rpiboot::scanRpibootDevices();
            driveList.insert(driveList.end(), rpibootDevices.begin(), rpibootDevices.end());
        }

        // Fastboot device scanning — enumerate storage on fastboot-mode Compute Modules
        if (_fastbootScanEnabled.load(std::memory_order_relaxed)) {
            try {
                rpiboot::LibusbContext ctx;
                auto fbDevices = ctx.scanFastbootDevices();

                // Build set of currently-present port path keys
                std::set<std::string> presentKeys;
                for (const auto& dev : fbDevices) {
                    std::string ppKey = rpiboot::portPathToString(dev.portPath);
                    presentKeys.insert(ppKey);

                    // Query new devices not yet in cache
                    if (_fastbootCache.find(ppKey) == _fastbootCache.end()) {
                        auto transport = ctx.openDevice(dev);
                        if (!transport || !transport->isOpen())
                            continue;

                        fastboot::FastbootProtocol fb;
                        FastbootDeviceCache cache;
                        cache.fastbootId = std::to_string(dev.busNumber) + ":" + std::to_string(dev.deviceAddress);
                        cache.portPath = dev.portPath;

                        // Query product name
                        auto product = fb.getVar(*transport, "product");
                        cache.productName = product.value_or("Compute Module");

                        // Query block devices (comma-separated list)
                        auto blockDevStr = fb.getVar(*transport, "block-devices");
                        if (blockDevStr) {
                            // Parse comma-separated list: "mmcblk0,nvme0n1"
                            std::string devList = *blockDevStr;
                            std::istringstream iss(devList);
                            std::string blockDev;
                            while (std::getline(iss, blockDev, ',')) {
                                if (blockDev.empty()) continue;

                                FastbootStorageInfo storInfo;
                                storInfo.blockDevice = blockDev;
                                storInfo.sizeBytes = 0;
                                storInfo.storageType = "unknown";

                                // Query size
                                auto sizeStr = fb.getVar(*transport, "block-device-size:" + blockDev);
                                if (sizeStr) {
                                    try {
                                        std::string val = *sizeStr;
                                        if (val.starts_with("0x") || val.starts_with("0X"))
                                            storInfo.sizeBytes = std::stoull(val, nullptr, 16);
                                        else
                                            storInfo.sizeBytes = std::stoull(val);
                                    } catch (...) {}
                                }

                                // Query type
                                auto typeStr = fb.getVar(*transport, "block-device-type:" + blockDev);
                                if (typeStr)
                                    storInfo.storageType = *typeStr;

                                cache.storage.push_back(std::move(storInfo));
                            }
                        }

                        qDebug() << "Fastboot cache: new device" << QString::fromStdString(ppKey)
                                 << "product=" << QString::fromStdString(cache.productName)
                                 << "storage count=" << cache.storage.size();
                        _fastbootCache[ppKey] = std::move(cache);
                    }
                }

                // Remove stale cache entries
                for (auto it = _fastbootCache.begin(); it != _fastbootCache.end(); ) {
                    if (presentKeys.find(it->first) == presentKeys.end()) {
                        qDebug() << "Fastboot cache: removing stale" << QString::fromStdString(it->first);
                        it = _fastbootCache.erase(it);
                    } else {
                        ++it;
                    }
                }

                // Create DeviceDescriptor entries from cached fastboot devices
                for (const auto& [ppKey, cache] : _fastbootCache) {
                    for (const auto& stor : cache.storage) {
                        Drivelist::DeviceDescriptor dd;
                        dd.device = "fastboot://" + cache.fastbootId + "/" + stor.blockDevice;
                        dd.size = stor.sizeBytes;
                        dd.isRemovable = true;
                        dd.isUSB = true;
                        dd.isReadOnly = false;
                        dd.isSystem = false;
                        dd.isVirtual = false;

                        // Build description: "CM5 eMMC" / "CM5 NVMe"
                        std::string typeLabel = stor.storageType;
                        if (typeLabel == "emmc") typeLabel = "eMMC";
                        else if (typeLabel == "sd") typeLabel = "SD";
                        else if (typeLabel == "nvme") typeLabel = "NVMe";
                        else if (typeLabel == "usb") typeLabel = "USB";
                        else if (typeLabel == "scsi") typeLabel = "SCSI";
                        dd.description = cache.productName + " " + typeLabel;

                        dd.isFastbootStorage = true;
                        dd.fastbootId = cache.fastbootId;
                        dd.fastbootBlockDevice = stor.blockDevice;
                        dd.fastbootStorageType = stor.storageType;
                        dd.fastbootPortPath = cache.portPath;

                        // Set busType for icon selection
                        if (stor.storageType == "nvme")
                            dd.busType = "NVME";

                        driveList.push_back(std::move(dd));
                    }
                }
            } catch (const std::exception& e) {
                qDebug() << "Fastboot scan error:" << e.what();
            } catch (...) {
                // Ignore errors during fastboot scan
            }
        }

        emit newDriveList(driveList);
        quint32 elapsed = static_cast<quint32>(t1.elapsed());
        
        // Emit timing event for performance tracking (always, but listeners can filter)
        emit eventDriveListPoll(elapsed);
        
        if (elapsed > 1000)
            qDebug() << "Enumerating drives took a long time:" << elapsed/1000.0 << "seconds";
        
        // Sleep based on current mode
        int sleepSeconds = (currentMode == ScanMode::Slow) ? 5 : 1;
        
        // Use interruptible sleep - check mode periodically
        for (int i = 0; i < sleepSeconds && !_terminate; ++i) {
            QMutexLocker lock(&_mutex);
            if (_scanMode == ScanMode::Paused) {
                break;  // Mode changed to paused, exit sleep early
            }
            lock.unlock();
            QThread::sleep(1);
        }
    }
}
