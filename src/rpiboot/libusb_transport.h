/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Real libusb-backed USB transport for the rpiboot protocol.
 * Wraps libusb_device_handle with RAII and provides device discovery.
 */

#ifndef RPIBOOT_LIBUSB_TRANSPORT_H
#define RPIBOOT_LIBUSB_TRANSPORT_H

#include "usb_transport.h"
#include "rpiboot_types.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace rpiboot {

// Describes a device found during USB scanning
struct UsbDeviceInfo {
    uint8_t  busNumber;
    uint8_t  deviceAddress;
    uint16_t vendorId;
    uint16_t productId;
    std::vector<uint8_t> portPath;       // USB port numbers for multi-device tracking
    ChipGeneration chipGeneration;
};

// RAII wrapper around libusb_context
class LibusbContext {
public:
    LibusbContext();
    ~LibusbContext();

    LibusbContext(const LibusbContext&) = delete;
    LibusbContext& operator=(const LibusbContext&) = delete;

    // Scan for Broadcom devices in USB boot mode
    std::vector<UsbDeviceInfo> scanBootDevices() const;

    // Scan for devices in fastboot mode (Google VID 0x18d1, PID 0x4ee0)
    std::vector<UsbDeviceInfo> scanFastbootDevices() const;

    // Open a specific device for communication
    std::unique_ptr<class LibusbTransport> openDevice(const UsbDeviceInfo& info) const;

    // Raw context for advanced usage (hotplug registration, etc.)
    libusb_context* raw() const { return _ctx; }

private:
    libusb_context* _ctx = nullptr;
};

// Real IUsbTransport implementation backed by libusb
class LibusbTransport : public IUsbTransport {
public:
    // Takes ownership of the handle; claims interface 0
    explicit LibusbTransport(libusb_device_handle* handle);
    ~LibusbTransport() override;

    LibusbTransport(const LibusbTransport&) = delete;
    LibusbTransport& operator=(const LibusbTransport&) = delete;

    bool controlTransfer(uint8_t requestType, uint8_t request,
                         uint16_t wValue, uint16_t wIndex,
                         std::span<const uint8_t> data,
                         int timeoutMs) override;

    int bulkWrite(uint8_t endpoint,
                  std::span<const uint8_t> data,
                  int timeoutMs) override;

    int bulkRead(uint8_t endpoint,
                 std::span<uint8_t> buffer,
                 int timeoutMs) override;

    bool isOpen() const override;

private:
    libusb_device_handle* _handle = nullptr;
    bool _interfaceClaimed = false;
};

} // namespace rpiboot

#endif // RPIBOOT_LIBUSB_TRANSPORT_H
