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
#include <istream>
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

    // Where the firmware comes from, as overridable accessors rather than
    // the constants directly.
    //
    // ensureAvailable() is the largest untested thing in this class, and the
    // only reason is that these are compile-time constants pointing at
    // github.com: nothing short of real network access could reach it.
    // Reading them through virtuals lets a test point the whole download and
    // cache path at a local server. Production behaviour is unchanged --
    // these return exactly the constants above.
protected:
    virtual std::string usbbootBase() const { return USBBOOT_RAW_BASE; }
    virtual std::string eepromBase() const { return EEPROM_RAW_BASE; }
    virtual std::string provisionerBase() const { return PROVISIONER_RAW_BASE; }


// Protected rather than private so a test can subclass and drive the cache
// logic directly.
//
// buildManifest(), findCachedVersion(), validateCacheForDevice() and
// clearCache()'s helpers are pure filesystem work -- they decide which
// firmware files are needed, whether what is already on disk can be trusted
// for a given board, and what to throw away. That is worth testing on its
// own, but the only public way in is ensureAvailable(), which downloads from
// the network first. Widening the access from private to protected changes
// nothing for existing callers and keeps the class's public surface exactly
// as it was.
protected:
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
    // Choose the version to use from the body of rpi-eeprom's versions.txt.
    // Split out from the fetch so it can be tested against hand-written
    // files: the choice matters because an archived ("old") row is not
    // guaranteed to exist under the latest/ channel we download from, so
    // selecting one yields a URL that 404s and the device gets no firmware.
    static std::optional<std::string> selectLatestVersion(std::istream& in,
                                                          const std::string& firmwareDir);

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
