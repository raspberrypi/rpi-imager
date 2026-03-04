/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Stage-2 bootcode upload for the rpiboot protocol.
 *
 * Selects the correct bootcode binary by chip generation, sends the
 * BootMessage header and payload using the upstream ep_write protocol:
 * a zero-data vendor control transfer announcing the length, followed
 * by a bulk OUT transfer of the actual data.
 */

#ifndef RPIBOOT_BOOTCODE_LOADER_H
#define RPIBOOT_BOOTCODE_LOADER_H

#include "rpiboot_types.h"
#include "usb_transport.h"

#include <atomic>
#include <filesystem>
#include <span>
#include <string>

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
    // Write data using the upstream rpiboot ep_write protocol:
    // 1. Zero-data vendor control transfer with length in wValue/wIndex
    // 2. Bulk OUT transfer of the actual data in BULK_CHUNK_SIZE chunks
    bool epWrite(IUsbTransport& transport,
                 std::span<const uint8_t> data,
                 const std::atomic<bool>& cancelled);

    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_BOOTCODE_LOADER_H
