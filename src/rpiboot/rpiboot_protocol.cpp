/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "rpiboot_protocol.h"

#include <thread>

namespace rpiboot {

bool RpibootProtocol::execute(IUsbTransport& transport,
                               ChipGeneration gen,
                               SideloadMode mode,
                               const std::filesystem::path& firmwareDir,
                               ProgressCallback progress,
                               std::atomic<bool>& cancelled)
{
    // ── Step 1: Upload second-stage bootloader ─────────────────────────
    if (progress)
        progress(0, 3, "Uploading bootloader...");

    if (!_bootcodeLoader.send(transport, gen, firmwareDir, cancelled)) {
        _lastError = "Bootcode upload failed: " + _bootcodeLoader.lastError();
        return false;
    }

    if (cancelled.load())
        return false;

    // ── Step 2: Wait for device to re-enumerate ────────────────────────
    // After receiving the bootcode, the device resets its USB connection
    // and re-appears with a new device address.  We give it a moment.
    if (progress)
        progress(1, 3, "Waiting for device to restart...");

    // Short delay for the device to re-enumerate on the USB bus.
    // The caller (RpibootThread) is responsible for re-opening the
    // transport if the device address changed.  For the simple
    // single-transport case the device stays on the same handle.
    for (int i = 0; i < 10 && !cancelled.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (transport.isOpen())
            break;
    }

    if (cancelled.load())
        return false;

    if (!transport.isOpen()) {
        _lastError = "Device did not re-enumerate after bootcode upload";
        return false;
    }

    // ── Step 3: File server ────────────────────────────────────────────
    if (progress)
        progress(2, 3, "Loading firmware...");

    auto sideloadDir = resolveSideloadDir(firmwareDir, gen, mode);

    // If the sideload directory contains a bootfiles.bin tar archive,
    // extract it and provide files from memory.
    auto bootfilesTar = sideloadDir / "bootfiles.bin";
    bool haveBootfiles = false;

    if (std::filesystem::exists(bootfilesTar)) {
        if (!_bootfiles.extractFromFile(bootfilesTar.string())) {
            _lastError = "Failed to extract bootfiles.bin: " + _bootfiles.lastError();
            return false;
        }
        haveBootfiles = true;
    }

    // Custom file resolver that first checks the extracted tar, then disk
    FileResolver resolver;
    if (haveBootfiles) {
        resolver = [this, &sideloadDir](const std::string& filename) -> std::vector<uint8_t> {
            // Try the tar archive first
            const auto* data = _bootfiles.find(filename);
            if (data)
                return *data;

            // Fall back to on-disk files
            return FileServer::readFileFromDisk(sideloadDir, filename);
        };
    }

    if (!_fileServer.run(transport, sideloadDir, progress, cancelled, resolver)) {
        _lastError = "File server failed: " + _fileServer.lastError();
        return false;
    }

    if (progress)
        progress(3, 3, "Device boot complete");

    return true;
}

std::filesystem::path RpibootProtocol::resolveSideloadDir(
    const std::filesystem::path& firmwareDir,
    ChipGeneration gen,
    SideloadMode mode) const
{
    switch (mode) {
    case SideloadMode::Fastboot:
        {
            auto dir = firmwareDir / "fastboot";
            if (std::filesystem::exists(dir))
                return dir;
        }
        break;

    case SideloadMode::SecureBootRecovery:
        if (gen == ChipGeneration::BCM2712) {
            auto dir = firmwareDir / "secure-boot-recovery5";
            if (std::filesystem::exists(dir))
                return dir;
        }
        {
            auto dir = firmwareDir / "secure-boot-recovery";
            if (std::filesystem::exists(dir))
                return dir;
        }
        break;
    }

    // Fall back to the base firmware directory
    return firmwareDir;
}

} // namespace rpiboot
