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
 * @brief Sanitize a string for safe display to remove Unicode control characters
 *
 * Removes or replaces dangerous Unicode characters that could be used to
 * deceive users about device names or paths:
 * - Bidirectional override characters (U+202A-U+202E, U+2066-U+2069)
 * - Zero-width characters (U+200B-U+200F, U+FEFF)
 * - Other format characters that don't render visibly
 *
 * This prevents attacks where a malicious USB device could have a name like
 * "Safe\u202Edirevod\u202C Storage" which displays as "Safe Override Storage"
 * but the text direction is actually reversed in parts.
 *
 * @param input The string to sanitize (UTF-8 encoded)
 * @return Sanitized string safe for display
 */
inline std::string sanitizeForDisplay(const std::string& input)
{
    std::string result;
    result.reserve(input.size());
    
    size_t i = 0;
    while (i < input.size()) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        
        // ASCII printable characters (0x20-0x7E) - always safe
        if (c >= 0x20 && c <= 0x7E) {
            result += static_cast<char>(c);
            ++i;
            continue;
        }
        
        // ASCII control characters (0x00-0x1F, 0x7F) - replace with space
        if (c < 0x20 || c == 0x7F) {
            result += ' ';
            ++i;
            continue;
        }
        
        // Multi-byte UTF-8 sequences
        size_t seqLen = 1;
        uint32_t codepoint = 0;
        
        if ((c & 0xE0) == 0xC0 && i + 1 < input.size()) {
            // 2-byte sequence (U+0080 to U+07FF)
            seqLen = 2;
            codepoint = (c & 0x1F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F);
        } else if ((c & 0xF0) == 0xE0 && i + 2 < input.size()) {
            // 3-byte sequence (U+0800 to U+FFFF)
            seqLen = 3;
            codepoint = (c & 0x0F) << 12;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 2]) & 0x3F);
        } else if ((c & 0xF8) == 0xF0 && i + 3 < input.size()) {
            // 4-byte sequence (U+10000 to U+10FFFF)
            seqLen = 4;
            codepoint = (c & 0x07) << 18;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3F) << 12;
            codepoint |= (static_cast<unsigned char>(input[i + 2]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(input[i + 3]) & 0x3F);
        } else {
            // Invalid UTF-8 or incomplete sequence - replace with replacement char
            result += '\xEF';  // U+FFFD replacement character in UTF-8
            result += '\xBF';
            result += '\xBD';
            ++i;
            continue;
        }
        
        // Check for dangerous Unicode code points
        bool isDangerous = false;
        
        // Bidirectional text control (RTL override attacks)
        // U+202A-U+202E: LRE, RLE, PDF, LRO, RLO
        // U+2066-U+2069: LRI, RLI, FSI, PDI
        if ((codepoint >= 0x202A && codepoint <= 0x202E) ||
            (codepoint >= 0x2066 && codepoint <= 0x2069)) {
            isDangerous = true;
        }
        
        // Zero-width and invisible formatting characters
        // U+200B: Zero Width Space
        // U+200C: Zero Width Non-Joiner
        // U+200D: Zero Width Joiner
        // U+200E: Left-to-Right Mark
        // U+200F: Right-to-Left Mark
        // U+FEFF: Zero Width No-Break Space (BOM)
        if ((codepoint >= 0x200B && codepoint <= 0x200F) ||
            codepoint == 0xFEFF) {
            isDangerous = true;
        }
        
        // Other format characters (Cf category) that could hide content
        // U+00AD: Soft Hyphen
        // U+034F: Combining Grapheme Joiner
        // U+061C: Arabic Letter Mark
        // U+115F-U+1160: Hangul fillers
        // U+17B4-U+17B5: Khmer vowel inherent
        // U+180B-U+180E: Mongolian free variation selectors
        // U+2060-U+2064: Word joiner, invisible operators
        if (codepoint == 0x00AD || codepoint == 0x034F || codepoint == 0x061C ||
            (codepoint >= 0x115F && codepoint <= 0x1160) ||
            (codepoint >= 0x17B4 && codepoint <= 0x17B5) ||
            (codepoint >= 0x180B && codepoint <= 0x180E) ||
            (codepoint >= 0x2060 && codepoint <= 0x2064)) {
            isDangerous = true;
        }
        
        // Tag characters (U+E0000-U+E007F) - can embed invisible metadata
        if (codepoint >= 0xE0000 && codepoint <= 0xE007F) {
            isDangerous = true;
        }
        
        // Variation selectors (U+FE00-U+FE0F, U+E0100-U+E01EF)
        if ((codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||
            (codepoint >= 0xE0100 && codepoint <= 0xE01EF)) {
            isDangerous = true;
        }
        
        if (isDangerous) {
            // Skip dangerous characters entirely (don't even add a space)
            i += seqLen;
            continue;
        }
        
        // Safe multi-byte character - copy as-is
        for (size_t j = 0; j < seqLen; ++j) {
            result += input[i + j];
        }
        i += seqLen;
    }
    
    return result;
}

/**
 * @brief Check if a hostname/domain contains non-ASCII characters (potential IDN homograph)
 *
 * Returns true if the domain contains characters outside basic ASCII that could
 * be used in IDN homograph attacks (e.g., Cyrillic 'а' looks like Latin 'a').
 *
 * @param host The hostname to check
 * @return true if the host contains non-ASCII characters
 */
inline bool hasNonAsciiChars(const std::string& host)
{
    for (unsigned char c : host) {
        if (c > 0x7F) {
            return true;
        }
    }
    return false;
}

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
