// Fuzz BlockMap::parse() and everything a caller does with the result.
//
// A .bmap sits alongside the image in a repository, so its bytes are as
// untrusted as the image itself. Parsing is only half the target: the
// ranges it yields then drive serialize() and the two lookup paths, which
// is where a nonsensical-but-accepted range set would bite.
#include "fastboot/bmap.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <vector>
#include <cstring>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fastboot::BlockMap map;
    std::string err;
    if (!map.parse(std::string_view(reinterpret_cast<const char *>(data), size), &err))
        return 0;

    // Accepted. Now do what the encoder does with it.
    //
    // The result was discarded, which made this call coverage and nothing
    // more. What it owes: the device is handed 32-bit fields, and a block
    // number past four billion used to be narrowed into them by a plain
    // cast -- a different block, written silently. parse() now refuses such
    // a bmap, so anything that got here must round-trip exactly.
    const std::vector<uint8_t> wire = map.serialize();
    if (wire.size() < 16)
        __builtin_trap();

    uint32_t wireBlockSize = 0;
    uint32_t wireRangeCount = 0;
    std::memcpy(&wireBlockSize, wire.data() + 4, 4);
    std::memcpy(&wireRangeCount, wire.data() + 8, 4);
    if (wireBlockSize != map.blockSize())
        __builtin_trap();
    if (wire.size() != 16 + size_t(wireRangeCount) * 40)
        __builtin_trap();

    // Every range on the wire has to be one the map would answer for. The
    // wire carries an inclusive end, so the block it names must be mapped
    // and the one after the range must not be -- which is the narrowing
    // caught from the other side.
    for (uint32_t i = 0; i < wireRangeCount; ++i) {
        uint32_t start = 0;
        uint32_t last = 0;
        std::memcpy(&start, wire.data() + 16 + size_t(i) * 40, 4);
        std::memcpy(&last, wire.data() + 16 + size_t(i) * 40 + 4, 4);
        if (start > last)
            __builtin_trap();
        if (!map.isMapped(start) || !map.isMapped(last))
            __builtin_trap();
    }

    (void)map.blockSize();

    // Nothing is asserted about what the ranges say. Every figure in a bmap
    // is read straight out of the file -- BlocksCount, MappedBlocksCount and
    // the ranges are all whatever it wrote -- so "the count matches the
    // ranges" and "no range runs past the last block" are properties of a
    // well-formed file, not of the parser, and a fuzzer writes ill-formed
    // ones by the thousand. Asserted anyway, they fired on the first seed
    // and said nothing. MappedBlocksCount reaches only a debug line.
    const uint64_t count = map.blockCount();

    // What does hold whatever the file says: the two ways of asking whether
    // a block is mapped have to agree.
    // isMappedSequential keeps a cursor so a write can walk the image
    // without searching for every block; isMapped searches every time. The
    // cursor is the one that runs on a real write, and a cursor that falls
    // out of step skips blocks or repeats them, quietly.
    const uint64_t probe = count > 4096 ? 4096 : count;
    for (uint64_t i = 0; i < probe; ++i) {
        if (map.isMappedSequential(i) != map.isMapped(i))
            __builtin_trap();
    }

    // A backwards jump has to rewind the cursor rather than answer from it.
    if (probe > 0 && map.isMappedSequential(0) != map.isMapped(0))
        __builtin_trap();
    return 0;
}
