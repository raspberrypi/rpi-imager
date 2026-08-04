/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Lime Technology, Inc.
 *
 * Tests for BlockBatcher, which coalesces libarchive's data blocks into larger
 * writes during multi-file extraction.
 *
 * The risk being covered is silent corruption: batching must never change which
 * bytes land at which offset. Sparse files are the dangerous case -- a gap in
 * offsets means a hole, and merging across it would shift every following byte.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "block_batcher.h"

using rpi_imager::BlockBatcher;

namespace {

struct Write {
  std::int64_t offset;
  std::string data;
};

// Collects the writes a batcher emits, and can replay them into a flat image so
// a test can assert on the bytes that would actually reach the device.
class Recorder {
 public:
  BlockBatcher::WriteFn fn() {
    return [this](const void* data, std::size_t size, std::int64_t offset) {
      writes.push_back({offset, std::string(static_cast<const char*>(data), size)});
      return result;
    };
  }

  // Replays the writes at their recorded offsets. Gaps read back as '\0', which
  // is what a hole in a sparse file looks like.
  std::string replay() const {
    std::size_t end = 0;
    for (const auto& w : writes) {
      end = std::max(end, static_cast<std::size_t>(w.offset) + w.data.size());
    }
    std::string image(end, '\0');
    for (const auto& w : writes) {
      image.replace(static_cast<std::size_t>(w.offset), w.data.size(), w.data);
    }
    return image;
  }

  std::vector<Write> writes;
  int result = 0;  // set non-zero to simulate a write failure
};

std::string repeated(char c, std::size_t n) { return std::string(n, c); }

}  // namespace

TEST_CASE("contiguous blocks are merged into one write", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(64, rec.fn());

  REQUIRE(b.Add("aaaa", 4, 0) == 0);
  REQUIRE(b.Add("bbbb", 4, 4) == 0);
  REQUIRE(b.Add("cccc", 4, 8) == 0);
  // Nothing should have been emitted yet -- that is the whole point.
  REQUIRE(rec.writes.empty());

  REQUIRE(b.Flush() == 0);
  REQUIRE(rec.writes.size() == 1);
  REQUIRE(rec.writes[0].offset == 0);
  REQUIRE(rec.writes[0].data == "aaaabbbbcccc");
}

TEST_CASE("a gap in offsets is preserved rather than closed up", "[blockbatcher][sparse]") {
  Recorder rec;
  BlockBatcher b(1024, rec.fn());

  REQUIRE(b.Add("head", 4, 0) == 0);
  // Jump past a hole. Merging here would move "tail" to offset 4.
  REQUIRE(b.Add("tail", 4, 100) == 0);
  REQUIRE(b.Flush() == 0);

  REQUIRE(rec.writes.size() == 2);
  REQUIRE(rec.writes[0].offset == 0);
  REQUIRE(rec.writes[0].data == "head");
  REQUIRE(rec.writes[1].offset == 100);
  REQUIRE(rec.writes[1].data == "tail");

  const std::string image = rec.replay();
  REQUIRE(image.size() == 104);
  REQUIRE(image.substr(0, 4) == "head");
  REQUIRE(image.substr(100, 4) == "tail");
  // The hole stays a hole.
  REQUIRE(image.substr(4, 96) == std::string(96, '\0'));
}

TEST_CASE("a backwards offset also forces a flush", "[blockbatcher][sparse]") {
  Recorder rec;
  BlockBatcher b(1024, rec.fn());

  REQUIRE(b.Add("second", 6, 50) == 0);
  REQUIRE(b.Add("first", 5, 0) == 0);
  REQUIRE(b.Flush() == 0);

  REQUIRE(rec.writes.size() == 2);
  REQUIRE(rec.writes[0].offset == 50);
  REQUIRE(rec.writes[1].offset == 0);
  REQUIRE(rec.replay().substr(0, 5) == "first");
  REQUIRE(rec.replay().substr(50, 6) == "second");
}

TEST_CASE("blocks at or above capacity bypass the buffer", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(16, rec.fn());

  REQUIRE(b.Add("pending", 7, 0) == 0);
  const std::string big = repeated('X', 16);
  REQUIRE(b.Add(big.data(), big.size(), 7) == 0);

  // The pending run must come out first, in order, so offsets stay ascending.
  REQUIRE(rec.writes.size() == 2);
  REQUIRE(rec.writes[0].offset == 0);
  REQUIRE(rec.writes[0].data == "pending");
  REQUIRE(rec.writes[1].offset == 7);
  REQUIRE(rec.writes[1].data == big);
  REQUIRE(b.buffered() == 0);
}

TEST_CASE("filling capacity exactly does not split or lose data", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(8, rec.fn());

  REQUIRE(b.Add("aaaa", 4, 0) == 0);
  REQUIRE(b.Add("bbbb", 4, 4) == 0);
  REQUIRE(rec.writes.empty());       // exactly full, still buffered
  REQUIRE(b.buffered() == 8);

  REQUIRE(b.Add("cccc", 4, 8) == 0); // one more byte forces the flush
  REQUIRE(rec.writes.size() == 1);
  REQUIRE(rec.writes[0].data == "aaaabbbb");

  REQUIRE(b.Flush() == 0);
  REQUIRE(rec.writes.size() == 2);
  REQUIRE(rec.writes[1].offset == 8);
  REQUIRE(rec.writes[1].data == "cccc");
}

TEST_CASE("a long contiguous stream reassembles byte for byte", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(1000, rec.fn());

  // Deliberately awkward block sizes so runs straddle the buffer boundary.
  std::string expected;
  std::int64_t offset = 0;
  const std::size_t sizes[] = {1, 7, 64, 333, 999, 2, 500, 128, 4096, 17};
  for (int round = 0; round < 5; ++round) {
    for (std::size_t size : sizes) {
      std::string chunk(size, static_cast<char>('A' + ((round + size) % 26)));
      REQUIRE(b.Add(chunk.data(), chunk.size(), offset) == 0);
      expected += chunk;
      offset += static_cast<std::int64_t>(size);
    }
  }
  REQUIRE(b.Flush() == 0);

  REQUIRE(rec.replay() == expected);
  // Batching must actually reduce the number of writes, or it is pointless.
  REQUIRE(rec.writes.size() < 50);
}

TEST_CASE("zero-sized blocks are ignored and do not break the run", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(64, rec.fn());

  REQUIRE(b.Add("aa", 2, 0) == 0);
  REQUIRE(b.Add("", 0, 999) == 0);   // bogus offset, but no data
  REQUIRE(b.Add("bb", 2, 2) == 0);   // must still be seen as contiguous
  REQUIRE(b.Flush() == 0);

  REQUIRE(rec.writes.size() == 1);
  REQUIRE(rec.writes[0].offset == 0);
  REQUIRE(rec.writes[0].data == "aabb");
}

TEST_CASE("flushing an empty batcher writes nothing", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(64, rec.fn());

  REQUIRE(b.Flush() == 0);
  REQUIRE(b.Flush() == 0);
  REQUIRE(rec.writes.empty());
}

TEST_CASE("a write failure is reported and not retried", "[blockbatcher]") {
  Recorder rec;
  rec.result = -30;  // e.g. ARCHIVE_FATAL
  BlockBatcher b(8, rec.fn());

  REQUIRE(b.Add("aaaa", 4, 0) == 0);
  REQUIRE(b.Flush() == -30);
  REQUIRE(rec.writes.size() == 1);

  // The failed bytes must not still be queued -- flushing again would write
  // them a second time if they were.
  REQUIRE(b.buffered() == 0);
  rec.result = 0;
  REQUIRE(b.Flush() == 0);
  REQUIRE(rec.writes.size() == 1);
}

TEST_CASE("a failure while making room propagates instead of dropping data", "[blockbatcher]") {
  Recorder rec;
  BlockBatcher b(8, rec.fn());

  // Fill to capacity in pieces. A single 8-byte block would take the
  // at-or-above-capacity bypass and never be buffered at all.
  REQUIRE(b.Add("aaaa", 4, 0) == 0);
  REQUIRE(b.Add("bbbb", 4, 4) == 0);
  REQUIRE(b.buffered() == 8);

  rec.result = -25;
  // This Add must flush to make room; that flush fails and the error surfaces
  // rather than the new block silently replacing the unwritten data.
  REQUIRE(b.Add("cccc", 4, 8) == -25);
}

TEST_CASE("a failure on the sparse-gap flush propagates", "[blockbatcher][sparse]") {
  Recorder rec;
  BlockBatcher b(64, rec.fn());

  REQUIRE(b.Add("head", 4, 0) == 0);
  rec.result = -25;
  // The gap forces a flush before the new offset is accepted; if that write
  // fails the caller must hear about it.
  REQUIRE(b.Add("tail", 4, 100) == -25);
}
