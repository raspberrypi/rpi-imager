/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Top-level rpiboot protocol orchestrator.
 *
 * Ties together the bootcode loader, file server, and bootfiles
 * extraction into a single execute() call that drives a Broadcom SoC
 * from USB boot mode through to the target sideload mode (MSD, Fastboot,
 * or Secure Boot Recovery).
 */

#ifndef RPIBOOT_PROTOCOL_H
#define RPIBOOT_PROTOCOL_H

#include "rpiboot_types.h"
#include "usb_transport.h"
#include "bootcode_loader.h"
#include "file_server.h"
#include "bootfiles.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace rpiboot {

class RpibootProtocol {
public:
    // Execute the full rpiboot sideload sequence:
    //   1. Upload second-stage bootloader
    //   2. Serve firmware files until the device signals Done
    //
    // firmwareDir should contain the bootcode binary and the sideload
    // payload (e.g. fastboot/ contents or secure-boot-recovery/ contents).
    //
    // Returns true on success.
    bool execute(IUsbTransport& transport,
                 ChipGeneration gen,
                 SideloadMode mode,
                 const std::filesystem::path& firmwareDir,
                 ProgressCallback progress,
                 std::atomic<bool>& cancelled);

    // Device metadata collected during the protocol run
    const DeviceMetadata& metadata() const { return _fileServer.metadata(); }

    const std::string& lastError() const { return _lastError; }

private:
    // Select the subdirectory within firmwareDir that holds the
    // sideload payload for the given mode and chip generation.
    std::filesystem::path resolveSideloadDir(const std::filesystem::path& firmwareDir,
                                              ChipGeneration gen,
                                              SideloadMode mode) const;

    BootcodeLoader _bootcodeLoader;
    FileServer _fileServer;
    Bootfiles _bootfiles;
    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_PROTOCOL_H
