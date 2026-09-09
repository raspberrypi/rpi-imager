/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Reading an Android sparse image back into the bytes it stands for.
 *
 * Shared by the encoder's own tests and by the fastboot flash tests, which
 * reassemble what the simulated device received and compare it with the image
 * that went in. Two copies of a decoder would be two things to keep honest.
 */

#ifndef RPI_IMAGER_TEST_SPARSE_DECODE_H
#define RPI_IMAGER_TEST_SPARSE_DECODE_H

#include <catch2/catch_test_macros.hpp>

#include "fastboot/sparse_encoder.h"

#include <cstring>
#include <span>
#include <vector>

namespace rpi_test {

using namespace fastboot;

// Parse a sparse image and return the decoded raw image
inline std::vector<uint8_t> decodeSparse(std::span<const uint8_t> sparse)
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

// Apply a sparse image onto an existing buffer (RAW/FILL overwrite, DONT_CARE skips)
inline void applySparse(std::span<const uint8_t> sparse, std::vector<uint8_t>& out)
{
    REQUIRE(sparse.size() >= sizeof(SparseFileHeader));
    SparseFileHeader fhdr;
    std::memcpy(&fhdr, sparse.data(), sizeof(fhdr));
    REQUIRE(fhdr.magic == SPARSE_MAGIC);
    REQUIRE(out.size() >= static_cast<size_t>(fhdr.total_blks) * SPARSE_BLK_SZ);

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
            // Leave existing content untouched (skip)
            break;

        default:
            FAIL("Unknown chunk type: " << chdr.chunk_type);
        }

        outOff += blockBytes;
    }
}

} // namespace rpi_test

#endif // RPI_IMAGER_TEST_SPARSE_DECODE_H
