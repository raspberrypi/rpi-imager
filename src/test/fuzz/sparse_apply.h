// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// A sparse decoder for the fuzz harnesses, written from the format
// description rather than reused from src/test/sparse_decode.h. Sharing that
// one would mean sharing its assumptions, and the point of these targets is
// to check the encoder against something that does not hold them.
//
// Shared between the harnesses rather than copied: two decoders drifting
// apart would be two different questions being asked, and only one of them
// would be the one written down.

#ifndef RPI_IMAGER_FUZZ_SPARSE_APPLY_H
#define RPI_IMAGER_FUZZ_SPARSE_APPLY_H

#include "fastboot/sparse_encoder.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace fuzzsparse {
using namespace fastboot;

// Apply one segment into `image` at the offsets it names. Segments are not
// a stream to be concatenated: each is self-positioning, and a DONT_CARE
// chunk advances the write cursor rather than contributing zeros -- that is
// how a continuation segment skips what earlier ones already wrote.
inline bool applySegment(std::span<const uint8_t> seg,
                         std::vector<uint8_t> &image)
{
    if (seg.size() < SPARSE_FILE_HDR_SZ)
        return false;

    SparseFileHeader fh{};
    std::memcpy(&fh, seg.data(), sizeof(fh));
    if (fh.magic != SPARSE_MAGIC || fh.blk_sz == 0)
        return false;

    size_t pos = fh.file_hdr_sz;
    size_t cursor = 0;                     // byte offset into the image
    for (uint32_t c = 0; c < fh.total_chunks; ++c) {
        if (pos + SPARSE_CHUNK_HDR_SZ > seg.size())
            return false;
        SparseChunkHeader ch{};
        std::memcpy(&ch, seg.data() + pos, sizeof(ch));
        const size_t payload = pos + fh.chunk_hdr_sz;
        const size_t bytes = size_t(ch.chunk_sz) * fh.blk_sz;

        if (ch.chunk_type == CHUNK_TYPE_RAW) {
            if (payload + bytes > seg.size() || cursor + bytes > image.size())
                return false;
            std::memcpy(image.data() + cursor, seg.data() + payload, bytes);
            cursor += bytes;
        } else if (ch.chunk_type == CHUNK_TYPE_FILL) {
            if (payload + 4 > seg.size() || cursor + bytes > image.size())
                return false;
            uint8_t fill[4];
            std::memcpy(fill, seg.data() + payload, 4);
            for (size_t i = 0; i < bytes; ++i)
                image[cursor + i] = fill[i % 4];
            cursor += bytes;
        } else if (ch.chunk_type == CHUNK_TYPE_DONT_CARE) {
            if (cursor + bytes > image.size())
                return false;
            cursor += bytes;           // seek, do not write
        } else {
            return false;
        }
        if (ch.total_sz < fh.chunk_hdr_sz)
            return false;
        pos += ch.total_sz;
    }
    return true;
}

} // namespace fuzzsparse

#endif // RPI_IMAGER_FUZZ_SPARSE_APPLY_H
