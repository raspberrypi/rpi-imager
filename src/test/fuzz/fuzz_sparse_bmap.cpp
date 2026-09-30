// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The sparse encoder driven with a block map, which is how it runs on a
// real write.
//
// setBlockMap() had one caller in the tree and no coverage at all until
// recently, which is a poor place for a gap: the map decides which blocks
// reach the card. A block wrongly called unmapped is a hole in the image,
// and nothing downstream reports one -- the device flashes what it is given
// and says it worked.
//
// The property is the one the ordered cases hold, asked of sequences nobody
// chose: every block the map calls mapped has to come out byte-identical,
// and no block it calls unmapped may be written over. The fuzzer picks the
// ranges, the segment limit and the image content, so the arithmetic that
// adds a block number to a segment offset is exercised at every boundary
// those three can produce between them.
#include "fastboot/bmap.h"
#include "fastboot/sparse_encoder.h"
#include "fuzz_silence.h"
#include "sparse_apply.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace fastboot;

namespace {

// Inclusive ranges from a bitmap of mapped blocks, in the layout the parser
// reads. Built as text and parsed rather than assembled directly: the
// encoder is only ever handed a map that came through parse(), so a map this
// harness invents by hand would be one the product cannot produce.
std::string rangesFor(uint64_t mappedBits, size_t blocks)
{
    std::string out;
    size_t b = 0;
    while (b < blocks) {
        if (!((mappedBits >> b) & 1)) {
            ++b;
            continue;
        }
        size_t last = b;
        while (last + 1 < blocks && ((mappedBits >> (last + 1)) & 1))
            ++last;
        out += "    <Range>" + std::to_string(b);
        if (last != b)
            out += "-" + std::to_string(last);
        out += "</Range>\n";
        b = last + 1;
    }
    return out;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Two bytes of segment choice, eight of mapped-block bitmap, then the
    // image itself.
    if (size < 10 + SPARSE_BLK_SZ)
        return 0;

    const uint32_t segChoice = uint32_t(data[0]) | (uint32_t(data[1]) << 8);
    uint64_t mappedBits = 0;
    std::memcpy(&mappedBits, data + 2, 8);
    data += 10;
    size -= 10;

    const size_t blocks = size / SPARSE_BLK_SZ;
    if (blocks == 0 || blocks > 64)
        return 0;
    const size_t rawLen = blocks * SPARSE_BLK_SZ;
    const std::vector<uint8_t> raw(data, data + rawLen);

    // Small enough to cut a mapped run in half, which is the boundary that
    // matters, and never below the floor the encoder insists on.
    const uint32_t maxSeg = SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ * 3
                          + SPARSE_BLK_SZ * (1 + (segChoice % 8));

    const std::string xml =
        "<?xml version=\"1.0\" ?>\n<bmap version=\"2.0\">\n"
        "  <BlockSize>" + std::to_string(SPARSE_BLK_SZ) + "</BlockSize>\n"
        "  <BlocksCount>" + std::to_string(blocks) + "</BlocksCount>\n"
        "  <MappedBlocksCount>0</MappedBlocksCount>\n"
        "  <BlockMap>\n" + rangesFor(mappedBits, blocks) +
        "  </BlockMap>\n</bmap>\n";

    auto map = std::make_unique<BlockMap>();
    std::string err;
    if (!map->parse(xml, &err))
        return 0;

    // Asked before the map is handed over, because the encoder takes it.
    std::vector<bool> mapped(blocks);
    for (size_t b = 0; b < blocks; ++b)
        mapped[b] = map->isMapped(b);

    SparseEncoder enc(maxSeg, rawLen);
    enc.setBlockMap(std::move(map));

    // DONT_CARE means "leave it alone", so the reconstruction starts as a
    // device that has never been written would: zeroed.
    std::vector<uint8_t> image(rawLen, 0);
    bool ok = true;

    size_t fed = 0;
    while (fed < raw.size() && ok) {
        const size_t took = enc.feed(raw.data() + fed, raw.size() - fed);
        fed += took;
        const std::span<const uint8_t> seg = enc.takeSegment();
        if (!seg.empty())
            ok = fuzzsparse::applySegment(seg, image);
        else if (took == 0)
            break;
    }

    enc.finish();
    while (ok) {
        const std::span<const uint8_t> seg = enc.takeSegment();
        if (seg.empty())
            break;
        ok = fuzzsparse::applySegment(seg, image);
        enc.finish();
    }

    // Anything the encoder emits has to be something this decoder can apply.
    if (!ok)
        __builtin_trap();

    for (size_t b = 0; b < blocks; ++b) {
        const uint8_t *got = image.data() + b * SPARSE_BLK_SZ;
        if (mapped[b]) {
            // A mapped block is one the card must end up holding.
            if (std::memcmp(got, raw.data() + b * SPARSE_BLK_SZ,
                            SPARSE_BLK_SZ) != 0)
                __builtin_trap();
        } else {
            // An unmapped one must be left exactly as it was found. Writing
            // the right bytes to the wrong block shows up here.
            for (size_t i = 0; i < SPARSE_BLK_SZ; ++i)
                if (got[i] != 0)
                    __builtin_trap();
        }
    }

    return 0;
}
