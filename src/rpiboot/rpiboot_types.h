/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Core types for the rpiboot protocol -- used by the bootcode loader,
 * file server, and protocol orchestrator to communicate with Broadcom
 * SoCs in USB boot mode.
 */

#ifndef RPIBOOT_TYPES_H
#define RPIBOOT_TYPES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace rpiboot {

// Broadcom USB Vendor ID shared by all Pi silicon
constexpr uint16_t BROADCOM_VID = 0x0a5c;

// USB Product IDs that identify each chip generation in boot mode
enum class ChipGeneration : uint16_t {
    BCM2835   = 0x2763,   // CM1, Pi Zero, Pi 1A+
    BCM2836_7 = 0x2764,   // CM3, Pi 2/3
    BCM2711   = 0x2711,   // CM4, Pi 4
    BCM2712   = 0x2712,   // CM5, Pi 5
};

// Convert a USB PID to the matching ChipGeneration, if known
inline std::optional<ChipGeneration> chipGenerationFromPid(uint16_t pid)
{
    switch (pid) {
    case static_cast<uint16_t>(ChipGeneration::BCM2835):   return ChipGeneration::BCM2835;
    case static_cast<uint16_t>(ChipGeneration::BCM2836_7): return ChipGeneration::BCM2836_7;
    case static_cast<uint16_t>(ChipGeneration::BCM2711):   return ChipGeneration::BCM2711;
    case static_cast<uint16_t>(ChipGeneration::BCM2712):   return ChipGeneration::BCM2712;
    default: return std::nullopt;
    }
}

// Human-readable name for each chip generation
inline std::string_view chipGenerationName(ChipGeneration gen)
{
    switch (gen) {
    case ChipGeneration::BCM2835:   return "BCM2835";
    case ChipGeneration::BCM2836_7: return "BCM2836/7";
    case ChipGeneration::BCM2711:   return "BCM2711";
    case ChipGeneration::BCM2712:   return "BCM2712";
    }
    return "Unknown";
}

// Directory prefix used inside bootfiles.bin TAR archives.
// Files are stored as e.g. "2712/mcb.bin" — this returns the numeric
// chip model prefix ("2712") for a given generation.
inline std::string_view chipDirectoryPrefix(ChipGeneration gen)
{
    switch (gen) {
    case ChipGeneration::BCM2835:   return "2835";
    case ChipGeneration::BCM2836_7: return "2836";
    case ChipGeneration::BCM2711:   return "2711";
    case ChipGeneration::BCM2712:   return "2712";
    }
    return "";
}

// Friendly device description for the UI (e.g. drive-list delegate)
inline std::string deviceDescription(ChipGeneration gen)
{
    switch (gen) {
    case ChipGeneration::BCM2835:   return "Compute Module 1 (USB Boot)";
    case ChipGeneration::BCM2836_7: return "Compute Module 3 (USB Boot)";
    case ChipGeneration::BCM2711:   return "Compute Module 4 (USB Boot)";
    case ChipGeneration::BCM2712:   return "Compute Module 5 (USB Boot)";
    }
    return "Raspberry Pi (USB Boot)";
}

// ── Boot message header ────────────────────────────────────────────────
// Sent via a vendor control transfer to initiate the second-stage boot.
struct BootMessage {
    int32_t length;                     // Payload length
    std::array<uint8_t, 20> signature;  // Optional SHA-1 HMAC (zeroed if unsigned)
};
static_assert(sizeof(BootMessage) == 24, "BootMessage must be 24 bytes");

// ── File server commands ───────────────────────────────────────────────
// The device's firmware drives the file-server conversation by sending
// FileMessage structs via vendor control transfers (ep_read protocol).
enum class FileCommand : int32_t {
    GetFileSize = 0,
    ReadFile    = 1,
    Done        = 2,
};

struct FileMessage {
    FileCommand command;
    std::array<char, 256> filename;

    [[nodiscard]] std::string_view name() const {
        // Find the first NUL and return the view up to it
        auto it = std::find(filename.begin(), filename.end(), '\0');
        return {filename.data(), static_cast<size_t>(it - filename.begin())};
    }
};
static_assert(sizeof(FileMessage) == 260, "FileMessage must be 260 bytes");

// ── Sideload mode ──────────────────────────────────────────────────────
enum class SideloadMode {
    Fastboot,             // Fastboot USB gadget
    SecureBootRecovery,   // OTP key programming + EEPROM signing
};

// ── Device metadata ────────────────────────────────────────────────────
// Collected from star-prefixed file requests during the file-server phase.
struct DeviceMetadata {
    std::optional<std::string> serialNumber;
    std::optional<std::string> macAddress;
    std::optional<std::string> otpState;
    std::optional<uint32_t>    boardRevision;
};

// ── Protocol constants ─────────────────────────────────────────────────
constexpr size_t  BULK_CHUNK_SIZE       = 16 * 1024;   // Max libusb bulk transfer
constexpr uint8_t VENDOR_REQUEST_TYPE   = 0x40;         // bmRequestType for vendor OUT
constexpr uint8_t VENDOR_REQUEST        = 0;             // bRequest
constexpr int     DEFAULT_TIMEOUT_MS    = 3000;

// Fastboot USB VID/PID used by the RPi fastboot gadget after rpiboot
// sideloads it.  Note: the standard Android fastboot PID is 0x4ee0;
// the RPi gadget uses 0x4e40.
constexpr uint16_t FASTBOOT_VID         = 0x18d1;       // Google
constexpr uint16_t FASTBOOT_PID         = 0x4e40;       // RPi Fastboot gadget

// ── Progress callback ──────────────────────────────────────────────────
// (current, total, status):
//   - Percentage mode: current/total in [0,100] range (e.g. download progress)
//   - Discrete steps:  current/total as step indices (e.g. 2/3)
//   - Indeterminate:   total == 0 means "unknown total" (e.g. file server)
using ProgressCallback = std::function<void(uint64_t current, uint64_t total, const std::string& status)>;

} // namespace rpiboot

#endif // RPIBOOT_TYPES_H
