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

    // Vendor control transfer (bmRequestType, bRequest, wValue, wIndex, data, timeout)
    virtual bool controlTransfer(uint8_t requestType, uint8_t request,
                                 uint16_t wValue, uint16_t wIndex,
                                 std::span<const uint8_t> data,
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
};

} // namespace rpiboot

#endif // RPIBOOT_USB_TRANSPORT_H
