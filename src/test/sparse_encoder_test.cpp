/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for the Android sparse image encoder.
 */

#include <catch2/catch_test_macros.hpp>

#include "fastboot/sparse_encoder.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>

using namespace fastboot;

// ── Helpers ─────────────────────────────────────────────────────────────

// Parse a sparse image and return the decoded raw image
static std::vector<uint8_t> decodeSparse(std::span<const uint8_t> sparse)
{
    REQUIRE(sparse.size() >= sizeof(SparseFileHeader));
    SparseFileHeader fhdr;
    std::memcpy(&fhdr, sparse.data(), sizeof(fhdr));
    REQUIRE(fhdr.magic == SPARSE_MAGIC);
    REQUIRE(fhdr.major_version == SPARSE_MAJOR_VER);
    REQUIRE(fhdr.file_hdr_sz == SPARSE_FILE_HDR_SZ);
    REQUIRE(fhdr.chunk_hdr_sz == SPARSE_CHUNK_HDR_SZ);
    REQUIRE(fhdr.blk_sz == SPARSE_BLK_SZ);

    std::vector<uint8_t> out(static_cast<size_t>(fhdr.total_blks) * SPARSE_BLK_SZ, 0);
    size_t pos = SPARSE_FILE_HDR_SZ;
    size_t outOff = 0;

    for (uint32_t i = 0; i < fhdr.total_chunks; ++i) {
        REQUIRE(pos + sizeof(SparseChunkHeader) <= sparse.size());
        SparseChunkHeader chdr;
        std::memcpy(&chdr, sparse.data() + pos, sizeof(chdr));
        pos += sizeof(SparseChunkHeader);

        size_t blockBytes = static_cast<size_t>(chdr.chunk_sz) * SPARSE_BLK_SZ;

        switch (chdr.chunk_type) {
        case CHUNK_TYPE_RAW:
            REQUIRE(pos + blockBytes <= sparse.size());
            std::memcpy(out.data() + outOff, sparse.data() + pos, blockBytes);
            pos += blockBytes;
            break;

        case CHUNK_TYPE_FILL: {
            REQUIRE(pos + 4 <= sparse.size());
            uint32_t fillVal;
            std::memcpy(&fillVal, sparse.data() + pos, 4);
            pos += 4;
            for (size_t j = 0; j < blockBytes; j += 4)
                std::memcpy(out.data() + outOff + j, &fillVal, 4);
            break;
        }

        case CHUNK_TYPE_DONT_CARE:
            // Leave as zeros
            break;

        default:
            FAIL("Unknown chunk type: " << chdr.chunk_type);
        }

        outOff += blockBytes;
    }

    REQUIRE(outOff == out.size());
    return out;
}

// Decode multiple sparse segments and concatenate the raw output
static std::vector<uint8_t> decodeSegments(const std::vector<std::vector<uint8_t>>& segments)
{
    std::vector<uint8_t> result;
    for (const auto& seg : segments) {
        auto decoded = decodeSparse({seg.data(), seg.size()});
        result.insert(result.end(), decoded.begin(), decoded.end());
    }
    return result;
}

// Collect all segments from an encoder (after finish())
static std::vector<std::vector<uint8_t>> collectSegments(SparseEncoder& enc)
{
    std::vector<std::vector<uint8_t>> segments;
    while (true) {
        auto seg = enc.takeSegment();
        if (seg.empty())
            break;
        segments.emplace_back(seg.begin(), seg.end());
        // Trigger clearing of _ready so the next takeSegment returns empty
        // (in real use, feed() or finish() does this)
        // Since we've already called finish(), we just break after one.
        break;
    }
    return segments;
}

// Feed data and collect all emitted segments along the way + after finish
static std::vector<std::vector<uint8_t>> feedAndCollect(
    SparseEncoder& enc, const std::vector<uint8_t>& data)
{
    std::vector<std::vector<uint8_t>> segments;

    // Feed in 64KB chunks to exercise partial-block handling.
    // feed() may return fewer bytes than offered when a segment is ready,
    // so we loop until all data is consumed.
    constexpr size_t FEED_SIZE = 65536;
    size_t off = 0;
    while (off < data.size()) {
        size_t chunk = std::min(FEED_SIZE, data.size() - off);
        size_t consumed = enc.feed(data.data() + off, chunk);
        off += consumed;

        auto seg = enc.takeSegment();
        if (!seg.empty())
            segments.emplace_back(seg.begin(), seg.end());
    }

    // finish() may need multiple calls if the trailing partial block
    // triggers a segment split.
    while (true) {
        enc.finish();
        auto seg = enc.takeSegment();
        if (seg.empty())
            break;
        segments.emplace_back(seg.begin(), seg.end());
    }

    return segments;
}

// ── Tests ───────────────────────────────────────────────────────────────

TEST_CASE("All-zero image produces only DONT_CARE chunks", "[sparse]")
{
    constexpr size_t IMAGE_SIZE = 64 * SPARSE_BLK_SZ;  // 256 KB
    std::vector<uint8_t> image(IMAGE_SIZE, 0);

    SparseEncoder enc(1024 * 1024, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    REQUIRE(segments.size() == 1);

    // Decode and verify
    auto decoded = decodeSparse({segments[0].data(), segments[0].size()});
    REQUIRE(decoded.size() == IMAGE_SIZE);
    REQUIRE(decoded == image);

    // Check stats
    REQUIRE(enc.dontCareBlockCount() == 64);
    REQUIRE(enc.rawBlockCount() == 0);
    REQUIRE(enc.fillBlockCount() == 0);

    // The sparse segment should be tiny (header + one DONT_CARE chunk)
    REQUIRE(segments[0].size() == SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ);
}

TEST_CASE("Fill-pattern image produces FILL chunks", "[sparse]")
{
    constexpr size_t IMAGE_SIZE = 16 * SPARSE_BLK_SZ;
    std::vector<uint8_t> image(IMAGE_SIZE);

    // Fill with repeating 0xDEADBEEF pattern
    uint32_t pattern = 0xDEADBEEF;
    for (size_t i = 0; i < IMAGE_SIZE; i += 4)
        std::memcpy(image.data() + i, &pattern, 4);

    SparseEncoder enc(1024 * 1024, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    REQUIRE(segments.size() == 1);

    auto decoded = decodeSparse({segments[0].data(), segments[0].size()});
    REQUIRE(decoded == image);

    REQUIRE(enc.fillBlockCount() == 16);
    REQUIRE(enc.rawBlockCount() == 0);
    REQUIRE(enc.dontCareBlockCount() == 0);

    // Sparse segment: header + one FILL chunk (12 + 4 bytes)
    REQUIRE(segments[0].size() == SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ + 4);
}

TEST_CASE("Random data produces RAW chunks", "[sparse]")
{
    constexpr size_t IMAGE_SIZE = 8 * SPARSE_BLK_SZ;
    std::vector<uint8_t> image(IMAGE_SIZE);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 255);  // no all-zero blocks
    for (auto& b : image)
        b = static_cast<uint8_t>(dist(rng));

    SparseEncoder enc(1024 * 1024, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    REQUIRE(segments.size() == 1);

    auto decoded = decodeSparse({segments[0].data(), segments[0].size()});
    REQUIRE(decoded == image);

    REQUIRE(enc.rawBlockCount() == 8);
}

TEST_CASE("Mixed image: raw + zero + fill + raw", "[sparse]")
{
    // 4 blocks: random, zeros, fill(0xAA), random
    std::vector<uint8_t> image(4 * SPARSE_BLK_SZ, 0);

    // Block 0: random
    std::mt19937 rng(123);
    for (size_t i = 0; i < SPARSE_BLK_SZ; ++i)
        image[i] = static_cast<uint8_t>(rng() & 0xFF);
    // Make sure it's not accidentally all-zero or fill
    image[0] = 1; image[1] = 2; image[2] = 3; image[3] = 4;

    // Block 1: zeros (already zero)

    // Block 2: fill with 0xAAAAAAAA
    uint32_t fill = 0xAAAAAAAA;
    for (size_t i = 2 * SPARSE_BLK_SZ; i < 3 * SPARSE_BLK_SZ; i += 4)
        std::memcpy(image.data() + i, &fill, 4);

    // Block 3: random
    for (size_t i = 3 * SPARSE_BLK_SZ; i < 4 * SPARSE_BLK_SZ; ++i)
        image[i] = static_cast<uint8_t>(rng() & 0xFF);
    image[3 * SPARSE_BLK_SZ] = 5;

    SparseEncoder enc(1024 * 1024, image.size());
    auto segments = feedAndCollect(enc, image);

    REQUIRE(segments.size() == 1);

    auto decoded = decodeSparse({segments[0].data(), segments[0].size()});
    REQUIRE(decoded == image);

    REQUIRE(enc.rawBlockCount() == 2);
    REQUIRE(enc.dontCareBlockCount() == 1);
    REQUIRE(enc.fillBlockCount() == 1);
}

TEST_CASE("Segment splitting respects maxDownloadSize", "[sparse]")
{
    // Use a tiny max segment size: enough for header + ~2 RAW blocks
    constexpr uint32_t MAX_SEG = SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ + 2 * SPARSE_BLK_SZ;
    constexpr size_t IMAGE_SIZE = 8 * SPARSE_BLK_SZ;

    // All random data — forces RAW chunks, segment splits every ~2 blocks
    std::vector<uint8_t> image(IMAGE_SIZE);
    std::mt19937 rng(99);
    for (auto& b : image)
        b = static_cast<uint8_t>(rng() % 254 + 1);

    SparseEncoder enc(MAX_SEG, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    // Should need multiple segments
    REQUIRE(segments.size() >= 4);

    // Each segment must be <= MAX_SEG
    for (const auto& seg : segments)
        REQUIRE(seg.size() <= MAX_SEG);

    // Decode all segments and verify round-trip
    auto decoded = decodeSegments(segments);
    REQUIRE(decoded == image);
}

TEST_CASE("Partial final block is zero-padded", "[sparse]")
{
    // Image that's not block-aligned
    constexpr size_t IMAGE_SIZE = 2 * SPARSE_BLK_SZ + 1000;
    std::vector<uint8_t> image(IMAGE_SIZE);
    std::mt19937 rng(77);
    for (auto& b : image)
        b = static_cast<uint8_t>(rng() % 254 + 1);

    SparseEncoder enc(1024 * 1024, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    REQUIRE(!segments.empty());

    // Decoded image should be 3 full blocks (rounded up)
    auto decoded = decodeSegments(segments);
    REQUIRE(decoded.size() == 3 * SPARSE_BLK_SZ);

    // First IMAGE_SIZE bytes should match
    REQUIRE(std::equal(image.begin(), image.end(), decoded.begin()));

    // Remaining bytes should be zero (padding)
    for (size_t i = IMAGE_SIZE; i < decoded.size(); ++i)
        REQUIRE(decoded[i] == 0);
}

TEST_CASE("Each segment has a valid sparse file header", "[sparse]")
{
    constexpr uint32_t MAX_SEG = SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ + 3 * SPARSE_BLK_SZ;
    constexpr size_t IMAGE_SIZE = 20 * SPARSE_BLK_SZ;

    std::vector<uint8_t> image(IMAGE_SIZE);
    std::mt19937 rng(55);
    for (auto& b : image)
        b = static_cast<uint8_t>(rng() % 254 + 1);

    SparseEncoder enc(MAX_SEG, IMAGE_SIZE);
    auto segments = feedAndCollect(enc, image);

    uint64_t totalBlocks = 0;
    for (const auto& seg : segments) {
        REQUIRE(seg.size() >= sizeof(SparseFileHeader));
        SparseFileHeader fhdr;
        std::memcpy(&fhdr, seg.data(), sizeof(fhdr));

        REQUIRE(fhdr.magic == SPARSE_MAGIC);
        REQUIRE(fhdr.major_version == SPARSE_MAJOR_VER);
        REQUIRE(fhdr.blk_sz == SPARSE_BLK_SZ);
        REQUIRE(fhdr.total_chunks > 0);
        REQUIRE(fhdr.total_blks > 0);
        totalBlocks += fhdr.total_blks;
    }

    REQUIRE(totalBlocks == IMAGE_SIZE / SPARSE_BLK_SZ);
}

TEST_CASE("Empty image produces no segments", "[sparse]")
{
    SparseEncoder enc(1024 * 1024, 0);
    enc.finish();
    auto seg = enc.takeSegment();
    REQUIRE(seg.empty());
}

TEST_CASE("isBlockZero correctly identifies zero blocks", "[sparse]")
{
    alignas(8) uint8_t block[SPARSE_BLK_SZ] = {};
    REQUIRE(isBlockZero(block));

    block[0] = 1;
    REQUIRE_FALSE(isBlockZero(block));

    block[0] = 0;
    block[SPARSE_BLK_SZ - 1] = 1;
    REQUIRE_FALSE(isBlockZero(block));
}

TEST_CASE("isBlockFill correctly identifies fill blocks", "[sparse]")
{
    alignas(8) uint8_t block[SPARSE_BLK_SZ];
    uint32_t pattern = 0x12345678;
    for (size_t i = 0; i < SPARSE_BLK_SZ; i += 4)
        std::memcpy(block + i, &pattern, 4);

    uint32_t fillVal = 0;
    REQUIRE(isBlockFill(block, &fillVal));
    REQUIRE(fillVal == pattern);

    // Break the pattern
    block[SPARSE_BLK_SZ - 1] ^= 0xFF;
    REQUIRE_FALSE(isBlockFill(block, &fillVal));
}

TEST_CASE("Large zero regions produce compact sparse output", "[sparse]")
{
    // 1 GB of zeros should produce a tiny sparse image
    constexpr uint64_t IMAGE_SIZE = 1024ULL * 1024 * 1024;
    constexpr size_t BLOCKS = IMAGE_SIZE / SPARSE_BLK_SZ;

    SparseEncoder enc(256 * 1024 * 1024, IMAGE_SIZE);

    // Feed in 1MB chunks of zeros
    std::vector<uint8_t> zeros(1024 * 1024, 0);
    for (uint64_t off = 0; off < IMAGE_SIZE; off += zeros.size()) {
        const uint8_t* ptr = zeros.data();
        size_t rem = zeros.size();
        while (rem > 0) {
            size_t consumed = enc.feed(ptr, rem);
            ptr += consumed;
            rem -= consumed;
            // Drain any ready segment (shouldn't happen for zeros within 256MB limit)
            enc.takeSegment();
        }
    }
    enc.finish();

    auto seg = enc.takeSegment();
    REQUIRE(!seg.empty());

    // Should be just header + one DONT_CARE chunk
    REQUIRE(seg.size() == SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ);

    REQUIRE(enc.dontCareBlockCount() == BLOCKS);
    REQUIRE(enc.rawBlockCount() == 0);
}

TEST_CASE("Feeding data in odd chunk sizes works correctly", "[sparse]")
{
    constexpr size_t IMAGE_SIZE = 10 * SPARSE_BLK_SZ;
    std::vector<uint8_t> image(IMAGE_SIZE);
    std::mt19937 rng(42);
    for (auto& b : image)
        b = static_cast<uint8_t>(rng() % 254 + 1);

    SparseEncoder enc(1024 * 1024, IMAGE_SIZE);

    // Feed in prime-sized chunks to stress partial-block handling
    std::vector<std::vector<uint8_t>> segments;
    size_t off = 0;
    constexpr size_t PRIME_CHUNK = 997;
    while (off < IMAGE_SIZE) {
        size_t chunk = std::min(PRIME_CHUNK, IMAGE_SIZE - off);
        size_t consumed = enc.feed(image.data() + off, chunk);
        off += consumed;

        auto seg = enc.takeSegment();
        if (!seg.empty())
            segments.emplace_back(seg.begin(), seg.end());
    }
    while (true) {
        enc.finish();
        auto seg = enc.takeSegment();
        if (seg.empty())
            break;
        segments.emplace_back(seg.begin(), seg.end());
    }

    auto decoded = decodeSegments(segments);
    REQUIRE(decoded == image);
}
