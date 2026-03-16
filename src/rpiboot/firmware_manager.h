/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Downloads and caches rpiboot firmware (fastboot gadget, bootcode
 * binaries, secure-boot-recovery, etc.) from GitHub raw URLs.
 *
 * Files are fetched individually from the master/main branches of
 * the usbboot and rpi-sb-provisioner repositories and cached
 * locally under a single "master/" directory.
 */

#ifndef RPIBOOT_FIRMWARE_MANAGER_H
#define RPIBOOT_FIRMWARE_MANAGER_H

#include "rpiboot_types.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rpiboot {

class FirmwareManager {
public:
    FirmwareManager();

    // Ensure that firmware for the given mode and chip generation is
    // available in the local cache.  Downloads on first use; subsequent
    // calls return the cached path.
    // Returns the path to the firmware directory, or empty on failure.
    std::filesystem::path ensureAvailable(SideloadMode mode,
                                           ChipGeneration chip,
                                           ProgressCallback progress,
                                           std::atomic<bool>& cancelled);

    // Return the cache root (platform-appropriate app data directory)
    std::filesystem::path cacheRoot() const;

    // Clear all cached firmware
    void clearCache();

    const std::string& lastError() const { return _lastError; }

    static constexpr const char* USBBOOT_RAW_BASE =
        "https://github.com/raspberrypi/usbboot/raw/refs/heads/master/";
    static constexpr const char* PROVISIONER_RAW_BASE =
        "https://github.com/raspberrypi/rpi-sb-provisioner/raw/refs/heads/main/";

private:
    struct ManifestEntry {
        std::string url;
        std::string localPath;  // relative to the version dir
    };

    // Build the list of files to download for a given mode + chip
    std::vector<ManifestEntry> buildManifest(SideloadMode mode,
                                              ChipGeneration chip) const;

    // Download a single file via curl
    bool downloadFile(const std::string& url,
                       const std::filesystem::path& destPath,
                       ProgressCallback progress,
                       std::atomic<bool>& cancelled);

    // Check that a cached version directory contains the required files
    // for the given chip generation and sideload mode.
    bool validateCacheForDevice(const std::filesystem::path& versionDir,
                                 SideloadMode mode,
                                 ChipGeneration chip) const;

    // Check whether <cacheRoot>/master/ validates for the given mode + chip.
    std::optional<std::filesystem::path> findCachedVersion(SideloadMode mode,
                                                            ChipGeneration chip) const;

    // For BCM2712 Fastboot: extract bootcode5.bin from the bootfiles.bin TAR
    // (firmware/bootfiles.bin in usbboot, symlinked from
    // mass-storage-gadget64/bootfiles.bin).  recovery5/bootcode5.bin is the
    // EEPROM update binary and must NOT be used for mass-storage-gadget mode.
    bool extractBootcodeFromBootfiles(const std::filesystem::path& versionDir);

    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_FIRMWARE_MANAGER_H
