/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A bmap tells the flasher which blocks of an image actually contain data,
 * so the rest can be skipped. That makes it a correctness input, not just an
 * optimisation: a block wrongly reported as unmapped is never written, and
 * the card ends up with a hole in the middle of the filesystem. The write
 * reports success, and the damage only shows up when the board fails to boot
 * or a file reads back as zeroes.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "fastboot/bmap.h"

#include <cstring>
#include <string>
#include <vector>

using namespace fastboot;
using Catch::Matchers::ContainsSubstring;

namespace {

// A bmap document in the layout bmap-tools emits.
std::string bmapDoc(const std::string &ranges,
                    const std::string &blockSize = "4096",
                    const std::string &blocksCount = "100",
                    const std::string &mapped = "0")
{
    return
        "<?xml version=\"1.0\" ?>\n"
        "<bmap version=\"2.0\">\n"
        "  <BlockSize>" + blockSize + "</BlockSize>\n"
        "  <BlocksCount>" + blocksCount + "</BlocksCount>\n"
        "  <MappedBlocksCount>" + mapped + "</MappedBlocksCount>\n"
        "  <BlockMap>\n" + ranges +
        "  </BlockMap>\n"
        "</bmap>\n";
}

uint32_t le32(const std::vector<uint8_t> &b, size_t off)
{
    return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8)
         | (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24);
}

} // namespace

// ══════════════════════════════════════════════════════════════
// Parsing
// ══════════════════════════════════════════════════════════════

TEST_CASE("A bmap document parses into ranges", "[bmap]")
{
    BlockMap map;
    std::string err;
    REQUIRE(map.parse(bmapDoc("    <Range>0-9</Range>\n"
                              "    <Range>20-29</Range>\n", "4096", "100", "20"), &err));
    INFO("error: " << err);

    CHECK(map.blockSize() == 4096);
    CHECK(map.blockCount() == 100);
    CHECK(map.mappedBlockCount() == 20);
    REQUIRE(map.ranges().size() == 2);

    // "0-9" is inclusive on the wire and half-open in memory.
    CHECK(map.ranges()[0].begin == 0);
    CHECK(map.ranges()[0].end == 10);
    CHECK(map.ranges()[1].begin == 20);
    CHECK(map.ranges()[1].end == 30);
}

TEST_CASE("A single-block range covers exactly one block", "[bmap]")
{
    // bmap writes a lone block as "42", not "42-42". Treating that as an
    // empty range would drop a block that has data in it.
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>42</Range>\n")));

    REQUIRE(map.ranges().size() == 1);
    CHECK(map.ranges()[0].begin == 42);
    CHECK(map.ranges()[0].end == 43);
    CHECK(map.isMapped(42));
    CHECK_FALSE(map.isMapped(41));
    CHECK_FALSE(map.isMapped(43));
}

TEST_CASE("Ranges come back in order however they were written", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>50-59</Range>\n"
                              "    <Range>0-9</Range>\n"
                              "    <Range>20-29</Range>\n")));

    REQUIRE(map.ranges().size() == 3);
    CHECK(map.ranges()[0].begin == 0);
    CHECK(map.ranges()[1].begin == 20);
    CHECK(map.ranges()[2].begin == 50);
}

TEST_CASE("Range checksums are read when present", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc(
        "    <Range chksum=\"" + std::string(64, 'a') + "\">0-9</Range>\n")));

    CHECK(map.hasChecksums());
    REQUIRE(map.ranges().size() == 1);
    CHECK(map.ranges()[0].hasSha256);
    CHECK(map.ranges()[0].sha256[0] == 0xaa);
}

TEST_CASE("A bmap with no checksums says so", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>0-9</Range>\n")));
    CHECK_FALSE(map.hasChecksums());
    CHECK_FALSE(map.ranges()[0].hasSha256);
}

TEST_CASE("A malformed checksum is ignored rather than half-read", "[bmap]")
{
    // A truncated digest must not leave a partly-filled buffer that later
    // gets compared against real data and fails for the wrong reason.
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range chksum=\"not-hex\">0-9</Range>\n")));
    CHECK_FALSE(map.hasChecksums());
    CHECK_FALSE(map.ranges()[0].hasSha256);
}

TEST_CASE("A truncated bmap is rejected, not half-accepted", "[bmap]")
{
    // The bmap is fetched over the network, so a cut-short response is a
    // real possibility. Accepting one would hand back the ranges that
    // happened to arrive, and every block past the truncation point would
    // look unmapped -- written as DONT_CARE, leaving holes in the card with
    // no error anywhere. Failing here instead costs only the optimisation:
    // the caller logs it and writes every block.
    BlockMap map;
    std::string err;
    CHECK_FALSE(map.parse("<bmap><BlockMap><Range>0-9</Range>", &err));
    CHECK_FALSE(err.empty());
    // Nothing half-parsed is left behind for a caller that ignores the false.
    CHECK(map.empty());
}

TEST_CASE("A bmap cut off mid-range is rejected", "[bmap]")
{
    BlockMap map;
    const std::string full = bmapDoc("    <Range>0-9</Range>\n"
                                     "    <Range>20-29</Range>\n");
    CHECK_FALSE(map.parse(full.substr(0, full.size() / 2)));
    CHECK(map.empty());
}

TEST_CASE("A document that is not bmap at all yields no ranges", "[bmap]")
{
    BlockMap map;
    CHECK(map.parse("<?xml version=\"1.0\" ?><something-else/>"));
    CHECK(map.empty());
}

TEST_CASE("An empty bmap has no mapped blocks", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("")));
    CHECK(map.empty());
    CHECK_FALSE(map.isMapped(0));
}

// ══════════════════════════════════════════════════════════════
// Lookup
// ══════════════════════════════════════════════════════════════

TEST_CASE("Mapped lookup honours range boundaries", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>10-19</Range>\n")));

    CHECK_FALSE(map.isMapped(9));   // just before
    CHECK(map.isMapped(10));        // first
    CHECK(map.isMapped(19));        // last, inclusive on the wire
    CHECK_FALSE(map.isMapped(20));  // just after
}

TEST_CASE("The sequential cursor agrees with random access on every block",
          "[bmap]")
{
    // The whole point of the cursor is to avoid a search per block while
    // streaming. If it ever disagrees with isMapped(), the encoder skips a
    // block that holds data and the card is written with a hole in it.
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>0-9</Range>\n"
                              "    <Range>20-29</Range>\n"
                              "    <Range>31</Range>\n"
                              "    <Range>90-99</Range>\n",
                              "4096", "100", "31")));

    for (uint64_t i = 0; i < 110; ++i) {
        INFO("block " << i);
        CHECK(map.isMappedSequential(i) == map.isMapped(i));
    }
}

TEST_CASE("The sequential cursor survives going backwards", "[bmap]")
{
    // A retry or a rewind must not leave the cursor stranded past the block
    // being asked about, silently answering "unmapped" from then on.
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>0-9</Range>\n"
                              "    <Range>20-29</Range>\n")));

    CHECK(map.isMappedSequential(25));
    CHECK(map.isMappedSequential(5));     // back into the first range
    CHECK_FALSE(map.isMappedSequential(15));
    CHECK(map.isMappedSequential(22));
    CHECK(map.isMappedSequential(0));
}

TEST_CASE("The sequential cursor handles repeated and skipped blocks", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>5-9</Range>\n"
                              "    <Range>50-59</Range>\n")));

    CHECK(map.isMappedSequential(5));
    CHECK(map.isMappedSequential(5));     // same block twice
    CHECK_FALSE(map.isMappedSequential(30));
    CHECK(map.isMappedSequential(55));    // big jump forward
    CHECK_FALSE(map.isMappedSequential(1000));
}

// ══════════════════════════════════════════════════════════════
// Wire format
// ══════════════════════════════════════════════════════════════

TEST_CASE("A range that cannot be read is refused, not read as block zero",
          "[bmap]")
{
    // toULongLong() answers zero for anything it cannot read, and the two
    // numbers were taken without asking whether it had. A damaged begin
    // became block 0; a damaged end became one, leaving begin past end --
    // and a range like that is stepped over by both lookups, so the encoder
    // is told those blocks are unmapped and writes nothing where their data
    // should have gone. The card comes back with a hole in it and nothing
    // reports a problem, which is the same ending the truncation check
    // upstream exists to prevent.
    const auto refuses = [](const char *rangeText) {
        fastboot::BlockMap map;
        std::string err;
        const std::string xml =
            std::string("<?xml version=\"1.0\" ?>\n<bmap version=\"2.0\">\n"
                        "<BlockSize>4096</BlockSize>\n<BlocksCount>200</BlocksCount>\n"
                        "<BlockMap>\n<Range>") + rangeText
            + "</Range>\n</BlockMap>\n</bmap>\n";
        INFO("range: " << rangeText);
        const bool ok = map.parse(xml, &err);
        CHECK_FALSE(ok);
        CHECK_FALSE(err.empty());
        CHECK(map.ranges().empty());
    };

    refuses("not-a-number");
    refuses("10-not-a-number");
    refuses("not-a-number-20");
    refuses("");
    refuses("20-10");          // end before begin
    refuses("10-9");           // the inclusive end one below the begin
}

TEST_CASE("Overlapping ranges are refused", "[bmap]")
{
    // isMapped() binary searches for the last range beginning at or before
    // the block and asks only that one; isMappedSequential() walks to the
    // first whose end is past it. Given two ranges covering the same block
    // they answer differently -- which a fuzz run found by asking both.
    fastboot::BlockMap map;
    std::string err;
    const std::string xml =
        "<?xml version=\"1.0\" ?>\n<bmap version=\"2.0\">\n"
        "<BlockSize>4096</BlockSize>\n<BlocksCount>200</BlocksCount>\n"
        "<BlockMap>\n<Range>0-59</Range>\n<Range>0-9</Range>\n"
        "</BlockMap>\n</bmap>\n";

    CHECK_FALSE(map.parse(xml, &err));
    CHECK(err.find("overlap") != std::string::npos);
    CHECK(map.ranges().empty());
}

TEST_CASE("The two ways of asking agree on every block", "[bmap]")
{
    // One is used by the encoder and one is not, but they answer the same
    // question and a public API that answers it two ways has to answer it
    // once. Out-of-order input used to make them differ.
    fastboot::BlockMap map;
    std::string err;
    const std::string xml =
        "<?xml version=\"1.0\" ?>\n<bmap version=\"2.0\">\n"
        "<BlockSize>4096</BlockSize>\n<BlocksCount>200</BlocksCount>\n"
        "<BlockMap>\n<Range>50-59</Range>\n<Range>0-9</Range>\n"
        "<Range>20-29</Range>\n</BlockMap>\n</bmap>\n";

    REQUIRE(map.parse(xml, &err));
    for (uint64_t i = 0; i < 200; ++i) {
        INFO("block " << i);
        CHECK(map.isMappedSequential(i) == map.isMapped(i));
    }
}

TEST_CASE("Serialisation produces the header fastbootd expects", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>0-9</Range>\n"
                              "    <Range>20-29</Range>\n")));

    const auto wire = map.serialize();
    REQUIRE(wire.size() >= 16);

    CHECK(le32(wire, 0) == BMAP_WIRE_MAGIC);
    CHECK(le32(wire, 4) == 4096);
    CHECK(le32(wire, 8) == 2);       // range count
    CHECK(le32(wire, 12) == 0);      // reserved

    // Header plus one 40-byte entry per range.
    CHECK(wire.size() == sizeof(BmapWireHeader) + 2 * sizeof(BmapWireRange));
}

TEST_CASE("Serialised ranges keep the inclusive end the device expects", "[bmap]")
{
    // In memory the range is half-open; on the wire end_block is inclusive.
    // Off by one here writes one block too few at the end of every range.
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("    <Range>10-19</Range>\n")));

    const auto wire = map.serialize();
    REQUIRE(wire.size() == sizeof(BmapWireHeader) + sizeof(BmapWireRange));

    const size_t off = sizeof(BmapWireHeader);
    CHECK(le32(wire, off + 0) == 10);   // start, inclusive
    CHECK(le32(wire, off + 4) == 19);   // end, inclusive
}

TEST_CASE("An empty bmap serialises to a header with no ranges", "[bmap]")
{
    BlockMap map;
    REQUIRE(map.parse(bmapDoc("")));

    const auto wire = map.serialize();
    REQUIRE(wire.size() == sizeof(BmapWireHeader));
    CHECK(le32(wire, 0) == BMAP_WIRE_MAGIC);
    CHECK(le32(wire, 8) == 0);
}

// ---------------------------------------------------------------------------
// Figures the file chose
// ---------------------------------------------------------------------------

TEST_CASE("A BlockSize that will not read is refused", "[bmap]")
{
    // toULongLong() answers zero for anything it cannot read, and zero is
    // the one value that cannot be a block size: every block number becomes
    // byte offset zero, so the whole map points at the start of the card.
    fastboot::BlockMap map;
    std::string err;
    CHECK_FALSE(map.parse(bmapDoc("<Range>0-3</Range>", "not-a-number"), &err));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("BlockSize"));

    std::string err2;
    fastboot::BlockMap zeroed;
    CHECK_FALSE(zeroed.parse(bmapDoc("<Range>0-3</Range>", "0"), &err2));
}

TEST_CASE("A BlocksCount that will not read is refused", "[bmap]")
{
    fastboot::BlockMap map;
    std::string err;
    CHECK_FALSE(map.parse(bmapDoc("<Range>0-3</Range>", "4096", "twelve"), &err));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("BlocksCount"));
}

TEST_CASE("The figures are read from the layout bmap-tools really writes", "[bmap]")
{
    // bmaptool pads the values inside their elements:
    //
    //     <BlockSize> 4096 </BlockSize>
    //
    // The ranges were already trimmed before being read, which looks like
    // somebody meeting this -- but the counts above them never were, and
    // they were right not to be: QString::toULongLong() skips the padding
    // on its own. Pinned here because the parser now relies on that, and
    // a bmap whose block size read as nought would point the whole map at
    // the start of the card.
    fastboot::BlockMap map;
    std::string err;
    REQUIRE(map.parse(bmapDoc(" <Range> 0-3 </Range>\n",
                              " 4096 ", " 100 ", " 4 "), &err));
    CHECK(map.blockSize() == 4096);
    CHECK(map.blockCount() == 100);
    CHECK(map.mappedBlockCount() == 4);
    CHECK(map.isMapped(0));
    CHECK(map.isMapped(3));
}

TEST_CASE("A block past what the device's format holds is refused", "[bmap]")
{
    // The wire format has 32-bit block numbers and serialize() wrote them
    // with a plain cast, so 4294967296 came out as block 0: the device was
    // told to write that range's data at the start of the card, and to
    // leave the blocks it names alone. Nothing said so.
    fastboot::BlockMap map;
    std::string err;
    CHECK_FALSE(map.parse(
        bmapDoc("<Range>4294967296-4294967300</Range>", "4096", "5000000000"), &err));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("device"));

    // The last block the format can carry is still accepted.
    fastboot::BlockMap edge;
    std::string err2;
    REQUIRE(edge.parse(
        bmapDoc("<Range>4294967294-4294967295</Range>", "4096", "5000000000"), &err2));
    CHECK(edge.isMapped(4294967295ULL));
}

TEST_CASE("A BlockSize past the wire format is refused", "[bmap]")
{
    fastboot::BlockMap map;
    std::string err;
    CHECK_FALSE(map.parse(bmapDoc("<Range>0-3</Range>", "4294967296"), &err));
}

