/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */
#include "pieeprom.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>

namespace fastboot::pieeprom {

namespace {

uint32_t readBE32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return (uint32_t(bytes[offset    ]) << 24)
         | (uint32_t(bytes[offset + 1]) << 16)
         | (uint32_t(bytes[offset + 2]) << 8 )
         | (uint32_t(bytes[offset + 3]));
}

void writeBE32(std::vector<uint8_t>& bytes, size_t offset, uint32_t v)
{
    bytes[offset    ] = uint8_t((v >> 24) & 0xff);
    bytes[offset + 1] = uint8_t((v >> 16) & 0xff);
    bytes[offset + 2] = uint8_t((v >> 8 ) & 0xff);
    bytes[offset + 3] = uint8_t( v        & 0xff);
}

size_t alignUp8(size_t v) { return (v + 7u) & ~size_t(7u); }

} // namespace

// ── Image ────────────────────────────────────────────────────────────

Image::Image(std::vector<uint8_t> bytes) : _bytes(std::move(bytes)) {}

std::string Image::parse()
{
    _sections.clear();

    if (_bytes.size() < SECTION_HDR_LEN)
        return "image too small";

    size_t offset = 0;
    while (offset + SECTION_HDR_LEN <= _bytes.size()) {
        uint32_t magic  = readBE32(_bytes, offset);
        uint32_t length = readBE32(_bytes, offset + 4);

        // The bootloader scratch page at the tail of the image contains
        // 0x00 or 0xff fill; both terminate the section walk.
        if (magic == 0x0u || magic == 0xffffffffu)
            break;

        if ((magic & MAGIC_MASK) != MAGIC) {
            std::ostringstream err;
            err << "corrupt section at offset 0x" << std::hex << offset
                << ": magic=0x" << magic;
            return err.str();
        }

        Section s;
        s.magic  = magic;
        s.length = length;
        s.offset = offset;
        if (magic == FILE_MAGIC && offset + SECTION_HDR_LEN + FILENAME_LEN <= _bytes.size()) {
            std::string fn(reinterpret_cast<const char*>(&_bytes[offset + SECTION_HDR_LEN]),
                           FILENAME_LEN);
            if (auto nul = fn.find('\0'); nul != std::string::npos)
                fn.resize(nul);
            s.filename = std::move(fn);
        }

        const size_t sectionEnd = offset + SECTION_HDR_LEN + length;
        if (sectionEnd > _bytes.size()) {
            std::ostringstream err;
            err << "section at 0x" << std::hex << offset
                << " runs past image end (length=" << std::dec << length << ")";
            return err.str();
        }
        _sections.push_back(std::move(s));

        offset = alignUp8(sectionEnd);
    }

    return {};
}

const Section* Image::findFile(std::string_view filename) const
{
    for (const auto& s : _sections) {
        if (s.isFile() && s.filename == filename)
            return &s;
    }
    return nullptr;
}

std::optional<std::vector<uint8_t>> Image::readFile(std::string_view filename) const
{
    const Section* s = findFile(filename);
    if (!s)
        return std::nullopt;
    auto begin = _bytes.begin() + static_cast<ptrdiff_t>(s->contentOffset());
    auto end   = begin + static_cast<ptrdiff_t>(s->contentSize());
    return std::vector<uint8_t>(begin, end);
}

std::string Image::writeFile(std::string_view filename, std::span<const uint8_t> newContent)
{
    // Locate the target section and the next non-PAD section after it
    // (or the end of the writable area). We must not encroach on the
    // bootloader scratch page at the tail.
    const Section* target = nullptr;
    size_t targetIdx = 0;
    for (size_t i = 0; i < _sections.size(); ++i) {
        if (_sections[i].isFile() && _sections[i].filename == filename) {
            target = &_sections[i];
            targetIdx = i;
            break;
        }
    }
    if (!target) {
        return "file '" + std::string(filename) + "' not found in image";
    }

    // Match rpi-eeprom-config: modifiable files (everything except the
    // bootcode) live in a single 988 KiB partition that starts at
    // READ_ONLY_SIZE. The rewrite must not extend past this partition's
    // upper bound, even if the original image has a PAD section that
    // happens to reach the very end of the flash. Using writableEnd as
    // the bound here would consolidate two adjacent PADs into one and
    // break byte-equivalence with `rpi-eeprom-config`.
    //
    // `isLast` mirrors python semantics: true iff the target is the
    // very last entry in the parsed section list (whether file or pad).
    const size_t writableEnd  = _bytes.size() > ERASE_ALIGN
                              ? _bytes.size() - ERASE_ALIGN
                              : _bytes.size();
    const size_t partitionEnd = std::min(writableEnd,
                                          READ_ONLY_SIZE + PARTITION_SIZE);

    const bool isLast = (targetIdx == _sections.size() - 1);
    size_t nextOffset = partitionEnd;
    for (size_t i = targetIdx + 1; i < _sections.size(); ++i) {
        if (_sections[i].magic == PAD_MAGIC)
            continue;
        nextOffset = _sections[i].offset;
        break;
    }

    const size_t hdrOffset    = target->offset;
    const size_t newLen       = newContent.size() + FILENAME_LEN + 4;
    const size_t newSectionEnd = hdrOffset + FILE_CONTENT_OFF + newContent.size();

    if (newContent.size() > ERASE_ALIGN - FILE_HDR_LEN) {
        return "file content too large (max " + std::to_string(ERASE_ALIGN - FILE_HDR_LEN) + " bytes)";
    }
    if (newSectionEnd > nextOffset) {
        return "replacement does not fit (would overlap next section)";
    }

    // Write the new length field (BE) and content at +24.
    writeBE32(_bytes, hdrOffset + 4, static_cast<uint32_t>(newLen));
    std::memcpy(&_bytes[hdrOffset + FILE_CONTENT_OFF],
                newContent.data(), newContent.size());

    // Pad to 8-byte boundary with 0xff (flash erase value).
    size_t padStart = hdrOffset + FILE_CONTENT_OFF + newContent.size();
    while (padStart % 8 != 0) {
        _bytes[padStart++] = 0xff;
    }

    // If there is room before nextOffset, drop a PAD_MAGIC section that
    // consumes the remainder. Skip this when the target is the last
    // section --- matching rpi-eeprom-config, which leaves the tail of
    // the partition free for the next config that fits.
    const size_t padBytes = nextOffset - padStart;
    if (padBytes >= SECTION_HDR_LEN && !isLast) {
        writeBE32(_bytes, padStart,     PAD_MAGIC);
        writeBE32(_bytes, padStart + 4, static_cast<uint32_t>(padBytes - SECTION_HDR_LEN));
        padStart += SECTION_HDR_LEN;
        // The payload of a PAD section is just 0xff fill.
        for (size_t i = padStart; i < nextOffset; ++i)
            _bytes[i] = 0xff;
    } else {
        for (size_t i = padStart; i < nextOffset; ++i)
            _bytes[i] = 0xff;
    }

    // Re-parse to refresh the section table.
    return parse();
}

std::optional<std::string> Image::readBootConfText() const
{
    auto raw = readFile(FILE_BOOTCONF_TXT);
    if (!raw) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(raw->data()), raw->size());
}

std::string Image::writeBootConfText(std::string_view text)
{
    auto data = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text.data()), text.size());
    return writeFile(FILE_BOOTCONF_TXT, data);
}

std::string Image::writeBootConfSig(std::span<const uint8_t> sigContent)
{
    return writeFile(FILE_BOOTCONF_SIG, sigContent);
}

// ── BOOT_ORDER helpers ───────────────────────────────────────────────

std::optional<uint32_t> parseBootOrder(std::string_view bootconf)
{
    // Look for a line of the form `BOOT_ORDER=0x....` (case-insensitive
    // on the prefix, value parsed as hex). Lines starting with '#' are
    // comments. Sections (`[all]` etc.) are ignored --- we treat
    // bootconf.txt as a flat key/value file because that is how the Pi
    // bootloader itself parses it (only the active filter section is
    // applied, but BOOT_ORDER outside any section is the default).
    size_t pos = 0;
    while (pos < bootconf.size()) {
        size_t eol = bootconf.find('\n', pos);
        std::string_view line = bootconf.substr(pos, eol == std::string_view::npos
                                                ? bootconf.size() - pos
                                                : eol - pos);
        pos = (eol == std::string_view::npos) ? bootconf.size() : eol + 1;

        // Trim leading whitespace
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
            ++start;
        if (start >= line.size() || line[start] == '#' || line[start] == '[')
            continue;

        constexpr std::string_view key = "BOOT_ORDER";
        if (line.size() < start + key.size() + 1)
            continue;
        if (line.substr(start, key.size()) != key)
            continue;
        size_t eq = start + key.size();
        if (line[eq] != '=')
            continue;
        std::string_view val = line.substr(eq + 1);
        // Trim trailing whitespace/CR
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r'))
            val.remove_suffix(1);

        int base = 10;
        if (val.size() > 2 && val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) {
            val.remove_prefix(2);
            base = 16;
        }
        uint32_t v = 0;
        for (char c : val) {
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
            else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
            if (digit < 0) return std::nullopt;
            v = v * static_cast<uint32_t>(base) + static_cast<uint32_t>(digit);
        }
        return v;
    }
    return std::nullopt;
}

std::string setBootOrderLine(std::string_view bootconf, uint32_t value)
{
    // Build the new line. Use lower-case hex with no leading zeros
    // beyond the natural width, matching the convention in the
    // upstream bootconf.txt files.
    char buf[32];
    int n = std::snprintf(buf, sizeof(buf), "BOOT_ORDER=0x%x", value);
    std::string newLine(buf, static_cast<size_t>(n));

    std::ostringstream out;
    bool replaced = false;
    size_t pos = 0;
    while (pos < bootconf.size()) {
        size_t eol = bootconf.find('\n', pos);
        std::string_view line = bootconf.substr(pos, eol == std::string_view::npos
                                                ? bootconf.size() - pos
                                                : eol - pos);
        bool hadNewline = (eol != std::string_view::npos);
        pos = hadNewline ? eol + 1 : bootconf.size();

        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
            ++start;
        bool isBootOrder = !replaced
            && line.size() >= start + 11
            && line.substr(start, 10) == "BOOT_ORDER"
            && line[start + 10] == '=';

        if (isBootOrder) {
            out << newLine;
            replaced = true;
        } else {
            out << line;
        }
        if (hadNewline)
            out << '\n';
    }
    if (!replaced) {
        if (!bootconf.empty() && bootconf.back() != '\n')
            out << '\n';
        out << newLine << '\n';
    }
    return out.str();
}

uint32_t composeBootOrder(uint32_t current, BootSource first)
{
    const uint8_t firstNibble = static_cast<uint8_t>(first) & 0xfu;

    // Strip the high RESTART nibble (if any) and collect remaining
    // nibbles right-to-left (LSB first), dropping zero pads and the
    // `first` nibble where it already appears, so we can re-prepend it.
    std::array<uint8_t, 8> nibbles{};
    size_t count = 0;
    uint32_t v = current;
    while (v != 0 && count < nibbles.size()) {
        uint8_t n = v & 0xfu;
        v >>= 4;
        if (n == static_cast<uint8_t>(BootSource::Restart))
            continue; // we'll re-add this at the top
        if (n == firstNibble)
            continue; // skip duplicates of the new first nibble
        if (n == 0)
            continue;
        nibbles[count++] = n;
    }

    // Rebuild: [restart][...original remaining...][firstNibble]
    // Restart sits at the top (highest active nibble) so the bootloader
    // loops back after exhausting all entries.
    uint32_t out = firstNibble;
    int shift = 4;
    for (size_t i = 0; i < count && shift < 32; ++i, shift += 4) {
        out |= static_cast<uint32_t>(nibbles[i]) << shift;
    }
    // Append the Restart nibble at the top
    if (shift < 32) {
        out |= static_cast<uint32_t>(BootSource::Restart) << shift;
    }
    return out;
}

std::string_view bootSourceName(BootSource s)
{
    switch (s) {
        case BootSource::Stop:    return "STOP";
        case BootSource::SdCard:  return "SD";
        case BootSource::Network: return "NETWORK";
        case BootSource::Rpiboot: return "RPIBOOT";
        case BootSource::UsbMsd:  return "USB-MSD";
        case BootSource::Nvme:    return "NVME";
        case BootSource::Http:    return "HTTP";
        case BootSource::StopErr: return "STOP-ERR";
        case BootSource::Restart: return "RESTART";
    }
    return "?";
}

} // namespace fastboot::pieeprom
