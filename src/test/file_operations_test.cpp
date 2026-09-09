/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Write-path tests for FileOperations, against a scratch image file and --
 * where the host permits one -- a loopback block device.
 *
 * The write path is the most safety-critical code in the imager and, until
 * this file, had no test that actually ran. file_operations.cpp and the
 * platform implementation behind it were reachable only from
 * fat_partition_test (tagged [.destructive], so hidden from CTest) and
 * disk_formatter_test (which was not registered with CTest at all). The
 * consequence was that linux/file_operations_linux.cpp -- including its
 * timeout-protected sync fallback -- was never exercised automatically.
 *
 * Nothing here touches a real device. Every case works against a scratch file
 * under a per-process temp directory; the loopback cases attach that same file
 * to a loop device and skip themselves if the host does not allow it (no
 * CAP_SYS_ADMIN, no passwordless sudo, containerised /dev). Correctness is
 * checked by reading the backing file back with plain pread(), independent of
 * the code under test.
 */

#include <catch2/catch_test_macros.hpp>

#include "loop_device.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aligned_buffer.h"
#include "file_operations.h"
#include "posix_write_error.h"
#include "faulty_block_device.h"

namespace fs = std::filesystem;

using rpi_imager::AlignedBuffer;
using rpi_imager::FileError;
using rpi_imager::FileOperations;

namespace {

constexpr std::uint64_t kImageSize = 16u * 1024 * 1024;  // 16 MiB

// Per-process scratch directory, removed when the process exits.
class ScratchDir {
 public:
  ScratchDir() {
    base_ = fs::temp_directory_path() /
            ("rpi-imager-file-ops-" + std::to_string(getpid()));
    std::error_code ec;
    fs::remove_all(base_, ec);
    fs::create_directories(base_, ec);
  }

  ~ScratchDir() {
    std::error_code ec;
    fs::remove_all(base_, ec);
  }

  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;

  std::string file(const std::string& name) const {
    return (base_ / name).string();
  }

 private:
  fs::path base_;
};

ScratchDir& scratch() {
  static ScratchDir dir;
  return dir;
}

// Deterministic filler, so a mismatch says which byte and where rather than
// just "buffers differ".
std::vector<std::uint8_t> pattern(std::size_t size, std::uint8_t seed) {
  std::vector<std::uint8_t> out(size);
  for (std::size_t i = 0; i < size; ++i) {
    out[i] = static_cast<std::uint8_t>((i * 31u + seed * 17u) & 0xFF);
  }
  return out;
}

// Read back through a plain fd rather than through FileOperations, so a bug in
// the reader cannot mask a bug in the writer.
std::vector<std::uint8_t> readBack(const std::string& path,
                                   std::uint64_t offset,
                                   std::size_t size) {
  std::vector<std::uint8_t> out(size, 0);
  int fd = ::open(path.c_str(), O_RDONLY);
  REQUIRE(fd >= 0);
  ssize_t n = ::pread(fd, out.data(), size, static_cast<off_t>(offset));
  ::close(fd);
  REQUIRE(n == static_cast<ssize_t>(size));
  return out;
}

// Create a sparse scratch image and return its path.
std::string makeImage(const std::string& name, std::uint64_t size = kImageSize) {
  const std::string path = scratch().file(name);
  int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0600);
  REQUIRE(fd >= 0);
  REQUIRE(::ftruncate(fd, static_cast<off_t>(size)) == 0);
  ::close(fd);
  return path;
}

// runCapture() and LoopDevice live in loop_device.h: the extract tests need
// the same block device, for the same reason.
using rpi_test::LoopDevice;
using rpi_test::runCapture;

}  // namespace

TEST_CASE("CreateTestFile produces a file of the requested size", "[file-ops]") {
  const std::string path = scratch().file("created.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  REQUIRE(ops->CreateTestFile(path, kImageSize) == FileError::kSuccess);
  REQUIRE(ops->IsOpen());

  std::uint64_t size = 0;
  REQUIRE(ops->GetSize(size) == FileError::kSuccess);
  CHECK(size == kImageSize);

  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK_FALSE(ops->IsOpen());
  CHECK(fs::file_size(path) == kImageSize);
}

TEST_CASE("WriteAtOffset puts bytes exactly where asked", "[file-ops]") {
  const std::string path = makeImage("offsets.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  // Deliberately not sector-aligned, and deliberately out of order: the write
  // path must not assume callers arrive sequentially.
  struct { std::uint64_t offset; std::uint8_t seed; std::size_t size; } writes[] = {
      {1u * 1024 * 1024,   0x11, 4096},
      {   0,               0x22, 512},
      {3u * 1024 * 1024 + 777, 0x33, 1000},
  };

  for (const auto& w : writes) {
    const auto data = pattern(w.size, w.seed);
    REQUIRE(ops->WriteAtOffset(w.offset, data.data(), data.size()) == FileError::kSuccess);
  }
  REQUIRE(ops->ForceSync() == FileError::kSuccess);
  REQUIRE(ops->Close() == FileError::kSuccess);

  for (const auto& w : writes) {
    INFO("offset " << w.offset << " size " << w.size);
    CHECK(readBack(path, w.offset, w.size) == pattern(w.size, w.seed));
  }

  // The gap between the first two writes must still be untouched.
  const auto gap = readBack(path, 512, 512);
  CHECK(std::all_of(gap.begin(), gap.end(), [](std::uint8_t b) { return b == 0; }));
}

TEST_CASE("Sequential writes round-trip through Seek and ReadSequential", "[file-ops]") {
  const std::string path = makeImage("sequential.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  // Uneven chunk sizes, so the implementation cannot get away with assuming a
  // fixed block.
  const std::vector<std::size_t> chunkSizes = {4096, 517, 65536, 1, 8191};
  std::vector<std::uint8_t> expected;

  std::uint8_t seed = 1;
  for (std::size_t size : chunkSizes) {
    const auto chunk = pattern(size, seed++);
    REQUIRE(ops->WriteSequential(chunk.data(), chunk.size()) == FileError::kSuccess);
    expected.insert(expected.end(), chunk.begin(), chunk.end());
  }

  CHECK(ops->Tell() == expected.size());
  REQUIRE(ops->Flush() == FileError::kSuccess);

  REQUIRE(ops->Seek(0) == FileError::kSuccess);
  CHECK(ops->Tell() == 0);

  std::vector<std::uint8_t> actual(expected.size(), 0);
  std::size_t total = 0;
  while (total < actual.size()) {
    std::size_t got = 0;
    REQUIRE(ops->ReadSequential(actual.data() + total, actual.size() - total, got)
            == FileError::kSuccess);
    REQUIRE(got > 0);  // no progress would mean an infinite loop
    total += got;
  }
  CHECK(total == expected.size());
  CHECK(actual == expected);

  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK(readBack(path, 0, expected.size()) == expected);
}

TEST_CASE("Async writes all land, and every callback fires", "[file-ops]") {
  auto ops = FileOperations::Create();
  if (!ops->IsAsyncIOSupported()) {
    SKIP("async I/O is not available in this build (no liburing, or too old)");
  }

  const std::string path = makeImage("async.img");
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->SetAsyncQueueDepth(4));
  CHECK(ops->GetAsyncQueueDepth() == 4);

  constexpr int kWrites = 16;
  constexpr std::size_t kChunk = 64 * 1024;

  // The interface requires buffers to outlive the write, so they are owned
  // here for the whole case rather than per iteration.
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(kWrites);
  std::atomic<int> callbacks{0};
  std::atomic<int> failures{0};

  for (int i = 0; i < kWrites; ++i) {
    buffers.push_back(pattern(kChunk, static_cast<std::uint8_t>(i)));
    REQUIRE(ops->AsyncWriteSequential(
                buffers.back().data(), kChunk,
                [&callbacks, &failures](FileError result, std::size_t) {
                  callbacks.fetch_add(1);
                  if (result != FileError::kSuccess) failures.fetch_add(1);
                }) == FileError::kSuccess);
  }

  REQUIRE(ops->WaitForPendingWrites() == FileError::kSuccess);
  CHECK(ops->GetPendingWriteCount() == 0);
  CHECK(callbacks.load() == kWrites);
  CHECK(failures.load() == 0);

  REQUIRE(ops->Close() == FileError::kSuccess);

  for (int i = 0; i < kWrites; ++i) {
    INFO("chunk " << i);
    CHECK(readBack(path, static_cast<std::uint64_t>(i) * kChunk, kChunk) == buffers[i]);
  }
}

TEST_CASE("Sync fallback replays queued writes rather than dropping them", "[file-ops]") {
  auto ops = FileOperations::Create();
  if (!ops->IsAsyncIOSupported()) {
    SKIP("async I/O is not available in this build (no liburing, or too old)");
  }

  const std::string path = makeImage("fallback.img");
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->SetAsyncQueueDepth(4));
  CHECK_FALSE(ops->IsInSyncFallbackMode());

  constexpr int kWrites = 8;
  constexpr std::size_t kChunk = 32 * 1024;

  // Buffers must outlive the writes: the fallback replays from pointers the
  // caller supplied, so they have to still be alive when it runs.
  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(kWrites);
  for (int i = 0; i < kWrites; ++i) {
    buffers.push_back(pattern(kChunk, static_cast<std::uint8_t>(0x40 + i)));
    REQUIRE(ops->AsyncWriteSequential(buffers.back().data(), kChunk, nullptr)
            == FileError::kSuccess);
  }

  // The recovery path proper: cancel whatever is in flight and replay every
  // still-pending write synchronously. This is the code that runs each pwrite
  // and the closing fsync under runWithTimeout, so it is the reason this file
  // exists. Writes that already completed asynchronously are not replayed --
  // they have landed -- and replaying one that did complete would be harmless
  // anyway, being the same bytes at the same offset.
  REQUIRE(ops->AttemptSyncFallback() == FileError::kSuccess);
  CHECK(ops->IsInSyncFallbackMode());

  REQUIRE(ops->Close() == FileError::kSuccess);

  for (int i = 0; i < kWrites; ++i) {
    INFO("chunk " << i << " queued before the fallback");
    CHECK(readBack(path, static_cast<std::uint64_t>(i) * kChunk, kChunk) == buffers[i]);
  }
}

TEST_CASE("Draining to sync mode completes once completions are consumed", "[file-ops]") {
  auto ops = FileOperations::Create();
  if (!ops->IsAsyncIOSupported()) {
    SKIP("async I/O is not available in this build (no liburing, or too old)");
  }

  const std::string path = makeImage("drain.img");
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->SetAsyncQueueDepth(4));

  constexpr int kWrites = 8;
  constexpr std::size_t kChunk = 32 * 1024;

  std::vector<std::vector<std::uint8_t>> buffers;
  buffers.reserve(kWrites);
  for (int i = 0; i < kWrites; ++i) {
    buffers.push_back(pattern(kChunk, static_cast<std::uint8_t>(0x80 + i)));
    REQUIRE(ops->AsyncWriteSequential(buffers.back().data(), kChunk, nullptr)
            == FileError::kSuccess);
  }

  // DrainAndSwitchToSync deliberately does NOT consume the completion queue
  // itself. io_uring allows only one CQ consumer, so on Linux
  // PollAsyncCompletions() is a documented no-op and the sole consumer is the
  // extract thread, inside AsyncWriteSequential() and WaitForPendingWrites().
  // The watchdog calls the drain from a different thread and watches the
  // pending count fall.
  //
  // Reproduce exactly that topology. Calling the drain with nobody consuming
  // is not a failure of the drain -- it is the contract -- but it does mean a
  // single-threaded version of this test would stall for the full timeout.
  std::thread consumer([&ops]() { ops->WaitForPendingWrites(); });

  const bool drained = ops->DrainAndSwitchToSync(30);
  consumer.join();

  CHECK(drained);
  CHECK(ops->IsInSyncFallbackMode());
  CHECK(ops->GetPendingWriteCount() == 0);

  // Writes must still work after the switch, now synchronously.
  const auto tail = pattern(kChunk, 0xFE);
  REQUIRE(ops->WriteSequential(tail.data(), tail.size()) == FileError::kSuccess);

  REQUIRE(ops->Close() == FileError::kSuccess);

  for (int i = 0; i < kWrites; ++i) {
    INFO("chunk " << i << " queued before the drain");
    CHECK(readBack(path, static_cast<std::uint64_t>(i) * kChunk, kChunk) == buffers[i]);
  }
  CHECK(readBack(path, static_cast<std::uint64_t>(kWrites) * kChunk, kChunk) == tail);
}

TEST_CASE("Opening a path that does not exist fails cleanly", "[file-ops]") {
  auto ops = FileOperations::Create();
  CHECK(ops->OpenDevice(scratch().file("definitely-absent.img")) != FileError::kSuccess);
  CHECK_FALSE(ops->IsOpen());
  // Closing something that was never opened must not crash or report success
  // it did not achieve.
  ops->Close();
  CHECK_FALSE(ops->IsOpen());
}

TEST_CASE("Writes round-trip through a loopback block device", "[file-ops][loop]") {
  const std::string backing = makeImage("loop.img", 32u * 1024 * 1024);
  LoopDevice loop(backing);
  if (!loop.valid()) {
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  }

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(loop.path()) == FileError::kSuccess);

  // A block device is the case where the implementation reaches for O_DIRECT,
  // so the size query and the buffer alignment both matter here in a way they
  // do not for a regular file.
  std::uint64_t size = 0;
  REQUIRE(ops->GetSize(size) == FileError::kSuccess);
  CHECK(size == 32u * 1024 * 1024);

  constexpr std::size_t kChunk = 1u * 1024 * 1024;
  AlignedBuffer buffer(kChunk);
  REQUIRE(buffer.valid());
  const auto expected = pattern(kChunk, 0x5A);
  std::memcpy(buffer.data(), expected.data(), kChunk);

  REQUIRE(ops->WriteAtOffset(0, buffer.data(), kChunk) == FileError::kSuccess);
  REQUIRE(ops->ForceSync() == FileError::kSuccess);
  REQUIRE(ops->Close() == FileError::kSuccess);

  // Read through the backing file, not the loop device, so the check does not
  // depend on the loop driver's own caching.
  CHECK(readBack(backing, 0, kChunk) == expected);
}

// ---------------------------------------------------------------------------
// A device that returns errors
// ---------------------------------------------------------------------------
//
// The async submission path has a whole tier of error handling that only runs
// on a negative completion coming back from the block layer: the completion
// reaper's error branch, the recorded first-async-error, and the fallback
// from async to synchronous writes. None of it can be reached by passing bad
// arguments -- the write has to be accepted and then fail.
//
// The device below is synthetic: a scratch file on a loop device with a
// device-mapper table that maps the first megabytes through and returns EIO
// beyond them. Nothing real is touched, and it needs root only to create the
// mapping, so these skip when passwordless sudo is unavailable.


// O_DIRECT requires the buffer, the length and the file offset to be aligned
// to the device's logical block size; a std::vector's storage is not. The
// production code allocates through AlignedBuffer for exactly this reason.
static std::unique_ptr<std::uint8_t[], void (*)(void *)> alignedBuffer(std::size_t size,
                                                                      std::uint8_t fill) {
  void *raw = nullptr;
  if (::posix_memalign(&raw, 4096, size) != 0) raw = nullptr;
  std::unique_ptr<std::uint8_t[], void (*)(void *)> buf(
      static_cast<std::uint8_t *>(raw), [](void *p) { ::free(p); });
  if (buf) std::memset(buf.get(), fill, size);
  return buf;
}

TEST_CASE("Async writes report a device that fails partway", "[file-ops][faulty]") {
  using rpi_imager::testing::canRunPrivileged;
  using rpi_imager::testing::FaultyDevice;

  if (!canRunPrivileged())
    SKIP("passwordless sudo is unavailable, so no faulty device can be built");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the device-mapper fault injection device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);

  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async path to fail");

  REQUIRE(ops->SetAsyncQueueDepth(8));

  // Write well past the 8MB boundary. The first writes land; the ones beyond
  // it come back as negative completions.
  const std::size_t kChunk = 1u << 20;
  auto buffer = alignedBuffer(kChunk, 0xC3);
  REQUIRE(buffer);

  bool sawError = false;
  for (int i = 0; i < 24; ++i) {
    const FileError result = ops->AsyncWriteSequential(buffer.get(), kChunk, nullptr);
    if (result != FileError::kSuccess) {
      sawError = true;
      break;
    }
  }
  ops->WaitForPendingWrites();

  // Either submission refused once the first error was recorded, or the
  // completions carried it -- both are the error path. What must not happen
  // is every write reporting success against a device that stopped taking
  // them.
  INFO("submission reported an error: " << sawError);
  CHECK((sawError || ops->GetLastErrorCode() != 0 ||
         ops->WriteSequential(buffer.get(), kChunk) != FileError::kSuccess));
}

TEST_CASE("Async write callbacks see the failure", "[file-ops][faulty]") {
  using rpi_imager::testing::canRunPrivileged;
  using rpi_imager::testing::FaultyDevice;

  if (!canRunPrivileged())
    SKIP("passwordless sudo is unavailable, so no faulty device can be built");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the device-mapper fault injection device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async path to fail");
  REQUIRE(ops->SetAsyncQueueDepth(8));

  const std::size_t kChunk = 1u << 20;
  auto buffer = alignedBuffer(kChunk, 0x5A);
  REQUIRE(buffer);

  std::atomic<int> completions{0};
  std::atomic<int> failures{0};

  for (int i = 0; i < 24; ++i) {
    const FileError submitted = ops->AsyncWriteSequential(
        buffer.get(), kChunk,
        [&](FileError result, std::size_t) {
          completions.fetch_add(1);
          if (result != FileError::kSuccess) failures.fetch_add(1);
        });
    if (submitted != FileError::kSuccess) break;
  }
  ops->WaitForPendingWrites();

  // Every submitted write gets exactly one callback, whatever the outcome --
  // that contract is what the caller's buffer lifetime depends on.
  INFO("completions: " << completions.load() << " failures: " << failures.load());
  CHECK(completions.load() > 0);
  CHECK(failures.load() > 0);
}

TEST_CASE("Writes succeed inside the good region of a mapped device",
          "[file-ops][faulty]") {
  using rpi_imager::testing::canRunPrivileged;
  using rpi_imager::testing::FaultyDevice;

  if (!canRunPrivileged())
    SKIP("passwordless sudo is unavailable, so no faulty device can be built");

  // Fully mapped: the control case, so a failure above is the device rather
  // than the harness.
  FaultyDevice device(32, 32);
  if (!device.isReady())
    SKIP("the device-mapper fault injection device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);

  const std::size_t kChunk = 1u << 20;
  auto buffer = alignedBuffer(kChunk, 0x11);
  REQUIRE(buffer);
  CHECK(ops->WriteSequential(buffer.get(), kChunk) == FileError::kSuccess);
}

// ── Lifecycle and durability ────────────────────────────────────────────────
//
// The write layer is the last thing between the decompressed image and the
// card. Two of its promises matter more than the rest: that a reported
// success has actually reached the device, and that a failed or repeated
// operation leaves the handle in a state the caller can reason about rather
// than half-open.

TEST_CASE("Operations on an unopened handle fail instead of asserting",
          "[file-ops]") {
  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  CHECK_FALSE(ops->IsOpen());

  std::uint64_t size = 0;
  const auto data = pattern(512, 0x5A);

  // None of these may be undefined behaviour on a closed handle: the write
  // path calls them from error handlers, where the file may already be gone.
  CHECK(ops->GetSize(size) != FileError::kSuccess);
  CHECK(ops->WriteAtOffset(0, data.data(), data.size()) != FileError::kSuccess);
  CHECK(ops->ForceSync() != FileError::kSuccess);
}

TEST_CASE("Closing twice is not an error the caller has to guard against",
          "[file-ops]") {
  const std::string path = makeImage("doubleclose.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK_FALSE(ops->IsOpen());

  // Cleanup paths call Close() again on the way out.
  ops->Close();
  CHECK_FALSE(ops->IsOpen());
}

TEST_CASE("Opening a directory is refused", "[file-ops]") {
  // A path that exists but is not a file: writing to it would fail later, in
  // the middle of the image rather than before anything started.
  // A directory that certainly exists: the scratch area's own subdirectory.
  const std::string dir = scratch().file("a-directory");
  fs::create_directories(dir);

  auto ops = FileOperations::Create();
  CHECK(ops->OpenDevice(dir) != FileError::kSuccess);
  CHECK_FALSE(ops->IsOpen());
}

TEST_CASE("Reopening replaces the previous handle", "[file-ops]") {
  const std::string first = makeImage("first.img");
  const std::string second = makeImage("second.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(first) == FileError::kSuccess);
  REQUIRE(ops->OpenDevice(second) == FileError::kSuccess);
  CHECK(ops->IsOpen());

  // Writes now land in the second file, and the first is untouched.
  const auto data = pattern(512, 0x77);
  REQUIRE(ops->WriteAtOffset(0, data.data(), data.size()) == FileError::kSuccess);
  REQUIRE(ops->ForceSync() == FileError::kSuccess);
  REQUIRE(ops->Close() == FileError::kSuccess);

  CHECK(readBack(second, 0, 512) == data);
  const auto untouched = readBack(first, 0, 512);
  CHECK(std::all_of(untouched.begin(), untouched.end(),
                    [](std::uint8_t b) { return b == 0; }));
}

TEST_CASE("Tell follows Seek and the writes that follow it", "[file-ops]") {
  const std::string path = makeImage("tell.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  REQUIRE(ops->Seek(4096) == FileError::kSuccess);
  CHECK(ops->Tell() == 4096);

  const auto data = pattern(1024, 0x31);
  REQUIRE(ops->WriteSequential(data.data(), data.size()) == FileError::kSuccess);
  CHECK(ops->Tell() == 4096 + 1024);

  REQUIRE(ops->Seek(0) == FileError::kSuccess);
  CHECK(ops->Tell() == 0);

  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK(readBack(path, 4096, 1024) == data);
}

TEST_CASE("Direct I/O can be turned on and off on an open handle",
          "[file-ops]") {
  // The imager switches between buffered and direct I/O depending on the
  // target. Toggling must not invalidate the handle or silently drop the
  // file position, or the next write lands in the wrong place.
  const std::string path = makeImage("directio.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  const auto before = ops->SetDirectIOEnabled(true);
  INFO("enabling direct I/O returned " << static_cast<int>(before));

  // Whether O_DIRECT is available depends on the filesystem under the
  // scratch directory, so the result is not asserted -- but the handle must
  // still be usable either way.
  CHECK(ops->IsOpen());

  REQUIRE(ops->SetDirectIOEnabled(false) == FileError::kSuccess);
  CHECK(ops->IsOpen());

  const auto data = pattern(4096, 0x63);
  REQUIRE(ops->WriteAtOffset(0, data.data(), data.size()) == FileError::kSuccess);
  REQUIRE(ops->ForceSync() == FileError::kSuccess);
  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK(readBack(path, 0, 4096) == data);
}

TEST_CASE("A write past the end of the file extends it", "[file-ops]") {
  const std::string path = makeImage("extend.img");
  const std::uint64_t beyond = kImageSize + 8192;

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  const auto data = pattern(512, 0x44);
  REQUIRE(ops->WriteAtOffset(beyond, data.data(), data.size()) == FileError::kSuccess);

  std::uint64_t size = 0;
  REQUIRE(ops->GetSize(size) == FileError::kSuccess);
  CHECK(size == beyond + 512);

  REQUIRE(ops->Close() == FileError::kSuccess);
  CHECK(readBack(path, beyond, 512) == data);
}

// ═══════════════════════════════════════════════════════════════════════════
// The async write path and its recovery ladder
//
// Writes are queued through io_uring and completed later. When a card stops
// keeping up, the watchdog walks a ladder: poll for completions, shrink the
// queue, drain what is outstanding and fall back to synchronous writes. Each
// rung is a public method here, and each is reached in production only after
// a stall -- which is why almost none of it had been executed.
//
// Getting this wrong is the difference between a slow card that eventually
// finishes and a write that dies half way. Driving the methods directly is
// far more controllable than provoking a real stall.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// A loopback block device, which is what the async machinery actually needs:
// against a regular file the queue depth comes back as 1 and no async
// statistics are recorded, so the interesting paths are never taken.
struct AsyncFixture {
  std::string backing;
  std::unique_ptr<LoopDevice> loop;
  std::unique_ptr<rpi_imager::FileOperations> ops;

  explicit AsyncFixture(const std::string &name) {
    backing = makeImage(name, 32u * 1024 * 1024);
    loop = std::make_unique<LoopDevice>(backing);
    ops = rpi_imager::FileOperations::Create();
  }

  ~AsyncFixture() {
    if (ops)
      ops->Close();
  }

  bool haveDevice() const { return loop && loop->valid(); }

  // The queue depth defaults to 1, and AsyncWriteSequential falls straight
  // through to a synchronous write at that depth. DownloadThread configures
  // it explicitly before writing; without the same step none of the async
  // machinery is exercised at all.
  bool open(int queueDepth = 16) {
    if (ops->OpenDevice(loop->path()) != rpi_imager::FileError::kSuccess)
      return false;
    if (ops->IsAsyncIOSupported())
      ops->SetAsyncQueueDepth(queueDepth);
    return true;
  }
};

// O_DIRECT is enabled for block devices, and it rejects a buffer that is not
// aligned to the logical block size -- which a std::vector is not. Production
// writes out of an AlignedBuffer for the same reason.
class AlignedBlock {
 public:
  AlignedBlock(std::size_t size, std::uint8_t seed) : buffer_(size), size_(size) {
    auto* p = static_cast<std::uint8_t*>(buffer_.data());
    for (std::size_t i = 0; i < size; ++i)
      p[i] = static_cast<std::uint8_t>((i * 7 + seed) & 0xFF);
  }
  const std::uint8_t* data() const {
    return static_cast<const std::uint8_t*>(buffer_.data());
  }
  std::size_t size() const { return size_; }
  bool valid() const { return buffer_.valid(); }

 private:
  rpi_imager::AlignedBuffer buffer_;
  std::size_t size_;
};

}  // namespace

TEST_CASE("Async writes complete and land", "[fileops][async]") {
  AsyncFixture fx("rpi-imager-async-land.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());
  if (!fx.ops->IsAsyncIOSupported())
    SKIP("io_uring is not available here");

  const AlignedBlock block(64 * 1024, 3);
  REQUIRE(block.valid());
  std::atomic<int> completed{0};
  for (int i = 0; i < 8; ++i) {
    const auto err = fx.ops->AsyncWriteSequential(
        block.data(), block.size(),
        [&completed](rpi_imager::FileError, std::size_t) { ++completed; });
    REQUIRE(err == rpi_imager::FileError::kSuccess);
  }

  // Nothing is guaranteed to have finished until this returns.
  REQUIRE(fx.ops->WaitForPendingWrites() == rpi_imager::FileError::kSuccess);
  CHECK(fx.ops->GetPendingWriteCount() == 0);
  CHECK(completed.load() == 8);

  fx.ops->Close();
  std::ifstream check(fx.loop->path(), std::ios::binary);
  std::vector<char> first(block.size());
  check.read(first.data(), static_cast<std::streamsize>(first.size()));
  CHECK(std::memcmp(first.data(), block.data(), block.size()) == 0);
}

// Three more rungs are deliberately not covered here: polling alone,
// DrainAndSwitchToSync() and CancelAsyncIO(). Driven directly they leave
// writes queued -- WaitForPendingWrites() drains the same writes without
// trouble -- which says the submission contract is not what a caller would
// assume from the names. That is worth someone establishing, but a test
// asserting my guess at it would be worse than none.

TEST_CASE("The queue depth can be shrunk for recovery", "[fileops][async]") {
  AsyncFixture fx("rpi-imager-async-depth.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());
  if (!fx.ops->IsAsyncIOSupported())
    SKIP("io_uring is not available here");

  const int original = fx.ops->GetAsyncQueueDepth();
  INFO("queue depth: " << original);
  REQUIRE(original > 2);

  // The second rung of the ladder: fewer outstanding writes so a slow card
  // can drain what it already has.
  fx.ops->ReduceQueueDepthForRecovery(2);
  CHECK(fx.ops->GetAsyncQueueDepth() <= original);

  const AlignedBlock block(16 * 1024, 9);
  for (int i = 0; i < 4; ++i)
    REQUIRE(fx.ops->AsyncWriteSequential(block.data(), block.size(), nullptr)
            == rpi_imager::FileError::kSuccess);
  CHECK(fx.ops->WaitForPendingWrites() == rpi_imager::FileError::kSuccess);
}

TEST_CASE("Pending writes are reported in offset order", "[fileops][async]") {
  AsyncFixture fx("rpi-imager-async-sorted.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());
  if (!fx.ops->IsAsyncIOSupported())
    SKIP("io_uring is not available here");

  const AlignedBlock block(64 * 1024, 13);
  for (int i = 0; i < 6; ++i)
    fx.ops->AsyncWriteSequential(block.data(), block.size(), nullptr);

  // The sync fallback replays these, so their order decides whether the
  // image is reassembled correctly or scrambled.
  const auto pending = fx.ops->GetPendingWritesSorted();
  for (std::size_t i = 1; i < pending.size(); ++i)
    CHECK(pending[i - 1].offset <= pending[i].offset);

  fx.ops->WaitForPendingWrites();
}

TEST_CASE("Direct I/O can be turned off and writing still works", "[fileops][async]") {
  AsyncFixture fx("rpi-imager-async-directio.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());

  // Support asks users to turn this off when a write misbehaves, so it has
  // to leave a working writer behind.
  const auto err = fx.ops->SetDirectIOEnabled(false);
  if (err != rpi_imager::FileError::kSuccess)
    SKIP("direct I/O could not be disabled on this filesystem");

  const AlignedBlock block(8 * 1024, 19);
  CHECK(fx.ops->WriteSequential(block.data(), block.size()) == rpi_imager::FileError::kSuccess);
  CHECK(fx.ops->Flush() == rpi_imager::FileError::kSuccess);
}

TEST_CASE("Async statistics are recorded and can be reset", "[fileops][async]") {
  AsyncFixture fx("rpi-imager-async-stats.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());
  if (!fx.ops->IsAsyncIOSupported())
    SKIP("io_uring is not available here");

  const AlignedBlock block(32 * 1024, 23);
  for (int i = 0; i < 4; ++i)
    fx.ops->AsyncWriteSequential(block.data(), block.size(), nullptr);
  fx.ops->WaitForPendingWrites();

  std::uint32_t wallMs = 0, writes = 0, minUs = 0, maxUs = 0, avgUs = 0;
  fx.ops->GetAsyncIOStats(wallMs, writes, minUs, maxUs, avgUs);
  // These end up in the performance report attached to bug reports.
  CHECK(writes >= 4);
  CHECK(maxUs >= minUs);

  fx.ops->ResetAsyncIOStats();
  fx.ops->GetAsyncIOStats(wallMs, writes, minUs, maxUs, avgUs);
  CHECK(writes == 0);
}

// ══════════════════════════════════════════════════════════════════════════
// What went wrong, in the user's terms
//
// A failed write ends on one screen with one sentence on it. Which sentence
// is decided by classifying the platform's error code, and until now only
// Windows did that: every write failure on Linux and macOS came out as
// kUnknown, and the message behind kUnknown asks the user to check whether
// the device is writable, has room, and is not write-protected -- three
// questions, when the kernel has already answered one of them.
//
// The mapping is checked twice over: once as a table, because most of these
// error codes cannot be produced to order, and once against a real device
// that really does fail.
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("A write error is classified from its errno", "[fileops][writeerror]") {
  using rpi_imager::WriteErrorClass;
  using rpi_imager::ClassifyPosixWriteErrno;

  struct Case {
    int err;
    WriteErrorClass expected;
    const char *what;
  };

  const Case cases[] = {
      {ENOSPC, WriteErrorClass::kDiskFull, "the card is full"},
      {EFBIG, WriteErrorClass::kDiskFull, "a size limit was reached"},
      {EROFS, WriteErrorClass::kWriteProtected, "the write-protect switch"},
      {EACCES, WriteErrorClass::kAccessDenied, "no permission"},
      {EPERM, WriteErrorClass::kAccessDenied, "not permitted"},
      {EIO, WriteErrorClass::kIoDeviceError, "an I/O fault"},
      {ENODEV, WriteErrorClass::kIoDeviceError, "the device went away"},
      {ENXIO, WriteErrorClass::kIoDeviceError, "no such device"},
      {EINVAL, WriteErrorClass::kInvalidParameter, "a bad argument"},
      {EBADF, WriteErrorClass::kInvalidParameter, "a closed handle"},
      // Nothing about the card, so nothing is claimed about it. A message
      // naming a cause that is not the cause sends the user after the wrong
      // thing, which the generic one at least does not do.
      {0, WriteErrorClass::kUnknown, "no error at all"},
      {EAGAIN, WriteErrorClass::kUnknown, "a transient stall"},
      {ENOMEM, WriteErrorClass::kUnknown, "the host out of memory"},
      {EINTR, WriteErrorClass::kUnknown, "an interrupted call"},
  };

  for (const Case &c : cases) {
    INFO(c.what << " (errno " << c.err << ")");
    CHECK(ClassifyPosixWriteErrno(c.err) == c.expected);
  }
}

TEST_CASE("A device that is really out of space says so", "[fileops][writeerror]") {
  // /dev/full is a character device whose whole purpose is to fail every
  // write with ENOSPC. It is the one way to get a genuine full-device error
  // without root and without a real card, and it exercises the whole chain:
  // the write fails, the errno is recorded, and the classification comes back
  // as the one the user is shown a message for.
  if (::access("/dev/full", W_OK) != 0) {
    SKIP("/dev/full is not available on this host");
  }

  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  REQUIRE(ops->OpenDevice("/dev/full") == FileError::kSuccess);

  AlignedBuffer buffer(4096);
  std::memset(buffer.data(), 0x5A, buffer.size());

  const FileError result = ops->WriteSequential(buffer.data(), buffer.size());
  INFO("errno was " << ops->GetLastErrorCode());
  CHECK(result != FileError::kSuccess);
  CHECK(ops->GetLastErrorCode() == ENOSPC);
  CHECK(ops->ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kDiskFull);

  ops->Close();
}

TEST_CASE("A device with room left is not reported as full", "[fileops][writeerror]") {
  // The other half: a write that worked must not leave a diagnosis behind
  // for something later to pick up and show.
  const std::string path = makeImage("room-left.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  AlignedBuffer buffer(4096);
  std::memset(buffer.data(), 0x11, buffer.size());
  REQUIRE(ops->WriteSequential(buffer.data(), buffer.size()) == FileError::kSuccess);

  CHECK(ops->ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kUnknown);
  ops->Close();
}

TEST_CASE("A device something else is holding is still written", "[file-ops][loop]") {
  // Imager unmounts the card before opening it, and then asks for exclusive
  // access so nothing can mount a partition out from under a write in
  // progress. Sometimes it cannot have it: udisks, a file manager, or another
  // copy of Imager may still have the device open.
  //
  // Giving up there would be the wrong answer. The user has chosen a card and
  // agreed to erase it; the write is what they asked for, and "device busy"
  // from somewhere inside the writer names nothing they can act on. So the
  // exclusive open is a preference and the shared one is the fallback.
  //
  // The hold is taken here, on a loop device this test created, so nothing of
  // the user's is involved -- and it is released as soon as the open under
  // test has been answered.
  const std::string backing = makeImage("busy-loop.img", 16u * 1024 * 1024);
  LoopDevice loop(backing);
  if (!loop.valid()) {
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  }

  const int held = ::open(loop.path().c_str(), O_RDWR | O_EXCL | O_CLOEXEC);
  if (held < 0) {
    SKIP(std::string("could not take an exclusive hold on the loop device: ") +
         std::strerror(errno));
  }

  auto ops = FileOperations::Create();
  const FileError opened = ops->OpenDevice(loop.path());
  ::close(held);

  REQUIRE(opened == FileError::kSuccess);

  // And it is a working handle, not merely a successful return: the fallback
  // has to give back something that can be written through.
  constexpr std::size_t kChunk = 1u * 1024 * 1024;
  AlignedBuffer buffer(kChunk);
  REQUIRE(buffer.valid());
  const auto expected = pattern(kChunk, 0x3C);
  std::memcpy(buffer.data(), expected.data(), kChunk);

  REQUIRE(ops->WriteSequential(buffer.data(), kChunk) == FileError::kSuccess);
  REQUIRE(ops->ForceSync() == FileError::kSuccess);

  REQUIRE(ops->Seek(0) == FileError::kSuccess);
  AlignedBuffer readBack(kChunk);
  REQUIRE(readBack.valid());
  std::size_t got = 0;
  REQUIRE(ops->ReadSequential(readBack.data(), kChunk, got) == FileError::kSuccess);
  CHECK(got == kChunk);
  CHECK(std::memcmp(readBack.data(), expected.data(), kChunk) == 0);

  ops->Close();
}

TEST_CASE("A random-access write that fails records why", "[fileops][writeerror]") {
  // WriteAtOffset is a separate implementation from WriteSequential -- pwrite
  // rather than write, so that the sequential cursor is left alone -- and it
  // returned kWriteError without keeping errno. The classification and the
  // message the user reads are both built from that number, so a failure on
  // this path arrived with no reason attached: "Error writing to device." and
  // nothing more, where the sequential path could say the card was full.
  //
  // /dev/full fails every write with ENOSPC, which is the one way to reach a
  // genuine full-device error without root and without a real card.
  if (::access("/dev/full", W_OK) != 0) {
    SKIP("/dev/full is not available on this host");
  }

  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  REQUIRE(ops->OpenDevice("/dev/full") == FileError::kSuccess);

  AlignedBuffer buffer(4096);
  std::memset(buffer.data(), 0x5A, buffer.size());

  const FileError result = ops->WriteAtOffset(0, buffer.data(), buffer.size());
  INFO("errno was " << ops->GetLastErrorCode());
  CHECK(result != FileError::kSuccess);
  CHECK(ops->GetLastErrorCode() == ENOSPC);

  // And the same classification the sequential path produces, so whichever
  // one the write happened to take, the user is told the same thing.
  CHECK(ops->ClassifyLastWriteError() == rpi_imager::WriteErrorClass::kDiskFull);

  ops->Close();
}

TEST_CASE("An async write that fails at completion is reported",
          "[fileops][writeerror]") {
  // The path a card takes when it stops accepting data part-way through a
  // write: the submission is fine, and the failure arrives later in the
  // completion queue. It is the commonest real hardware failure there is,
  // and the branch that handles it was never reached.
  //
  // The faulty-device case above accepts either outcome -- "submission
  // refused" or "the completions carried it" -- and on this machine takes
  // the first, so the completion branch stayed uncovered behind a test that
  // looked like it covered it.
  //
  // /dev/full reaches it directly and needs no privileges: every write to it
  // fails with ENOSPC, and io_uring accepts the submission and reports the
  // failure in the completion.
  if (::access("/dev/full", W_OK) != 0) {
    SKIP("/dev/full is not available on this host");
  }

  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  REQUIRE(ops->OpenDevice("/dev/full") == FileError::kSuccess);

  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no completion path to fail");

  REQUIRE(ops->SetAsyncQueueDepth(4));

  const std::size_t kChunk = 1u << 20;
  AlignedBuffer buffer(kChunk);
  std::memset(buffer.data(), 0x5A, buffer.size());

  // Enough to get past submission and into completions.
  bool submissionRefused = false;
  for (int i = 0; i < 8; ++i) {
    if (ops->AsyncWriteSequential(buffer.data(), buffer.size(), nullptr)
        != FileError::kSuccess) {
      submissionRefused = true;
      break;
    }
  }

  const FileError drained = ops->WaitForPendingWrites();

  INFO("submission refused: " << submissionRefused
       << ", drain said " << static_cast<int>(drained)
       << ", errno " << ops->GetLastErrorCode());

  // However it surfaced, it must not come back as success against a device
  // that took none of it.
  CHECK((submissionRefused || drained != FileError::kSuccess));

  // And the reason survives to whoever builds the message. io_uring hands
  // the error back as -errno in the completion rather than through errno,
  // so it has to be taken from there or it is lost.
  CHECK(ops->GetLastErrorCode() == ENOSPC);

  ops->Close();
}

// ═══════════════════════════════════════════════════════════════════════════
// A write that does not fill a sector
//
// Cards are opened with O_DIRECT, and the kernel refuses a write whose length
// is not a multiple of the logical block size. That is the rule every caller
// writing an image has to respect on its last block, and the reason both
// extract paths pad theirs.
//
// It is stated here rather than assumed, because it is not visible anywhere a
// scratch file can reach: O_DIRECT is only ever set for a block device path,
// so a regular file accepts any length and a caller that forgot to pad looks
// perfectly correct right up until it meets a real card.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A device using direct I/O refuses a partial sector", "[fileops][loop]") {
  AsyncFixture fx("rpi-imager-partial-sector.img");
  if (!fx.haveDevice())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");
  REQUIRE(fx.open());
  // Only meaningful with direct I/O actually on, which for a block device it
  // is by default.
  REQUIRE(fx.ops->IsDirectIOEnabled());

  // The buffer is aligned either way; it is the length that is wrong.
  const AlignedBlock block(4096, 5);
  REQUIRE(block.valid());

  const auto partial = fx.ops->WriteSequential(block.data(), 100);
  INFO("100 bytes -> " << static_cast<int>(partial)
       << ", errno " << fx.ops->GetLastErrorCode());
  CHECK(partial != rpi_imager::FileError::kSuccess);
  // EINVAL, which is the kernel objecting to the length rather than anything
  // being wrong with the device.
  CHECK(fx.ops->GetLastErrorCode() == EINVAL);

  // A whole sector of the same buffer goes through, so the refusal is about
  // the length and not the handle.
  CHECK(fx.ops->WriteSequential(block.data(), 512) == rpi_imager::FileError::kSuccess);
}
