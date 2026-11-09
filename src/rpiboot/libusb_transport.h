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

#include <QString>

#include <cstdint>
#include <functional>
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
    uint8_t  serialNumberIndex = 0;      // bSerialNumber from USB descriptor (0 = ROM mode)
};

// The USB bus as its callers use it.
//
// LibusbContext below is the real one. The sideload sequence in
// RpibootThread is written entirely against this interface, so a test can
// supply a bus with whatever devices it wants on it and drive the sequence
// without hardware -- which is the only way that code is reachable at all.
class IUsbContext {
public:
    virtual ~IUsbContext();

    // Scan for Broadcom devices in USB boot mode
    virtual std::vector<UsbDeviceInfo> scanBootDevices() const = 0;

    // Scan for devices in fastboot mode (Google VID 0x18d1, PID 0x4e40)
    virtual std::vector<UsbDeviceInfo> scanFastbootDevices() const = 0;

    // Open a specific device for communication
    virtual std::unique_ptr<IUsbTransport> openDevice(const UsbDeviceInfo& info) const = 0;

    enum class OpenFailure { None, NotFound, AccessDenied, Other };
    // Why the last openDevice() returned nothing.
    virtual OpenFailure lastOpenFailure() const { return OpenFailure::None; }
};

// RAII wrapper around libusb_context
class LibusbContext : public IUsbContext {
public:
    // Whether a node this process may not open is tried through the
    // privileged helper. A background scan must never raise its prompt.
    enum class Elevation { Never, IfHelperRunning, Allowed };

    // Installed by the application where a helper exists (Linux). open()
    // returns a descriptor for bus/address, or -1.
    struct PrivilegedOpener {
        std::function<int(uint8_t bus, uint8_t address)> open;
        std::function<bool()> helperRunning;
    };
    static void setPrivilegedOpener(PrivilegedOpener opener);

    explicit LibusbContext(Elevation elevation = Elevation::Never);
    ~LibusbContext() override;

    LibusbContext(const LibusbContext&) = delete;
    LibusbContext& operator=(const LibusbContext&) = delete;

    std::vector<UsbDeviceInfo> scanBootDevices() const override;
    std::vector<UsbDeviceInfo> scanFastbootDevices() const override;
    std::unique_ptr<IUsbTransport> openDevice(const UsbDeviceInfo& info) const override;
    OpenFailure lastOpenFailure() const override { return _lastOpenFailure; }

    // Raw context for advanced usage (hotplug registration, etc.)
    libusb_context* raw() const { return _ctx; }

private:
    libusb_device_handle* openThroughHelper(const UsbDeviceInfo& info, int& fd) const;

    libusb_context* _ctx = nullptr;
    Elevation _elevation;
    mutable OpenFailure _lastOpenFailure = OpenFailure::None;
};

// Real IUsbTransport implementation backed by libusb
class LibusbTransport : public IUsbTransport {
public:
    // Takes ownership of the handle; claims interface 0. ownedFd is the node
    // a wrapped handle was made from, which libusb leaves open.
    explicit LibusbTransport(libusb_device_handle* handle, int ownedFd = -1);
    ~LibusbTransport() override;

    LibusbTransport(const LibusbTransport&) = delete;
    LibusbTransport& operator=(const LibusbTransport&) = delete;

    bool controlTransfer(uint8_t requestType, uint8_t request,
                         uint16_t wValue, uint16_t wIndex,
                         std::span<const uint8_t> data,
                         int timeoutMs) override;

    int controlTransferIn(uint8_t requestType, uint8_t request,
                          uint16_t wValue, uint16_t wIndex,
                          std::span<uint8_t> buffer,
                          int timeoutMs) override;

    int bulkWrite(uint8_t endpoint,
                  std::span<const uint8_t> data,
                  int timeoutMs) override;

    int bulkRead(uint8_t endpoint,
                 std::span<uint8_t> buffer,
                 int timeoutMs) override;

    bool isOpen() const override;

    uint8_t outEndpoint() const override { return _outEp; }
    uint8_t inEndpoint() const override { return _inEp; }

    // Diagnostic string built during construction (set_configuration result,
    // claim_interface result).  Included in performance-capture metadata.
    QString initDiagnostics() const override { return _initDiag; }

    // Read the USB interface string descriptor (iInterface) for the active
    // interface, decoded as ASCII.  Returns an empty string on failure or if
    // the interface advertises no string.  Used to positively identify the
    // RPi fastboot gadget, whose interface descriptor is "fastbootd-provisioner"
    // (see rpiboot::FASTBOOT_INTERFACE_DESCRIPTOR).
    std::string interfaceString() const override;

private:
    libusb_device_handle* _handle = nullptr;
    [[maybe_unused]] int _ownedFd = -1;  // closed on Linux only
    bool _interfaceClaimed = false;
    uint8_t _interface = 0;
    uint8_t _outEp = 0x01;
    uint8_t _inEp = 0x82;
    QString _initDiag;
};

} // namespace rpiboot

#endif // RPIBOOT_LIBUSB_TRANSPORT_H
