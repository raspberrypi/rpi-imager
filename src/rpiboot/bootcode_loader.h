/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Stage-2 bootcode upload for the rpiboot protocol.
 *
 * Selects the correct bootcode binary by chip generation, sends the
 * BootMessage header via a vendor control transfer, and streams the
 * payload via bulk OUT in 16 KB chunks.
 */

#ifndef RPIBOOT_BOOTCODE_LOADER_H
#define RPIBOOT_BOOTCODE_LOADER_H

#include "rpiboot_types.h"
#include "usb_transport.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace rpiboot {

class BootcodeLoader {
public:
    // Return the bootcode filename appropriate for the given chip
    static std::string bootcodeFilename(ChipGeneration gen);

    // Send the second-stage bootloader to the device.
    // firmwareDir must contain the appropriate bootcode*.bin file.
    // Returns false on failure (transport error or file not found).
    bool send(IUsbTransport& transport,
              ChipGeneration gen,
              const std::filesystem::path& firmwareDir,
              const std::atomic<bool>& cancelled);

    // Last error message (empty on success)
    const std::string& lastError() const { return _lastError; }

private:
    bool sendBootMessage(IUsbTransport& transport,
                         int32_t payloadLength,
                         const std::atomic<bool>& cancelled);

    bool sendPayload(IUsbTransport& transport,
                     const std::vector<uint8_t>& data,
                     ChipGeneration gen,
                     const std::atomic<bool>& cancelled);

    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_BOOTCODE_LOADER_H
