/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * In-memory pieeprom.bin parser/editor.
 *
 * The bootloader EEPROM image is a sequence of sections, each starting
 * with a big-endian 8-byte header (magic + length). Modifiable files
 * (config, sigs, keys) live in FILE_MAGIC sections that carry a
 * 12-byte filename. This module lets callers locate and replace those
 * file sections in place while preserving the 8-byte alignment and
 * PAD_MAGIC fill that the on-device bootloader expects --- matching
 * the byte-for-byte behaviour of `rpi-eeprom-config --config`.
 *
 * No USB, no Qt, no networking --- pure std::byte-level manipulation
 * so it can be unit-tested offline against the real tool's output.
 */
#ifndef PIEEPROM_H
#define PIEEPROM_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastboot::pieeprom {

// Section header magic values (big-endian on the wire).
inline constexpr uint32_t MAGIC      = 0x55aaf00fu; // generic section
inline constexpr uint32_t FILE_MAGIC = 0x55aaf11fu; // modifiable named file
inline constexpr uint32_t PAD_MAGIC  = 0x55aafeefu; // erase-value filler
inline constexpr uint32_t MAGIC_MASK = 0xfffff00fu;

// File-section layout constants. See rpi-eeprom-config for canonical values.
//   offset+0:  magic     (4)
//   offset+4:  length    (4)   -- value of the length field stored in the image
//   offset+8:  filename  (12)  -- FILE_MAGIC only, NUL-padded
//   offset+20: reserved  (4)   -- FILE_MAGIC only, observed to be 0
//   offset+24: content        -- FILE_MAGIC only
//
// For non-FILE_MAGIC sections, the payload starts immediately at offset+8
// and consumes `length` bytes (no filename/reserved fields).
//
// FILE_HDR_LEN matches rpi-eeprom-config's definition: 20 bytes from the
// start of the length field to the start of content (4-byte length +
// 12-byte filename + 4-byte reserved). The content-start expression is
// therefore `offset + 4 + FILE_HDR_LEN` = `offset + 24`.
inline constexpr size_t SECTION_HDR_LEN  = 8;    // magic + length
inline constexpr size_t FILENAME_LEN     = 12;
inline constexpr size_t FILE_HDR_LEN     = 20;   // length(4) + filename(12) + reserved(4)
inline constexpr size_t FILE_CONTENT_OFF = 24;   // section start -> content start
inline constexpr size_t ERASE_ALIGN      = 4096; // SPI flash erase sector
inline constexpr size_t READ_ONLY_SIZE   = 64 * 1024;
inline constexpr size_t PARTITION_SIZE   = 988 * 1024; // single (non-AB) partition

inline constexpr size_t IMAGE_SIZE_2711 = 512 * 1024;
inline constexpr size_t IMAGE_SIZE_2712 = 2 * 1024 * 1024;

// Conventional filenames inside a pieeprom.bin.
inline constexpr std::string_view FILE_BOOTCONF_TXT = "bootconf.txt";
inline constexpr std::string_view FILE_BOOTCONF_SIG = "bootconf.sig";
inline constexpr std::string_view FILE_PUBKEY_BIN   = "pubkey.bin";

// Boot sources, by nibble value. BOOT_ORDER is evaluated right-to-left
// (LSB first) by the bootloader.
enum class BootSource : uint8_t {
    Stop    = 0x0,
    SdCard  = 0x1,
    Network = 0x2,
    Rpiboot = 0x3,
    UsbMsd  = 0x4,
    Nvme    = 0x6,
    Http    = 0x7,
    StopErr = 0xe,
    Restart = 0xf,
};

struct Section {
    uint32_t magic = 0;
    uint32_t length = 0;       // value of the length field as stored in the image
    size_t   offset = 0;       // byte offset of the section start in the image buffer
    std::string filename;      // populated only for FILE_MAGIC sections (stripped of trailing NULs)

    bool isFile() const { return magic == FILE_MAGIC; }

    // Byte range of the section's content. For FILE_MAGIC sections this
    // is the data after the (length, filename, reserved) header; for
    // other sections it is the raw payload after the 8-byte section
    // header.
    size_t contentOffset() const { return isFile() ? offset + FILE_CONTENT_OFF
                                                   : offset + SECTION_HDR_LEN; }
    size_t contentSize()  const { return isFile() ? length - (FILENAME_LEN + 4)
                                                   : length; }
};

// Owning, mutable view over a pieeprom.bin buffer.
class Image {
public:
    Image() = default;
    explicit Image(std::vector<uint8_t> bytes);

    // Re-parse the section table from the current buffer. Returns an
    // error string on corruption, empty on success.
    std::string parse();

    // Bytes view.
    const std::vector<uint8_t>& bytes() const { return _bytes; }
    std::vector<uint8_t>&       mutableBytes()       { return _bytes; }

    // Section table (read-only).
    const std::vector<Section>& sections() const { return _sections; }

    // Locate a FILE_MAGIC section by name. Returns nullptr if missing.
    const Section* findFile(std::string_view filename) const;

    // Read the content bytes of a named file section. Returns nullopt
    // if the file does not exist.
    std::optional<std::vector<uint8_t>> readFile(std::string_view filename) const;

    // Replace the content of a named file section with `newContent`.
    // Mirrors `rpi-eeprom-config` semantics:
    //   - Updates the length field at offset+4 to len(newContent)+16
    //   - Pads to 8-byte boundary with 0xff
    //   - Inserts a PAD_MAGIC section to fill the gap up to the next section
    //     (unless the modified section was the last one)
    // Returns an empty string on success, an error message on failure
    // (file missing, content too big, or layout would collide with a
    // following section).
    std::string writeFile(std::string_view filename, std::span<const uint8_t> newContent);

    // Convenience: read bootconf.txt as a UTF-8 string.
    std::optional<std::string> readBootConfText() const;

    // Convenience: replace bootconf.txt with a UTF-8 string.
    std::string writeBootConfText(std::string_view text);

    // Replace bootconf.sig with a signature blob produced by
    // rpi-eeprom-digest (a small text file: "<sha256-hex>\nts: <ts>\n[rsa2048: <hex>\n]").
    std::string writeBootConfSig(std::span<const uint8_t> sigContent);

private:
    std::vector<uint8_t>   _bytes;
    std::vector<Section>   _sections;
};

// Parse a BOOT_ORDER=0x.... line out of a bootconf.txt body.
// Returns the 32-bit value, or nullopt if absent / malformed.
std::optional<uint32_t> parseBootOrder(std::string_view bootconf);

// Emit a new bootconf.txt body with BOOT_ORDER set to `value`.
// Preserves all other lines; replaces the existing BOOT_ORDER line in
// place, or appends one if absent.
std::string setBootOrderLine(std::string_view bootconf, uint32_t value);

// Compose a new BOOT_ORDER value by placing `first` as the LSB nibble
// and keeping the remaining nibbles from `current` (with `first`
// removed if it already appeared). The high nibble is always Restart
// (0xf) so the device retries from the start.
//
// Example: current=0xf461, first=Nvme(6) -> 0xf416
//          current=0xf41,  first=Nvme(6) -> 0xf416
//          current=0x0,    first=SdCard(1) -> 0xf1
uint32_t composeBootOrder(uint32_t current, BootSource first);

// String form of a BootSource for diagnostics.
std::string_view bootSourceName(BootSource s);

} // namespace fastboot::pieeprom

#endif // PIEEPROM_H
