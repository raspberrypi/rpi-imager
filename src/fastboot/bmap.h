/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Parser for .bmap (block map) files.
 *
 * A bmap file is a small XML document listing which block ranges of a disk
 * image contain meaningful data.  Unmapped ranges are unallocated filesystem
 * free space that can safely be skipped (DONT_CARE in sparse encoding).
 *
 * Format reference: https://github.com/intel/bmap-tools
 */

#ifndef FASTBOOT_BMAP_H
#define FASTBOOT_BMAP_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastboot {

// A half-open range [begin, end) of block numbers that contain data,
// with an optional SHA-256 checksum covering the raw bytes of the range.
struct BlockRange {
    uint64_t begin;
    uint64_t end;
    std::array<uint8_t, 32> sha256{};  // all-zero if not provided
    bool hasSha256 = false;
};

// ── Binary wire format for fastbootd bmap-verify ────────────────────────
// Matches the structs in fastbootd's commands.cpp.  All integers are
// little-endian, structs are packed (no padding).

static constexpr uint32_t BMAP_WIRE_MAGIC = 0x424D4150;  // "BMAP"

#pragma pack(push, 1)

struct BmapWireHeader {        // offset  size
    uint32_t magic;            //   0      4    0x424D4150, little-endian
    uint32_t block_size;       //   4      4    always 4096
    uint32_t range_count;      //   8      4    number of BmapWireRange entries
    uint32_t reserved;         //  12      4    must be 0
};
static_assert(sizeof(BmapWireHeader) == 16);

struct BmapWireRange {         // offset  size
    uint32_t start_block;      //   0      4    first block (inclusive)
    uint32_t end_block;        //   4      4    last block (inclusive)
    uint8_t  sha256[32];       //   8     32    SHA-256 digest
};
static_assert(sizeof(BmapWireRange) == 40);

#pragma pack(pop)

// ── Parsed block map ────────────────────────────────────────────────────

// Sorted, non-overlapping list of mapped (allocated) block ranges.
// Blocks outside these ranges are unmapped and can be treated as
// DONT_CARE during sparse encoding.
class BlockMap {
public:
    // Parse a bmap XML document.  Returns false on parse error.
    // errorMsg is set on failure.
    bool parse(std::string_view xml, std::string* errorMsg = nullptr);

    // Serialize to the binary wire format expected by fastbootd's
    // oem bmap-load command.  Returns the packed binary payload.
    std::vector<uint8_t> serialize() const;

    // Returns true if block `idx` falls within a mapped range.
    // Blocks are numbered from 0.  Callers that iterate sequentially
    // should use the cursor-based isMappedSequential() instead.
    bool isMapped(uint64_t idx) const;

    // Sequential query optimised for the streaming encoder: tracks a
    // cursor so that sequential calls are O(1) amortised.
    // NOT thread-safe — use one instance per thread.
    bool isMappedSequential(uint64_t idx);

    uint64_t blockSize() const { return _blockSize; }
    uint64_t blockCount() const { return _blockCount; }
    uint64_t mappedBlockCount() const { return _mappedBlockCount; }
    const std::vector<BlockRange>& ranges() const { return _ranges; }
    bool empty() const { return _ranges.empty(); }
    bool hasChecksums() const { return _hasChecksums; }

private:
    uint64_t _blockSize = 4096;
    uint64_t _blockCount = 0;
    uint64_t _mappedBlockCount = 0;
    bool _hasChecksums = false;
    std::vector<BlockRange> _ranges;

    // Cursor for sequential access
    size_t _cursor = 0;
};

// ── Inline implementations ─────────────────────────────────────────────

inline bool BlockMap::isMappedSequential(uint64_t idx)
{
    // Advance cursor past ranges that end before this block
    while (_cursor < _ranges.size() && _ranges[_cursor].end <= idx)
        ++_cursor;

    if (_cursor >= _ranges.size())
        return false;

    return idx >= _ranges[_cursor].begin;
}

} // namespace fastboot

#endif // FASTBOOT_BMAP_H
