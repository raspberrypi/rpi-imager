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
#include <utility>
#include <vector>

namespace rpiboot {

// Broadcom USB Vendor ID shared by all Pi silicon
constexpr uint16_t BROADCOM_VID = 0x0a5c;

// USB Product IDs that identify each chip generation in boot mode.
// BCM2835 (PID 0x2763, CM1/Pi Zero/Pi 1A+) is intentionally not supported.
enum class ChipGeneration : uint16_t {
    BCM2836_7 = 0x2764,   // CM3/CM3+, Pi 2/3 (BCM2836 and BCM2837 share this PID)
    BCM2711   = 0x2711,   // CM4, Pi 4
    BCM2712   = 0x2712,   // CM5, Pi 5
};

// Convert a USB PID to the matching ChipGeneration, if known
inline std::optional<ChipGeneration> chipGenerationFromPid(uint16_t pid)
{
    switch (pid) {
    case static_cast<uint16_t>(ChipGeneration::BCM2836_7): return ChipGeneration::BCM2836_7;
    case static_cast<uint16_t>(ChipGeneration::BCM2711):   return ChipGeneration::BCM2711;
    case static_cast<uint16_t>(ChipGeneration::BCM2712):   return ChipGeneration::BCM2712;
    default: return std::nullopt;
    }
}

// The synthetic device URI for a board sitting in rpiboot mode.
//
//     rpiboot://<bus>:<address>:<port.path>:<pid>
//
// Kept here in one place because it is written in rpiboot_scanner.cpp and
// read in two others -- ImageWriter::startWrite(), to address the device, and
// DriveListModel, to announce it -- each of which had its own copy of the
// splitting.
//
// The last field is the chip generation, whose value is also the USB product
// ID, written in decimal. It selects which bootcode is uploaded, so a reader
// that disagreed with the writer about the base would quietly send CM4
// firmware to a CM5 and the board would never appear in fastboot mode. That
// agreement is what the round-trip test pins.
struct DeviceUri
{
    bool valid = false;              // false unless at least bus and address parsed
    uint8_t busNumber = 0;
    uint8_t deviceAddress = 0;
    std::vector<uint8_t> portPath;
    std::optional<ChipGeneration> chipGeneration;   // absent when missing or unknown
};

inline std::string portPathToUriField(const std::vector<uint8_t>& portPath)
{
    std::string result;
    for (size_t i = 0; i < portPath.size(); ++i) {
        if (i > 0) result += '.';
        result += std::to_string(portPath[i]);
    }
    return result;
}

inline std::string formatDeviceUri(uint8_t busNumber, uint8_t deviceAddress,
                                   const std::vector<uint8_t>& portPath,
                                   ChipGeneration generation)
{
    return "rpiboot://" + std::to_string(busNumber) + ":"
         + std::to_string(deviceAddress) + ":" + portPathToUriField(portPath)
         + ":" + std::to_string(static_cast<uint16_t>(generation));
}

// Unparseable numbers read as zero, which is what the QString::toUInt() calls
// this replaced did.
inline unsigned long uriFieldToNumber(std::string_view field)
{
    unsigned long value = 0;
    for (const char c : field) {
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + static_cast<unsigned long>(c - '0');
        if (value > 0xFFFFUL)
            return 0;
    }
    return field.empty() ? 0 : value;
}

inline DeviceUri parseDeviceUri(std::string_view uri)
{
    DeviceUri out;

    constexpr std::string_view kScheme = "rpiboot://";
    if (uri.substr(0, kScheme.size()) == kScheme)
        uri.remove_prefix(kScheme.size());

    std::vector<std::string_view> fields;
    while (!uri.empty()) {
        const size_t colon = uri.find(':');
        if (colon == std::string_view::npos) {
            fields.push_back(uri);
            break;
        }
        fields.push_back(uri.substr(0, colon));
        uri.remove_prefix(colon + 1);
    }

    if (fields.size() < 2)
        return out;

    out.valid = true;
    out.busNumber = static_cast<uint8_t>(uriFieldToNumber(fields[0]));
    out.deviceAddress = static_cast<uint8_t>(uriFieldToNumber(fields[1]));

    if (fields.size() >= 3) {
        std::string_view path = fields[2];
        while (!path.empty()) {
            const size_t dot = path.find('.');
            const std::string_view part =
                dot == std::string_view::npos ? path : path.substr(0, dot);
            if (!part.empty())
                out.portPath.push_back(static_cast<uint8_t>(uriFieldToNumber(part)));
            if (dot == std::string_view::npos)
                break;
            path.remove_prefix(dot + 1);
        }
    }

    if (fields.size() >= 4)
        out.chipGeneration =
            chipGenerationFromPid(static_cast<uint16_t>(uriFieldToNumber(fields[3])));

    return out;
}

// Human-readable name for each chip generation
inline std::string_view chipGenerationName(ChipGeneration gen)
{
    switch (gen) {
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
//
// The device's recovery/provisioning firmware streams these back at the end
// of the run as "*PROPERTY*VALUE" file requests (e.g. "*USER_SERIAL_NUM*...",
// "*EEPROM_UPDATE*success", "*SECURE_BOOT_PROVISION*success").  Upstream
// rpiboot (write_metadata_file) records every property/value pair verbatim to
// a JSON file; we mirror that by capturing them all in `fields`, in arrival
// order, and additionally expose the common ones via typed accessors.
struct DeviceMetadata {
    // Every property/value pair the device reported, in the order received.
    std::vector<std::pair<std::string, std::string>> fields;

    // Typed convenience accessors for frequently-used properties, populated
    // from the upstream property names during parsing.
    std::optional<std::string> serialNumber;   // USER_SERIAL_NUM
    std::optional<std::string> macAddress;     // MAC_ADDR
    std::optional<std::string> otpState;       // legacy
    std::optional<uint32_t>    boardRevision;  // USER_BOARDREV (hex)

    // Look up an arbitrary property by its upstream name (e.g. "EEPROM_UPDATE",
    // "SECURE_BOOT_PROVISION", "CUSTOMER_KEY_HASH").  Returns nullptr if the
    // device didn't report it.
    [[nodiscard]] const std::string* find(std::string_view key) const {
        for (const auto& [k, v] : fields)
            if (k == key)
                return &v;
        return nullptr;
    }
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

// USB interface string descriptor (iInterface) advertised by the RPi
// fastboot gadget — rpi-fastbootd's FASTBOOTD_INTERFACE_DESCRIPTOR.  The
// VID/PID above are borrowed from Google, so they cannot distinguish a
// genuine Pi from a non-Pi device in a colliding fastboot mode; this string
// can.  Stock Android fastbootd advertises "fastbootd" / "Android Fastboot"
// instead.  Read at open time (before any fastboot command) to positively
// identify the gadget.
constexpr const char* FASTBOOT_INTERFACE_DESCRIPTOR = "fastbootd-provisioner";

// ── Progress callback ──────────────────────────────────────────────────
// (current, total, status):
//   - Percentage mode: current/total in [0,100] range (e.g. download progress)
//   - Discrete steps:  current/total as step indices (e.g. 2/3)
//   - Indeterminate:   total == 0 means "unknown total" (e.g. file server)
using ProgressCallback = std::function<void(uint64_t current, uint64_t total, const std::string& status)>;

} // namespace rpiboot

#endif // RPIBOOT_TYPES_H
