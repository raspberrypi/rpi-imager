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
    // Run the full rpiboot sequence on a single transport.
    //
    // WARNING: This does NOT handle USB re-enumeration.  It is only correct
    // when the transport stays valid across the bootcode→file-server
    // transition (mock transports in tests, or protocols like secure-boot
    // recovery where the device does not reset).  For Fastboot sideloading
    // on real hardware, use uploadBootcode() + serveFiles() separately
    // with a reconnect between them, as done in RpibootThread::run().
    bool execute(IUsbTransport& transport,
                 ChipGeneration gen,
                 SideloadMode mode,
                 const std::filesystem::path& firmwareDir,
                 ProgressCallback progress,
                 std::atomic<bool>& cancelled);

    // Upload the second-stage bootloader to the device.
    // After this returns, the device will reset its USB connection and
    // re-enumerate — the caller must close this transport and open a
    // new one before calling serveFiles().
    bool uploadBootcode(IUsbTransport& transport,
                        ChipGeneration gen,
                        const std::filesystem::path& firmwareDir,
                        ProgressCallback progress,
                        std::atomic<bool>& cancelled);

    // Run the file server phase on a (potentially new) transport.
    // The device must have already booted the second-stage bootloader
    // via uploadBootcode().
    bool serveFiles(IUsbTransport& transport,
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
