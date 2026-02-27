/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Downloads and caches rpiboot firmware (fastboot gadget, bootcode
 * binaries, secure-boot-recovery, etc.) from the usbboot GitHub releases.
 *
 * On each call the manager follows the GitHub /releases/latest
 * redirect to discover the current version tag, then downloads
 * the tarball only when the cached version is stale.
 * If the version check fails, the newest cached version is used;
 * as a last resort a hardcoded fallback URL is tried.
 */

#ifndef RPIBOOT_FIRMWARE_MANAGER_H
#define RPIBOOT_FIRMWARE_MANAGER_H

#include "rpiboot_types.h"

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

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

    // Hardcoded fallback — used only when the manifest is unreachable
    // AND no cached version exists.
    static constexpr const char* FALLBACK_FIRMWARE_VERSION = "2025.02.20";
    static constexpr const char* FALLBACK_FIRMWARE_URL_TEMPLATE =
        "https://github.com/raspberrypi/usbboot/releases/download/%s/rpiboot-firmware-%s.tar.gz";

private:
    // Discover the latest firmware version tag via the GitHub
    // /releases/latest redirect.  Returns nullopt on any failure.
    std::optional<std::string> fetchLatestVersion(std::atomic<bool>& cancelled);

    // Check that a cached version directory contains the required files
    // for the given chip generation and sideload mode.
    bool validateCacheForDevice(const std::filesystem::path& versionDir,
                                 SideloadMode mode,
                                 ChipGeneration chip) const;

    // Scan the cache root for the newest version directory that
    // validates for the given mode + chip.
    std::optional<std::filesystem::path> findCachedVersion(SideloadMode mode,
                                                            ChipGeneration chip) const;

    // Remove all cached version directories except keepVersion.
    void cleanOldVersions(const std::filesystem::path& root,
                           const std::string& keepVersion);

    // Download a firmware tarball from url and extract into destDir.
    bool downloadAndExtract(const std::string& url,
                             const std::filesystem::path& destDir,
                             ProgressCallback progress,
                             std::atomic<bool>& cancelled);

    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_FIRMWARE_MANAGER_H
