/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Abstract USB transport interface for the rpiboot protocol.
 * The concrete implementation wraps libusb; tests inject a mock.
 */

#ifndef RPIBOOT_USB_TRANSPORT_H
#define RPIBOOT_USB_TRANSPORT_H

#include <cstdint>
#include <span>

#include <QString>
#include <string>

namespace rpiboot {

// Abstract USB transport: the operations rpiboot and fastboot need from a
// device, with no libusb in the signature so both can be driven by a test.
class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;

    // Vendor control transfer OUT (host-to-device)
    // bmRequestType, bRequest, wValue, wIndex, data, timeout
    virtual bool controlTransfer(uint8_t requestType, uint8_t request,
                                 uint16_t wValue, uint16_t wIndex,
                                 std::span<const uint8_t> data,
                                 int timeoutMs) = 0;

    // Vendor control transfer IN (device-to-host)
    // Returns number of bytes read into buffer, or -1 on error
    virtual int controlTransferIn(uint8_t requestType, uint8_t request,
                                  uint16_t wValue, uint16_t wIndex,
                                  std::span<uint8_t> buffer,
                                  int timeoutMs) = 0;

    // Bulk OUT -- returns number of bytes actually transferred, or -1 on error
    virtual int bulkWrite(uint8_t endpoint,
                          std::span<const uint8_t> data,
                          int timeoutMs) = 0;

    // Bulk IN -- returns number of bytes actually read, or -1 on error
    virtual int bulkRead(uint8_t endpoint,
                         std::span<uint8_t> buffer,
                         int timeoutMs) = 0;

    // True if the underlying device handle is still valid
    virtual bool isOpen() const = 0;

    // What happened while the transport was being brought up: which
    // configuration and interface were claimed, and anything that had to be
    // retried.  Carried into the telemetry and the error message when a
    // sideload fails, since by then the device is usually gone from the bus
    // and this is the only account of it.  Empty by default, for transports
    // with nothing to say.
    virtual QString initDiagnostics() const { return {}; }

    // Bulk OUT endpoint address (e.g. 0x01).  Determined from the device's
    // active configuration descriptor; falls back to EP 1 if unknown.
    virtual uint8_t outEndpoint() const { return 0x01; }

    // Bulk IN endpoint address (e.g. 0x82).  Determined from the device's
    // active configuration descriptor; falls back to EP 2 if unknown.
    virtual uint8_t inEndpoint() const { return 0x82; }

    // USB interface string descriptor (iInterface) for the active interface,
    // decoded as ASCII, or an empty string if unavailable.  Used to
    // positively identify the RPi fastboot gadget (whose descriptor is
    // "fastbootd-provisioner").  The default returns empty, so transports
    // that cannot supply it (e.g. non-USB) fall back to protocol-level
    // identification.
    virtual std::string interfaceString() const { return {}; }
};

} // namespace rpiboot

#endif // RPIBOOT_USB_TRANSPORT_H
