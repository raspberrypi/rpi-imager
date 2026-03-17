/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Streaming encoder for the Android sparse image format.
 *
 * Converts a raw disk image into a series of sparse image segments,
 * each fitting within the fastboot max-download-size.  Zero-filled
 * regions are emitted as DONT_CARE chunks (never transferred or
 * written), and uniform-fill regions as 4-byte FILL chunks.  This
 * dramatically reduces transfer time for typical OS images.
 */

#ifndef FASTBOOT_SPARSE_ENCODER_H
#define FASTBOOT_SPARSE_ENCODER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fastboot {

// ── Android sparse image on-wire structures ────────────────────────────

static constexpr uint32_t SPARSE_MAGIC        = 0xED26FF3A;
static constexpr uint16_t SPARSE_MAJOR_VER    = 1;
static constexpr uint16_t SPARSE_MINOR_VER    = 0;
static constexpr uint32_t SPARSE_FILE_HDR_SZ  = 28;
static constexpr uint32_t SPARSE_CHUNK_HDR_SZ = 12;
static constexpr uint32_t SPARSE_BLK_SZ       = 4096;

static constexpr uint16_t CHUNK_TYPE_RAW       = 0xCAC1;
static constexpr uint16_t CHUNK_TYPE_FILL      = 0xCAC2;
static constexpr uint16_t CHUNK_TYPE_DONT_CARE = 0xCAC3;

#pragma pack(push, 1)
struct SparseFileHeader {
    uint32_t magic;
    uint16_t major_version;
    uint16_t minor_version;
    uint16_t file_hdr_sz;
    uint16_t chunk_hdr_sz;
    uint32_t blk_sz;
    uint32_t total_blks;
    uint32_t total_chunks;
    uint32_t image_checksum;
};
static_assert(sizeof(SparseFileHeader) == 28);

struct SparseChunkHeader {
    uint16_t chunk_type;
    uint16_t reserved1;
    uint32_t chunk_sz;    // number of blocks
    uint32_t total_sz;    // sizeof(SparseChunkHeader) + data bytes
};
static_assert(sizeof(SparseChunkHeader) == 12);
#pragma pack(pop)

// ── Block classification ───────────────────────────────────────────────
// These are intentionally in the header so the compiler can inline and
// auto-vectorise them at the call site.

// Returns true if all 4096 bytes are zero.
// Uses OR-accumulate over uint64_t — auto-vectorises to SSE2/AVX2/NEON.
inline bool isBlockZero(const uint8_t* data)
{
    const auto* p = reinterpret_cast<const uint64_t*>(data);
    uint64_t acc = 0;
    for (int i = 0; i < static_cast<int>(SPARSE_BLK_SZ / sizeof(uint64_t)); ++i)
        acc |= p[i];
    return acc == 0;
}

// Returns true if the block is a single 32-bit value repeated 1024 times.
// On success, writes the fill value to *fillValue.
inline bool isBlockFill(const uint8_t* data, uint32_t* fillValue)
{
    const auto* p32 = reinterpret_cast<const uint32_t*>(data);
    uint32_t val = p32[0];
    // Build 64-bit pattern and XOR-accumulate
    uint64_t pattern = (static_cast<uint64_t>(val) << 32) | val;
    const auto* p64 = reinterpret_cast<const uint64_t*>(data);
    uint64_t diff = 0;
    for (int i = 0; i < static_cast<int>(SPARSE_BLK_SZ / sizeof(uint64_t)); ++i)
        diff |= (p64[i] ^ pattern);
    if (diff == 0) {
        *fillValue = val;
        return true;
    }
    return false;
}

// ── Streaming encoder ──────────────────────────────────────────────────

class SparseEncoder {
public:
    // maxSegmentSize: max bytes per sparse segment (fastboot max-download-size).
    // totalImageSize: uncompressed image size in bytes (0 if unknown).
    SparseEncoder(uint32_t maxSegmentSize, uint64_t totalImageSize);

    // Feed raw decompressed data.  May be called with any size.
    // Internally buffers partial blocks and emits complete segments.
    // Returns the number of bytes consumed.  When fewer than `size`
    // bytes are consumed, a segment is ready — call takeSegment()
    // then feed() again with the remainder.
    size_t feed(const void* data, size_t size);

    // Signal end of input.  Zero-pads any partial block and finalises
    // the last segment.  May need to be called more than once if the
    // partial block triggers a segment split — keep calling while
    // takeSegment() returns data.
    void finish();

    // Per-segment statistics, populated by takeSegment().
    struct SegmentStats {
        uint32_t blocks = 0;       // total blocks in this segment
        uint32_t chunks = 0;       // number of chunks
        uint32_t rawBlocks = 0;    // blocks sent as RAW
        uint32_t fillBlocks = 0;   // blocks sent as FILL
        uint32_t dontCareBlocks = 0; // blocks skipped as DONT_CARE
        size_t   wireBytes = 0;    // segment size on the wire
    };

    // Returns the next completed segment, or an empty span if none ready.
    // The returned span is valid until the next call to feed() or finish().
    // If stats is non-null, it is populated with per-segment statistics.
    std::span<const uint8_t> takeSegment(SegmentStats* stats = nullptr);

    // Statistics
    uint64_t rawBlockCount() const { return _statsRaw; }
    uint64_t fillBlockCount() const { return _statsFill; }
    uint64_t dontCareBlockCount() const { return _statsDontCare; }
    uint64_t totalBlocksProcessed() const { return _processedBlocks; }

private:
    void processBlock(const uint8_t* block);
    void flushRun();
    void finaliseSegment();
    void beginSegment();

    uint32_t _maxSegmentSize;
    uint64_t _totalImageBlocks;

    // Output buffer — one segment at a time
    std::vector<uint8_t> _out;
    uint32_t _chunkCount = 0;       // chunks in current segment
    uint32_t _segmentBlocks = 0;    // blocks covered by current segment

    // Current run being coalesced
    uint16_t _runType = 0;
    uint32_t _runBlocks = 0;
    uint32_t _runFillValue = 0;
    size_t   _runRawStart = 0;      // offset in _out where RAW data begins

    // Partial block accumulator
    alignas(8) uint8_t _partial[SPARSE_BLK_SZ] = {};
    size_t _partialSize = 0;

    // Image-level tracking
    uint64_t _processedBlocks = 0;

    // Per-segment counters (reset in beginSegment, snapshotted in finaliseSegment)
    uint32_t _segRaw = 0;
    uint32_t _segFill = 0;
    uint32_t _segDontCare = 0;
    SegmentStats _readyStats{};

    // Completed segment ready for takeSegment()
    std::vector<uint8_t> _ready;

    // Stats
    uint64_t _statsRaw = 0;
    uint64_t _statsFill = 0;
    uint64_t _statsDontCare = 0;
};

} // namespace fastboot

#endif // FASTBOOT_SPARSE_ENCODER_H
