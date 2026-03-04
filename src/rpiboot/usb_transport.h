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

namespace rpiboot {

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

    // Bulk OUT endpoint address (e.g. 0x01).  Determined from the device's
    // active configuration descriptor; falls back to EP 1 if unknown.
    virtual uint8_t outEndpoint() const { return 0x01; }

    // Bulk IN endpoint address (e.g. 0x82).  Determined from the device's
    // active configuration descriptor; falls back to EP 2 if unknown.
    virtual uint8_t inEndpoint() const { return 0x82; }
};

} // namespace rpiboot

#endif // RPIBOOT_USB_TRANSPORT_H
