/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Native implementation of the Android Fastboot USB protocol.
 *
 * Operates directly over IUsbTransport, sending ASCII commands via
 * bulk OUT and reading responses via bulk IN.
 */

#ifndef FASTBOOT_PROTOCOL_H
#define FASTBOOT_PROTOCOL_H

#include "../rpiboot/usb_transport.h"
#include "../rpiboot/rpiboot_types.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fastboot {

struct Response {
    enum Type { Okay, Fail, Data, Info, Text };
    Type type = Fail;
    std::string message;
    uint32_t dataSize = 0;  // Only valid for Data type
};

class FastbootProtocol {
public:
    // Send an ASCII command and read the response.
    // For multi-response commands (INFO prefix), this reads until a
    // terminal response (OKAY or FAIL) is received.
    Response sendCommand(rpiboot::IUsbTransport& transport,
                         std::string_view command,
                         int timeoutMs);

    // Download data to the device.
    // Sends "download:<hex-size>", waits for DATA response, then
    // streams the data via bulk OUT.
    bool download(rpiboot::IUsbTransport& transport,
                  std::span<const uint8_t> data,
                  rpiboot::ProgressCallback progress,
                  std::atomic<bool>& cancelled);

    // Flash the previously downloaded data to a partition.
    // Sends "flash:<partition>" and waits for OKAY/FAIL.
    bool flash(rpiboot::IUsbTransport& transport,
               std::string_view partition,
               int timeoutMs);

    // Query a device variable.
    // Sends "getvar:<name>" and returns the value, or nullopt on failure.
    std::optional<std::string> getVar(rpiboot::IUsbTransport& transport,
                                       std::string_view name);

    // Combined download + flash in one call.
    bool flashImage(rpiboot::IUsbTransport& transport,
                    std::string_view partition,
                    std::span<const uint8_t> data,
                    rpiboot::ProgressCallback progress,
                    std::atomic<bool>& cancelled);

    const std::string& lastError() const { return _lastError; }

private:
    // Read a single response packet from the device
    Response readResponse(rpiboot::IUsbTransport& transport, int timeoutMs);

    std::string _lastError;

    // Fastboot uses these endpoint addresses
    static constexpr uint8_t EP_OUT = 0x01;
    static constexpr uint8_t EP_IN  = 0x81;
};

} // namespace fastboot

#endif // FASTBOOT_PROTOCOL_H
