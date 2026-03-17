/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "sparse_encoder.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace fastboot {

// Minimum segment must hold: file header + one chunk header + one block
static constexpr size_t MIN_SEGMENT_SIZE =
    SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ + SPARSE_BLK_SZ;

SparseEncoder::SparseEncoder(uint32_t maxSegmentSize, uint64_t totalImageSize)
    : _maxSegmentSize(maxSegmentSize)
    , _totalImageBlocks(totalImageSize > 0
          ? (totalImageSize + SPARSE_BLK_SZ - 1) / SPARSE_BLK_SZ
          : 0)
{
    assert(maxSegmentSize >= MIN_SEGMENT_SIZE);
    _out.reserve(maxSegmentSize);
    _ready.reserve(maxSegmentSize);
    beginSegment();
}

void SparseEncoder::beginSegment()
{
    _out.clear();
    _out.resize(SPARSE_FILE_HDR_SZ);  // placeholder for header
    _chunkCount = 0;
    _segmentBlocks = 0;
    _runType = 0;
    _runBlocks = 0;
    _segRaw = 0;
    _segFill = 0;
    _segDontCare = 0;
}


void SparseEncoder::flushRun()
{
    if (_runBlocks == 0)
        return;

    SparseChunkHeader hdr{};
    hdr.chunk_type = _runType;
    hdr.chunk_sz = _runBlocks;

    switch (_runType) {
    case CHUNK_TYPE_RAW:
        // The raw data is already in _out starting at _runRawStart.
        // Write the chunk header just before the data.
        hdr.total_sz = SPARSE_CHUNK_HDR_SZ + _runBlocks * SPARSE_BLK_SZ;
        std::memcpy(_out.data() + _runRawStart - SPARSE_CHUNK_HDR_SZ,
                     &hdr, sizeof(hdr));
        break;

    case CHUNK_TYPE_FILL:
        hdr.total_sz = SPARSE_CHUNK_HDR_SZ + 4;
        {
            size_t pos = _out.size();
            _out.resize(pos + SPARSE_CHUNK_HDR_SZ + 4);
            std::memcpy(_out.data() + pos, &hdr, sizeof(hdr));
            std::memcpy(_out.data() + pos + SPARSE_CHUNK_HDR_SZ,
                         &_runFillValue, 4);
        }
        break;

    case CHUNK_TYPE_DONT_CARE:
        hdr.total_sz = SPARSE_CHUNK_HDR_SZ;
        {
            size_t pos = _out.size();
            _out.resize(pos + SPARSE_CHUNK_HDR_SZ);
            std::memcpy(_out.data() + pos, &hdr, sizeof(hdr));
        }
        break;
    }

    _segmentBlocks += _runBlocks;
    ++_chunkCount;
    _runBlocks = 0;
    _runType = 0;
}

void SparseEncoder::finaliseSegment()
{
    flushRun();

    if (_chunkCount == 0)
        return;  // nothing to emit

    // Write the file header
    SparseFileHeader fhdr{};
    fhdr.magic = SPARSE_MAGIC;
    fhdr.major_version = SPARSE_MAJOR_VER;
    fhdr.minor_version = SPARSE_MINOR_VER;
    fhdr.file_hdr_sz = SPARSE_FILE_HDR_SZ;
    fhdr.chunk_hdr_sz = SPARSE_CHUNK_HDR_SZ;
    fhdr.blk_sz = SPARSE_BLK_SZ;
    fhdr.total_blks = _segmentBlocks;
    fhdr.total_chunks = _chunkCount;
    fhdr.image_checksum = 0;
    std::memcpy(_out.data(), &fhdr, sizeof(fhdr));

    // Snapshot per-segment stats
    _readyStats = SegmentStats{};
    _readyStats.blocks = _segmentBlocks;
    _readyStats.chunks = _chunkCount;
    _readyStats.rawBlocks = _segRaw;
    _readyStats.fillBlocks = _segFill;
    _readyStats.dontCareBlocks = _segDontCare;
    _readyStats.wireBytes = _out.size();

    // Move to ready buffer
    _ready = std::move(_out);
    beginSegment();
}

void SparseEncoder::processBlock(const uint8_t* block)
{
    // Classify the block
    uint16_t type;
    uint32_t fillVal = 0;

    if (isBlockZero(block)) {
        type = CHUNK_TYPE_DONT_CARE;
        ++_statsDontCare;
    } else if (isBlockFill(block, &fillVal)) {
        type = CHUNK_TYPE_FILL;
        ++_statsFill;
    } else {
        type = CHUNK_TYPE_RAW;
        ++_statsRaw;
    }

    // Does this block continue the current run?
    bool continues = (_runBlocks > 0 && type == _runType);
    if (continues && type == CHUNK_TYPE_FILL && fillVal != _runFillValue)
        continues = false;

    // Would this block fit in the current segment?
    size_t cost = continues
        ? (type == CHUNK_TYPE_RAW ? SPARSE_BLK_SZ : 0)
        : (SPARSE_CHUNK_HDR_SZ + (type == CHUNK_TYPE_RAW ? SPARSE_BLK_SZ
                                 : type == CHUNK_TYPE_FILL ? 4 : 0));
    if (_out.size() + cost > _maxSegmentSize) {
        finaliseSegment();
        continues = false;
        cost = SPARSE_CHUNK_HDR_SZ + (type == CHUNK_TYPE_RAW ? SPARSE_BLK_SZ
                                     : type == CHUNK_TYPE_FILL ? 4 : 0);
    }

    // Increment per-segment stats after the potential segment split so
    // this block is counted against the segment it actually belongs to.
    switch (type) {
    case CHUNK_TYPE_DONT_CARE: ++_segDontCare; break;
    case CHUNK_TYPE_FILL:      ++_segFill; break;
    case CHUNK_TYPE_RAW:       ++_segRaw; break;
    }

    // Start a new run if not continuing
    if (!continues) {
        flushRun();
        _runType = type;
        _runBlocks = 0;
        _runFillValue = fillVal;

        if (type == CHUNK_TYPE_RAW) {
            // Reserve space for chunk header before the raw data
            _out.resize(_out.size() + SPARSE_CHUNK_HDR_SZ);
            _runRawStart = _out.size();
        }
    }

    // Append data for RAW blocks
    if (type == CHUNK_TYPE_RAW) {
        size_t pos = _out.size();
        _out.resize(pos + SPARSE_BLK_SZ);
        std::memcpy(_out.data() + pos, block, SPARSE_BLK_SZ);
    }

    ++_runBlocks;
    ++_processedBlocks;
}

size_t SparseEncoder::feed(const void* data, size_t size)
{
    _ready.clear();  // caller has consumed the previous segment
    auto* begin = static_cast<const uint8_t*>(data);
    auto* src = begin;
    auto* end = src + size;

    // Drain partial block first
    if (_partialSize > 0) {
        size_t need = SPARSE_BLK_SZ - _partialSize;
        size_t take = std::min(need, static_cast<size_t>(end - src));
        std::memcpy(_partial + _partialSize, src, take);
        _partialSize += take;
        src += take;

        if (_partialSize == SPARSE_BLK_SZ) {
            processBlock(_partial);
            _partialSize = 0;
            // processBlock may have finalised a segment — stop so the
            // caller can drain it before we overwrite _ready.
            if (!_ready.empty())
                return static_cast<size_t>(src - begin);
        } else {
            return size;  // all consumed, still not a full block
        }
    }

    // Process full blocks directly from input (zero-copy classification)
    while (static_cast<size_t>(end - src) >= SPARSE_BLK_SZ) {
        processBlock(src);
        src += SPARSE_BLK_SZ;
        if (!_ready.empty())
            return static_cast<size_t>(src - begin);
    }

    // Buffer remainder
    size_t rem = static_cast<size_t>(end - src);
    if (rem > 0) {
        std::memcpy(_partial, src, rem);
        _partialSize = rem;
    }

    return size;
}

void SparseEncoder::finish()
{
    _ready.clear();  // caller has consumed the previous segment

    // Zero-pad and process any partial block
    if (_partialSize > 0) {
        std::memset(_partial + _partialSize, 0, SPARSE_BLK_SZ - _partialSize);
        processBlock(_partial);
        _partialSize = 0;
        // processBlock may have finalised a segment — let the caller
        // drain it first.  The next call to finish() will fall through
        // to finaliseSegment() below.
        if (!_ready.empty())
            return;
    }

    // Finalise the last segment (no-op if nothing pending)
    finaliseSegment();
}

std::span<const uint8_t> SparseEncoder::takeSegment(SegmentStats* stats)
{
    if (_ready.empty())
        return {};
    if (stats)
        *stats = _readyStats;
    return {_ready.data(), _ready.size()};
}

} // namespace fastboot
