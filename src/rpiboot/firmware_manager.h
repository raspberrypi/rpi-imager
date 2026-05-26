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

    // Set a local fastboot gadget image (boot.img) to use instead of
    // downloading from GitHub.  When non-empty, ensureAvailable() copies
    // this file into the cache as fastboot/boot.img, bypassing the
    // remote download for that entry.
    void setCustomFastbootGadget(const std::string& path) { _customFastbootGadget = path; }
    const std::string& customFastbootGadget() const { return _customFastbootGadget; }

    // When non-empty, sign the fastboot gadget (boot.img) with this RSA-2048
    // private key (PEM) to produce fastboot/boot.sig.  Required when
    // re-provisioning a CM5 whose secure-boot OTP is fused — the bootloader
    // refuses to load an unsigned gadget.
    void setSignFastbootGadgetKey(const std::string& keyPath) { _signFastbootGadgetKey = keyPath; }
    const std::string& signFastbootGadgetKey() const { return _signFastbootGadgetKey; }

    // Clear all cached firmware
    void clearCache();

    const std::string& lastError() const { return _lastError; }

    // usbboot hosts the rpiboot USB-protocol scaffolding — gadget kernels
    // (fastboot-gadget.img, mass-storage-gadget64), the fastboot bootfiles
    // bundle (firmware/bootfiles.bin), the BCM2836/7 MSD-mode bootcode
    // (msd/bootcode.bin), and the secure-boot-recovery text configs
    // (boot.conf, config.txt).  Anything resembling actual EEPROM payload
    // or recovery firmware under usbboot is a git-symlink into rpi-eeprom
    // — and GitHub raw HTTP serves symlinks as ~30 bytes of target-path
    // text, not the binary.  Don't fetch those paths via this base; go to
    // EEPROM_RAW_BASE instead.
    static constexpr const char* USBBOOT_RAW_BASE =
        "https://github.com/raspberrypi/usbboot/raw/refs/heads/master/";
    // rpi-eeprom is the canonical source for the BCM2711/BCM2712 bootloader
    // firmware shipped to end users: pieeprom-DATE.bin (the signed EEPROM
    // image) and recovery.bin (the unsigned recovery binary that doubles as
    // the USB-mode bootcode we upload via rpiboot during SBR).  Both live
    // under firmware-271X/{channel}/; we pull from `latest`.  pieeprom
    // filenames are dated, so the latest version must be resolved at
    // runtime via firmware-271X/versions.txt — see
    // resolveLatestEepromVersion().  recovery.bin is a stable filename.
    static constexpr const char* EEPROM_RAW_BASE =
        "https://github.com/raspberrypi/rpi-eeprom/raw/refs/heads/master/";
    static constexpr const char* PROVISIONER_RAW_BASE =
        "https://github.com/raspberrypi/rpi-sb-provisioner/raw/refs/heads/main/";

private:
    struct ManifestEntry {
        std::string url;
        std::string localPath;  // relative to the version dir
    };

    // Build the list of files to download for a given mode + chip.
    // For SecureBootRecovery, eepromVersion identifies the dated pieeprom
    // bin in rpi-eeprom (e.g. "2026-05-22" → pieeprom-2026-05-22.bin).
    // When std::nullopt, the EEPROM/recovery URLs fall back to whatever's
    // in the local cache (offline mode); the cache check upstream will
    // refuse to proceed if those files aren't actually there.
    std::vector<ManifestEntry> buildManifest(SideloadMode mode,
                                              ChipGeneration chip,
                                              const std::optional<std::string>& eepromVersion = std::nullopt) const;

    // Fetch rpi-eeprom's firmware-271X/versions.txt and return the first
    // (newest) version it lists.  Returns std::nullopt on network failure;
    // caller falls back to the cached version sidecar.
    std::optional<std::string> resolveLatestEepromVersion(ChipGeneration chip,
                                                           std::atomic<bool>& cancelled);

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

    // Extract bootcode from the fastboot bootfiles.bin TAR archive.
    // BCM2711 → bootcode4.bin, BCM2712 → bootcode5.bin.
    bool extractBootcodeFromBootfiles(const std::filesystem::path& versionDir,
                                       ChipGeneration chip);

    // Make the SecureBootRecovery config.txt re-enumerate the device back
    // into rpiboot after writing the EEPROM, so we can scan for completion.
    // Strips any existing set_boot_order= / recovery_reboot= lines from the
    // upstream config and appends our required pair (set_boot_order=0x3
    // *before* recovery_reboot=1 — order is load-bearing per the bootloader's
    // section processing).  Idempotent; no-op if the file doesn't exist.
    bool ensureSbrReenumerates(const std::filesystem::path& versionDir,
                                ChipGeneration chip);

    std::string _lastError;
    std::string _customFastbootGadget;
    std::string _signFastbootGadgetKey;
};

} // namespace rpiboot

#endif // RPIBOOT_FIRMWARE_MANAGER_H
