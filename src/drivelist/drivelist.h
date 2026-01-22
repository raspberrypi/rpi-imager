/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Native drive enumeration for rpi-imager.
 * Platform-specific implementations enumerate storage devices and populate
 * DeviceDescriptor structs with device properties.
 */

#ifndef DRIVELIST_H
#define DRIVELIST_H

#include <cstdint>
#include <string>
#include <vector>

namespace Drivelist {

/**
 * @brief Describes a storage device discovered on the system
 *
 * This struct contains all information needed by rpi-imager to display
 * and filter storage devices for imaging operations.
 */
struct DeviceDescriptor {
    // Device identification
    std::string device;           ///< System device path (e.g., "/dev/sda", "\\\\.\\PhysicalDrive0")
    std::string raw;              ///< Raw device path for direct I/O (e.g., "/dev/rdisk1" on macOS)
    std::string description;      ///< Human-readable device description
    std::string error;            ///< Error message if enumeration failed for this device

    // Size and geometry
    uint64_t size = 0;            ///< Device size in bytes
    uint32_t blockSize = 512;     ///< Physical block/sector size
    uint32_t logicalBlockSize = 512; ///< Logical block size

    // Connection information
    std::string busType;          ///< Bus type: "USB", "SATA", "NVME", "SD", "MMC", "SCSI", etc.
    std::string busVersion;       ///< Bus version string (e.g., "3.0" for USB 3.0)
    std::string enumerator;       ///< Driver/enumerator name (Windows-specific)
    std::string devicePath;       ///< Full device path (Windows-specific)
    std::string parentDevice;     ///< Parent device path (for APFS volumes on macOS)

    // Mount information
    std::vector<std::string> mountpoints;      ///< Filesystem mount paths
    std::vector<std::string> mountpointLabels; ///< Volume labels for each mountpoint
    std::vector<std::string> childDevices;     ///< Child devices (e.g., APFS volumes)

    // Device classification flags
    bool isReadOnly = false;      ///< Device is read-only (hardware or software)
    bool isSystem = false;        ///< Device contains system files (should warn user)
    bool isVirtual = false;       ///< Device is virtual (loop device, VHD, etc.)
    bool isRemovable = false;     ///< Device can be removed while system is running
    bool isCard = false;          ///< Device is an SD/MMC card
    bool isSCSI = false;          ///< Connected via SCSI/SAS
    bool isUSB = false;           ///< Connected via USB
    bool isUAS = false;           ///< Connected via USB Attached SCSI

    // Null indicators (for optional fields)
    bool busVersionNull = true;
    bool devicePathNull = true;
    bool isUASNull = true;

    /**
     * @brief Check if this device should be shown in the UI
     *
     * Filters out devices that are:
     * - Zero-sized
     * - Read-only virtual devices
     * - System virtual devices
     * - Non-removable virtual devices
     *
     * @return true if device should be displayed to user
     */
    [[nodiscard]] bool isDisplayable() const noexcept {
        // Skip zero-sized devices
        if (size == 0) return false;

        // Allow read/write virtual devices (mounted disk images) but filter out:
        // - Read-only virtual devices
        // - System virtual devices
        // - Virtual devices that are not removable/ejectable
        if (isVirtual && (isReadOnly || isSystem || !isRemovable)) {
            return false;
        }

        return true;
    }

    /**
     * @brief Check if this device contains a system mount point
     * @return true if any mountpoint is a critical system path
     */
    [[nodiscard]] bool hasSystemMountpoint() const noexcept {
        for (const auto& mp : mountpoints) {
            if (mp == "/" || mp == "C:\\" || mp == "C://" ||
                mp == "/usr" || mp == "/var" || mp == "/home" || mp == "/boot") {
                return true;
            }
            // Linux snap mounts
            if (mp.rfind("/snap/", 0) == 0) return true;
        }
        return false;
    }

    /**
     * @brief Generate a unique key for this device (for UI tracking)
     * @return String combining device path and size
     */
    [[nodiscard]] std::string uniqueKey() const {
        std::string key = device + ":" + std::to_string(size);
        if (isReadOnly) key += "ro";
        return key;
    }
};

/**
 * @brief Enumerate all storage devices on the system
 *
 * This function is implemented per-platform:
 * - Linux: Uses lsblk with JSON output
 * - macOS: Uses DiskArbitration and IOKit frameworks
 * - Windows: Uses SetupDi and DeviceIoControl APIs
 *
 * The returned list includes all block devices; callers should use
 * DeviceDescriptor::isDisplayable() to filter for user-facing display.
 *
 * @return Vector of device descriptors, may be empty on error
 */
std::vector<DeviceDescriptor> ListStorageDevices();

// ============================================================================
// Test API - Platform-specific functions exposed for unit testing
// ============================================================================
// These are declared in the platform implementation files when
// DRIVELIST_ENABLE_TEST_API is defined. See:
// - drivelist_linux.cpp: Drivelist::testing::parseLinuxBlockDevices()
// - drivelist_windows.cpp: Drivelist::testing::windowsBusTypeToString()
// - drivelist_windows.cpp: Drivelist::testing::isWindowsSystemDevice()

} // namespace Drivelist

#endif // DRIVELIST_H
