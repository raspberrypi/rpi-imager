// Round-trip the Android sparse encoder: encode an arbitrary raw image,
// decode it, and require the original bytes back.
//
// Unlike the other targets this is not hunting a crash. The encoder's output
// goes to a device that flashes it, so an image that encodes *wrongly* --
// a fill run mis-counted, a segment boundary landing mid-block -- is worse
// than one that fails to encode. The fuzzer picks the raw content, the
// segment limit and the image size, which is where the arithmetic lives.
#include "fastboot/sparse_encoder.h"
#include "sparse_apply.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

// A decoder written from the format description rather than reused from the
// tests: sharing one would mean sharing its assumptions, and the point here
// is to check the encoder against something that does not.

using namespace fastboot;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // First bytes choose the geometry; the rest is the raw image.
    if (size < 8)
        return 0;
    const uint32_t segChoice = uint32_t(data[0]) | (uint32_t(data[1]) << 8);
    data += 2; size -= 2;

    // A segment has to hold the file header plus at least one block.
    uint32_t maxSeg = SPARSE_FILE_HDR_SZ + SPARSE_CHUNK_HDR_SZ
                          + SPARSE_BLK_SZ * (1 + (segChoice % 8));
    if (const char *fixed = ::getenv("FUZZ_SPARSE_MAXSEG"))
        maxSeg = uint32_t(std::atoi(fixed));

    // Whole blocks only: that is the contract the encoder is given.
    const size_t blocks = size / SPARSE_BLK_SZ;
    if (blocks == 0 || blocks > 64)
        return 0;
    const size_t rawLen = blocks * SPARSE_BLK_SZ;
    std::vector<uint8_t> raw(data, data + rawLen);

    SparseEncoder enc(maxSeg, rawLen);

    // DONT_CARE means "unchanged", so the reconstruction starts as the
    // device would: zeroed. A block the encoder skips must therefore have
    // been zero in the input for the round trip to hold.
    std::vector<uint8_t> image(rawLen, 0);
    bool ok = true;

    // Take after every feed, not only when bytes were left over. Only one
    // segment is ever ready at a time -- takeSegment() does not advance --
    // so a segment completed by a feed that consumed everything is still
    // waiting, and beginning the next one overwrites it.
    size_t fed = 0;
    while (fed < raw.size() && ok) {
        const size_t remaining = raw.size() - fed;
        const size_t took = enc.feed(raw.data() + fed, remaining);
        fed += took;
        const std::span<const uint8_t> seg = enc.takeSegment();
        if (!seg.empty())
            ok = fuzzsparse::applySegment(seg, image);
        else if (took == 0)
            break;                     // no progress and nothing to collect
    }

    enc.finish();
    while (ok) {
        const std::span<const uint8_t> seg = enc.takeSegment();
        if (seg.empty())
            break;
        ok = fuzzsparse::applySegment(seg, image);
        enc.finish();
    }

    // A segment the encoder produced must be one this decoder can apply,
    // and what went in must come out.
    if (!ok)
        __builtin_trap();
    if (std::memcmp(image.data(), raw.data(), raw.size()) != 0)
        __builtin_trap();

    // And the counters, which no harness had read. Every block the encoder
    // processed is classified as exactly one of raw, fill or don't-care --
    // processBlock() is a strict if/else-if/else and bumps the total once at
    // the end of the same call -- so the three have to sum to it. A
    // disagreement means a block counted twice or lost, which on a card is a
    // range written twice or a range not written at all, and nothing
    // downstream would say so.
    const uint64_t classified = enc.rawBlockCount() + enc.fillBlockCount()
                              + enc.dontCareBlockCount();
    if (classified != enc.totalBlocksProcessed())
        __builtin_trap();          // a block classified twice, or not at all
    return 0;
}
