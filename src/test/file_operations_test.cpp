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
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
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

#ifdef __APPLE__
TEST_CASE("A device the user can already write to is opened without asking",
          "[file-ops][macos]") {
  // OpenDevice() used to send every /dev/ path to authopen, so opening a node
  // this user can already write to raised a prompt to grant access they had.
  // /dev/null is such a node, and needs no privilege to prove it: the open
  // has to succeed, and it has to succeed without a dialog -- which is also
  // why this test can run unattended at all.
  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice("/dev/null") == FileError::kSuccess);
  CHECK(ops->IsOpen());
  ops->Close();
}

TEST_CASE("A device that is not there fails rather than prompting",
          "[file-ops][macos]") {
  // The other half: escalate only on a refusal. ENOENT is not something an
  // authorisation dialog can fix, so asking would be a prompt whose only
  // possible outcome is the failure we already have.
  auto ops = FileOperations::Create();
  CHECK(ops->OpenDevice("/dev/rpi-imager-no-such-device") != FileError::kSuccess);
  CHECK(ops->GetLastErrorCode() == ENOENT);
  CHECK_FALSE(ops->IsOpen());
}
#endif

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
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

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
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

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
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  // Fully mapped: the control case, so a failure above is the device rather
  // than the harness.
  FaultyDevice device(32, 32);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

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

  // And only ever down. Recovery is entered because the card is struggling;
  // a caller that asks for a deeper queue while that is still true would undo
  // the back-off, so the request is ignored rather than obeyed.
  const int reduced = fx.ops->GetAsyncQueueDepth();
  fx.ops->ReduceQueueDepthForRecovery(reduced * 8);
  CHECK(fx.ops->GetAsyncQueueDepth() == reduced);

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

// The two platforms answer a partial sector differently, and both answers are
// deliberate: Linux lets O_DIRECT refuse it, macOS absorbs it into a tail that
// PwriteAligned commits by read-modify-write at the next flush.
#ifndef __APPLE__
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
#else
TEST_CASE("A partial sector is absorbed and lands at the flush", "[fileops][loop]") {
  // /dev/rdiskN refuses a write that is not a whole number of sectors, so
  // PwriteAligned holds the residue back rather than passing the refusal up --
  // libarchive hands over chunks of whatever length it likes. What has to be
  // true is that the bytes are not lost: the tail is committed by
  // read-modify-write, so it is on the device once the flush has run.
  AsyncFixture fx("rpi-imager-partial-sector.img");
  if (!fx.haveDevice())
    SKIP("no raw disk image attached (hdiutil unavailable?)");
  REQUIRE(fx.open());
  REQUIRE(fx.ops->IsDirectIOEnabled());

  const AlignedBlock block(4096, 5);
  REQUIRE(block.valid());

  // Accepted, though the device would have refused this length itself.
  CHECK(fx.ops->WriteSequential(block.data(), 100) == rpi_imager::FileError::kSuccess);
  REQUIRE(fx.ops->ForceSync() == rpi_imager::FileError::kSuccess);

  // And actually there afterwards, which is the part a deferral could lose.
  REQUIRE(fx.ops->Seek(0) == rpi_imager::FileError::kSuccess);
  std::vector<std::uint8_t> readBack(512, 0);
  std::size_t got = 0;
  REQUIRE(fx.ops->ReadSequential(readBack.data(), readBack.size(), got) ==
          rpi_imager::FileError::kSuccess);
  REQUIRE(got == 512);
  CHECK(std::equal(readBack.begin(), readBack.begin() + 100, block.data()));
}
#endif

// ---------------------------------------------------------------------------
// The handle after the card has gone
//
// Every entry point below can be called on a closed handle, because the write
// path calls them from its own error handling: the card has been pulled, the
// open failed, or a previous step already tore the handle down. One of them
// touching fd_ = -1 is a crash in the middle of a failure the user was about
// to be told about.
//
// The existing case covers GetSize, WriteAtOffset and ForceSync. These are
// the rest.
// ---------------------------------------------------------------------------

TEST_CASE("Every entry point survives a handle that was never opened",
          "[file-ops]") {
  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);
  REQUIRE_FALSE(ops->IsOpen());

  const auto data = pattern(512, 0x11);
  std::vector<std::uint8_t> readInto(512, 0);
  std::size_t got = 12345;

  CHECK(ops->WriteSequential(data.data(), data.size()) != FileError::kSuccess);
  CHECK(ops->ReadSequential(readInto.data(), readInto.size(), got) != FileError::kSuccess);
  CHECK(ops->Seek(0) != FileError::kSuccess);
  CHECK(ops->Flush() != FileError::kSuccess);
  CHECK(ops->ForceSync() != FileError::kSuccess);
  CHECK(ops->SetDirectIOEnabled(true) != FileError::kSuccess);
  CHECK(ops->WriteAtOffset(0, data.data(), data.size()) != FileError::kSuccess);

  std::uint64_t size = 999;
  CHECK(ops->GetSize(size) != FileError::kSuccess);

  // The async entry point too: the write loop calls this one, and on a handle
  // that failed to open it has to report rather than queue. The callback is
  // the only channel the caller has, so it must fire even here.
  int asyncCalls = 0;
  CHECK(ops->AsyncWriteSequential(data.data(), data.size(),
                                  [&](FileError, std::size_t) { ++asyncCalls; }) !=
        FileError::kSuccess);
  CHECK(asyncCalls == 1);
  CHECK(ops->WaitForPendingWrites() == FileError::kSuccess);
  CHECK(ops->GetPendingWriteCount() == 0);

  // These have no error to return, so what matters is that they answer at all
  // and answer something a caller can act on.
  CHECK(ops->Tell() == 0);
  CHECK(ops->GetHandle() < 0);
  CHECK_FALSE(ops->IsDirectIOEnabled());
  CHECK_NOTHROW(ops->PrepareForSequentialRead(0, 4096));
  CHECK_NOTHROW(ops->CancelAsyncIO());
}

TEST_CASE("Asking for the direct I/O state the handle is already in costs nothing",
          "[file-ops]") {
  // The read-back before customisation verification asks for direct I/O
  // without knowing whether it is already on, precisely so it does not have
  // to. Reopening the device to tell it what it already knows would drop the
  // exclusive hold on the card for no reason.
  const std::string path = makeImage("directio-noop.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  // Unlinked while open, which is what makes this case say something: the
  // open handle keeps working, but the path no longer resolves. A no-op
  // succeeds; a reopen would go back to the path and fail. Without this the
  // case passes either way -- a reopen usually gets the same descriptor
  // number back, so nothing else here can tell the two apart.
  REQUIRE(::unlink(path.c_str()) == 0);

  const bool before = ops->IsDirectIOEnabled();

  CHECK(ops->SetDirectIOEnabled(before) == FileError::kSuccess);
  CHECK(ops->IsDirectIOEnabled() == before);
  CHECK(ops->IsOpen());

  ops->Close();
}

TEST_CASE("Toggling direct I/O keeps the place in the file", "[file-ops]") {
  // Turning direct I/O on reopens the handle, and a reopened handle starts at
  // zero. The customisation read-back asks for it part-way through the device
  // -- after the write has already positioned the handle -- so losing the
  // position there means verifying the wrong bytes and reporting a card
  // corrupt that is fine.
  const std::string path = makeImage("directio-seek.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  constexpr std::uint64_t kSomewhere = 8192;
  REQUIRE(ops->Seek(kSomewhere) == FileError::kSuccess);
  REQUIRE(ops->Tell() == kSomewhere);

  const bool before = ops->IsDirectIOEnabled();
  // Toggle to the other state and back, so the case works whichever state a
  // plain file opens in.
  REQUIRE(ops->SetDirectIOEnabled(!before) == FileError::kSuccess);
  CHECK(ops->Tell() == kSomewhere);

  REQUIRE(ops->SetDirectIOEnabled(before) == FileError::kSuccess);
  CHECK(ops->Tell() == kSomewhere);

  ops->Close();
}

// ---------------------------------------------------------------------------
// Falling back to synchronous writes
//
// When the async queue stops making progress the writer abandons it and
// replays whatever was outstanding synchronously, then carries on in sync
// mode. It is the last thing between a stalled io_uring and a write that
// never finishes, and most of it was uncovered.
// ---------------------------------------------------------------------------

TEST_CASE("Falling back with nothing outstanding is a clean switch",
          "[fileops][async]") {
  // The watchdog can decide the queue has stalled at a moment when it has in
  // fact just drained. There is nothing to replay, and that is a switch to
  // sync mode rather than a failure -- reporting an error here would abort a
  // write that is going perfectly well.
  const std::string path = makeImage("fallback-empty.img", 4u * 1024 * 1024);

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async path to fall back from");

  REQUIRE(ops->GetPendingWriteCount() == 0);
  CHECK_FALSE(ops->IsInSyncFallbackMode());

  CHECK(ops->AttemptSyncFallback() == FileError::kSuccess);

  // And it really did switch, rather than deciding there was nothing to do:
  // the caller reads this to know the queue is no longer in play.
  CHECK(ops->IsInSyncFallbackMode());

  ops->Close();
}

TEST_CASE("A write replayed onto a device that has stopped taking them is reported",
          "[file-ops][faulty]") {
  // The queue stalled because the card stopped answering, not because the
  // kernel was busy. Replaying the outstanding writes synchronously runs into
  // the same wall, and the fallback has to say so -- carrying on in sync mode
  // over a device that is refusing writes is how a write reports success with
  // half an image on the card.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async path to fall back from");

  REQUIRE(ops->SetAsyncQueueDepth(16));

  // Fill the queue with writes aimed past the 8MB boundary, where the mapping
  // returns EIO, and do not poll: they are still outstanding when the
  // fallback is asked to replay them.
  const std::size_t kChunk = 1u << 20;
  auto buffer = alignedBuffer(kChunk, 0x9E);
  REQUIRE(buffer);
  REQUIRE(ops->Seek(8u * 1024 * 1024) == FileError::kSuccess);
  for (int i = 0; i < 12; ++i) {
    if (ops->AsyncWriteSequential(buffer.get(), kChunk, nullptr) != FileError::kSuccess)
      break;
  }

  const FileError fallback = ops->AttemptSyncFallback();

  // Whatever it found -- writes still pending that then failed, or a queue
  // that had already drained into errors -- it must not report a plain
  // success over a device in this state.
  INFO("fallback returned " << static_cast<int>(fallback)
       << ", last errno " << ops->GetLastErrorCode());
  const bool refused = (fallback != FileError::kSuccess);
  const bool deviceIsUnusable =
      (ops->WriteSequential(buffer.get(), kChunk) != FileError::kSuccess);
  CHECK((refused || deviceIsUnusable));

  ops->Close();
}

TEST_CASE("A device with no async queue still writes, and still calls back",
          "[file-ops]") {
  // io_uring is not everywhere: an older kernel, a container with the syscall
  // seccomp-blocked, or a host where the ring would not initialise all end up
  // here, and so does the queue depth of 1 that FileOperations starts with.
  // The caller does not branch on any of that -- it hands over a buffer and a
  // callback either way -- so the synchronous fall-through has to honour the
  // same contract: the bytes land, and the callback fires exactly once with
  // the number of bytes it was given.
  const std::string path = makeImage("no-async-queue.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->GetAsyncQueueDepth() <= 1);

  const auto expected = pattern(8192, 0x64);
  int calls = 0;
  FileError reported = FileError::kLockError;  // anything the call cannot return
  std::size_t reportedBytes = 0;

  REQUIRE(ops->AsyncWriteSequential(expected.data(), expected.size(),
                                    [&](FileError e, std::size_t n) {
                                      ++calls;
                                      reported = e;
                                      reportedBytes = n;
                                    }) == FileError::kSuccess);

  CHECK(calls == 1);
  CHECK(reported == FileError::kSuccess);
  CHECK(reportedBytes == expected.size());
  CHECK(ops->Tell() == expected.size());

  ops->Close();
  CHECK(readBack(path, 0, expected.size()) == expected);
}

TEST_CASE("A position the kernel cannot represent is refused", "[file-ops]") {
  // Seek takes an unsigned offset and lseek a signed one, so anything with the
  // top bit set arrives at the kernel as a negative position. The only two
  // answers are to report it or to carry on writing at whatever position the
  // handle happened to be left at -- and the second one silently puts image
  // data somewhere other than where the caller asked for it.
  const std::string path = makeImage("impossible-seek.img");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->Seek(4096) == FileError::kSuccess);

  CHECK(ops->Seek(std::numeric_limits<std::uint64_t>::max()) ==
        FileError::kSeekError);
  CHECK(ops->GetLastErrorCode() == EINVAL);

  ops->Close();
}

TEST_CASE("A scratch file that cannot be made says so", "[file-ops]") {
  // CreateTestFile is how the write-speed benchmark and the pre-flight space
  // check get a file to work with. Both are on the path to starting a write,
  // so a failure here has to come back as a failure rather than as a handle
  // to a file that does not exist or is not the size that was asked for.
  auto ops = FileOperations::Create();

  SECTION("nowhere to put it") {
    CHECK(ops->CreateTestFile("/proc/self/no/such/place/probe.bin", 4096) !=
          FileError::kSuccess);
    CHECK_FALSE(ops->IsOpen());
  }

  SECTION("a size that cannot be represented") {
    // The size arrives unsigned and reaches ftruncate signed, so anything with
    // the top bit set is a negative length by the time the kernel sees it. The
    // handle is opened before the size is set, so what matters is that the
    // failure closes it again rather than leaving an empty file open behind a
    // returned error -- the caller has no handle to close it with.
    const std::string path = scratch().file("absurd-size.bin");
    CHECK(ops->CreateTestFile(path, std::numeric_limits<std::uint64_t>::max()) ==
          FileError::kSizeError);
    CHECK_FALSE(ops->IsOpen());
  }
}

TEST_CASE("A read that hits a bad sector is reported, not returned as data",
          "[file-ops][faulty]") {
  // Verification reads the card back and hashes what it finds. A read that
  // fails has to be told apart from one that succeeded, because the buffer is
  // hashed either way: a failure reported as success feeds whatever was left
  // in the buffer into the hash, and the user is shown a verification mismatch
  // -- pointing at the write, which was fine -- instead of a read error on a
  // card that has developed a bad sector.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);

  // Inside the good region first, so a failure below is the mapping and not
  // the harness.
  auto buffer = alignedBuffer(1u << 20, 0);
  REQUIRE(buffer);
  std::size_t got = 0;
  REQUIRE(ops->Seek(0) == FileError::kSuccess);
  REQUIRE(ops->ReadSequential(buffer.get(), 1u << 20, got) == FileError::kSuccess);
  CHECK(got == (1u << 20));

  // And past the 8MB boundary, where the mapping returns EIO.
  REQUIRE(ops->Seek(16ull << 20) == FileError::kSuccess);
  got = 12345;
  CHECK(ops->ReadSequential(buffer.get(), 1u << 20, got) == FileError::kReadError);
  CHECK(got == 0);
  CHECK(ops->GetLastErrorCode() == EIO);

  ops->Close();
}

TEST_CASE("A card slow enough to matter makes the writer back off",
          "[file-ops][faulty][slow]") {
  // A card that is slow rather than broken is the common complaint, and it
  // needs different handling: nothing errors, the writes all land eventually,
  // but a deep queue in front of a device that cannot keep up just moves the
  // stall somewhere the user cannot see. So a write that takes longer than
  // kHighLatencyThresholdMs halves the queue depth, repeatedly, down to the
  // floor -- fewer outstanding writes, so the card drains what it has and the
  // progress the user is watching keeps moving.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  if (!rpi_imager::testing::asyncWritesOverlap())
    SKIP("this platform writes serially, so the delays would not overlap");

  FaultyDevice device(64, FaultyDevice::SlowWrites{11000});
  if (!device.isReady())
    SKIP("the delaying device could not be created (dm-delay?)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no queue to shrink");

  constexpr int kStartingDepth = 16;
  REQUIRE(ops->SetAsyncQueueDepth(kStartingDepth));
  REQUIRE(ops->GetAsyncQueueDepth() == kStartingDepth);

  constexpr std::size_t kChunk = 64u * 1024;
  auto buffer = alignedBuffer(kChunk, 0xA7);
  REQUIRE(buffer);

  // Four writes, submitted together so the device delays them in parallel and
  // the whole case costs one delay rather than four.
  std::atomic<int> completed{0};
  for (int i = 0; i < 4; ++i) {
    REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk,
                                      [&](FileError e, std::size_t) {
                                        if (e == FileError::kSuccess) ++completed;
                                      }) == FileError::kSuccess);
  }

  // The wait itself is part of what is under test: ProcessCompletions gives up
  // on any single blocking wait after kAsyncFirstCompletionTimeoutMs so the
  // caller can react, and WaitForPendingWrites has to come back and wait again
  // rather than treat that as the device being gone.
  CHECK(ops->WaitForPendingWrites() == FileError::kSuccess);
  CHECK(completed.load() == 4);
  CHECK(ops->GetPendingWriteCount() == 0);

  const int settled = ops->GetAsyncQueueDepth();
  INFO("queue depth settled at " << settled << " from " << kStartingDepth);
  CHECK(settled < kStartingDepth);
  CHECK(settled >= 2);

  // Backing off is only worth anything if the data still arrives.
  REQUIRE(ops->Seek(0) == FileError::kSuccess);
  auto readInto = alignedBuffer(kChunk, 0);
  REQUIRE(readInto);
  std::size_t got = 0;
  REQUIRE(ops->ReadSequential(readInto.get(), kChunk, got) == FileError::kSuccess);
  CHECK(got == kChunk);
  CHECK(std::memcmp(readInto.get(), buffer.get(), kChunk) == 0);

  ops->Close();
}

TEST_CASE("Cancelling a busy card is answered without waiting it out",
          "[file-ops][faulty][slow]") {
  // Cancel has to be answered while the card is busy, which is the only time
  // anyone presses it. Waiting for the writes already handed to a slow card
  // would leave the button dead for as long as the card takes, which on the
  // cards people complain about is the whole complaint.
  //
  // What must not happen is the outstanding writes being reported as write
  // errors: the user asked for this, and a failure dialog for a cancellation
  // sends them looking for a fault in the card.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  if (!rpi_imager::testing::asyncWritesOverlap())
    SKIP("this platform writes serially, so the delays would not overlap");

  FaultyDevice device(64, FaultyDevice::SlowWrites{4000});
  if (!device.isReady())
    SKIP("the delaying device could not be created (dm-delay?)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is nothing to cancel");

  constexpr int kDepth = 4;
  REQUIRE(ops->SetAsyncQueueDepth(kDepth));

  constexpr std::size_t kChunk = 64u * 1024;
  auto buffer = alignedBuffer(kChunk, 0x3B);
  REQUIRE(buffer);

  std::atomic<int> cancelled{0};
  std::atomic<int> failed{0};
  std::atomic<int> succeeded{0};
  auto note = [&](FileError e, std::size_t) {
    if (e == FileError::kCancelled)
      ++cancelled;
    else if (e == FileError::kSuccess)
      ++succeeded;
    else
      ++failed;
  };

  for (int i = 0; i < kDepth; ++i)
    REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk, note) ==
            FileError::kSuccess);
  REQUIRE(ops->GetPendingWriteCount() == kDepth);

  const auto start = std::chrono::steady_clock::now();
  ops->CancelAsyncIO();
  ops->WaitForPendingWrites();
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

  INFO("drained in " << waited << "ms; " << cancelled.load() << " cancelled, "
                     << succeeded.load() << " completed, " << failed.load()
                     << " failed");
  CHECK(ops->GetPendingWriteCount() == 0);
  CHECK(cancelled.load() + succeeded.load() == kDepth);
  CHECK(failed.load() == 0);

  ops->Close();
}

TEST_CASE("A write waiting for a queue slot answers a cancel",
          "[file-ops][faulty][slow]") {
  // The extract thread blocks inside AsyncWriteSequential whenever the queue
  // is full, which on a slow card is most of the time. A cancel arriving from
  // the UI thread while it is parked there has to be noticed in that wait, and
  // the write reported as cancelled rather than as an error -- and it has to
  // be reported at all, or the extract loop sees a success for data that was
  // never queued and moves its cursor past it.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  if (!rpi_imager::testing::asyncWritesOverlap())
    SKIP("this platform writes serially, so the delays would not overlap");

  FaultyDevice device(64, FaultyDevice::SlowWrites{6000});
  if (!device.isReady())
    SKIP("the delaying device could not be created (dm-delay?)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no queue to fill");

  constexpr int kDepth = 4;
  REQUIRE(ops->SetAsyncQueueDepth(kDepth));

  constexpr std::size_t kChunk = 64u * 1024;
  auto buffer = alignedBuffer(kChunk, 0x77);
  REQUIRE(buffer);

  for (int i = 0; i < kDepth; ++i)
    REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk, nullptr) ==
            FileError::kSuccess);
  REQUIRE(ops->GetPendingWriteCount() == kDepth);

  // The extract thread: parks in the queue-full wait, because nothing can
  // complete for another six seconds.
  std::atomic<int> callbacks{0};
  std::atomic<int> viaCallback{-1};
  std::atomic<int> returned{-1};
  std::thread writer([&] {
    returned.store(static_cast<int>(ops->AsyncWriteSequential(
        buffer.get(), kChunk, [&](FileError e, std::size_t) {
          ++callbacks;
          viaCallback.store(static_cast<int>(e));
        })));
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto cancelAt = std::chrono::steady_clock::now();
  ops->CancelAsyncIO();
  writer.join();
  const auto answered = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - cancelAt)
                            .count();

  INFO("the parked write was answered " << answered << "ms after the cancel");
  CHECK(callbacks.load() == 1);
  CHECK(viaCallback.load() == static_cast<int>(FileError::kCancelled));
  CHECK(returned.load() == static_cast<int>(FileError::kCancelled));
  // Long before the card would have freed a slot on its own.
  CHECK(answered < 3000);

  ops->WaitForPendingWrites();
  ops->Close();
}

TEST_CASE("A drain that never makes progress gives up", "[file-ops]") {
  // The watchdog calls DrainAndSwitchToSync when a write has stopped moving,
  // and it deliberately does not consume the completion queue itself -- the
  // extract thread is the only thread allowed to. So if the extract thread is
  // the thing that is stuck, nothing drains, and the drain has to come back
  // and say so. Waiting indefinitely would leave the user watching a progress
  // bar that has stopped, with a Cancel button whose handler is behind this
  // call.
  auto ops = FileOperations::Create();
  if (!ops->IsAsyncIOSupported())
    SKIP("async I/O is not available in this build (no liburing, or too old)");

  const std::string path = makeImage("drain-stalled.img");
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->SetAsyncQueueDepth(8));

  constexpr std::size_t kChunk = 32 * 1024;
  const auto block = pattern(kChunk, 0x2A);
  for (int i = 0; i < 4; ++i)
    REQUIRE(ops->AsyncWriteSequential(block.data(), kChunk, nullptr) ==
            FileError::kSuccess);

  // Not a fixed count. io_uring reaps completions inside the write path, so
  // on a loaded machine one of the four can already be done by the time the
  // drain starts -- which failed this as a flake, roughly one run in twenty
  // under load. What the drain needs is something still outstanding.
  const int pending = ops->GetPendingWriteCount();
  INFO("pending when the drain began: " << pending);
  REQUIRE(pending > 0);

  // Nobody consuming: the pending count cannot fall further on its own.
  const auto start = std::chrono::steady_clock::now();
  const bool drained = ops->DrainAndSwitchToSync(1);
  const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

  INFO("the drain returned after " << waited << "ms");
  CHECK_FALSE(drained);
  CHECK(waited < 5000);

  // And it still switched to sync mode on the way in, so whatever the caller
  // does next is not queued behind the writes it could not drain.
  CHECK(ops->IsInSyncFallbackMode());
  ops->Close();
}

TEST_CASE("A drain waits while the queue is still going down",
          "[file-ops][faulty][slow]") {
  // The other half of the same call. Progress resets the stall timer, so a
  // card that is merely slow gets as long as it needs -- the timeout is for a
  // card that has stopped, not for one that is taking its time. Getting the
  // two confused would abandon the async queue on exactly the cards that
  // most need it drained in order.
  //
  // The completions are staggered so the pending count is seen falling rather
  // than jumping straight to zero, which is what the progress arm is for.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  if (!rpi_imager::testing::asyncWritesOverlap())
    SKIP("this platform writes serially, so the delays would not overlap");

  constexpr int kDelayMs = 1500;
  FaultyDevice device(64, FaultyDevice::SlowWrites{kDelayMs});
  if (!device.isReady())
    SKIP("the delaying device could not be created (dm-delay?)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no queue to drain");
  REQUIRE(ops->SetAsyncQueueDepth(8));

  constexpr std::size_t kChunk = 64u * 1024;
  auto buffer = alignedBuffer(kChunk, 0x5E);
  REQUIRE(buffer);

  // Half a second apart, so the device answers them half a second apart:
  // completions at roughly 1.5s, 2.0s and 2.5s from here.
  for (int i = 0; i < 3; ++i) {
    REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk, nullptr) ==
            FileError::kSuccess);
    if (i < 2)
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
  REQUIRE(ops->GetPendingWriteCount() == 3);

  // The extract thread, doing the draining the drain call is waiting on.
  std::thread consumer([&ops] { ops->WaitForPendingWrites(); });

  // A one-second stall timeout against a drain that takes a second and a half.
  // Each completion is inside the second, so the timer keeps being reset; a
  // drain that timed the whole thing instead would have given up half a second
  // before the last write landed.
  const bool drained = ops->DrainAndSwitchToSync(1);
  consumer.join();

  CHECK(drained);
  CHECK(ops->IsInSyncFallbackMode());
  CHECK(ops->GetPendingWriteCount() == 0);

  ops->Close();
}

TEST_CASE("A card that stops answering has the write abandoned, not waited on",
          "[file-ops][faulty][slow]") {
  // The last line of defence. Sync fallback is already the recovery path --
  // the async queue was not draining, so the queued writes are replayed one at
  // a time through pwrite -- and if the card does not answer those either,
  // there is nothing left to fall back to. Each replayed write gets
  // kSyncWriteTimeoutSeconds and no more, because pwrite to a card that has
  // stopped responding does not return: without the timeout the writer thread
  // is gone for good, progress stops at whatever percentage it reached, and
  // Cancel cannot get it back.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  if (!rpi_imager::testing::asyncWritesOverlap())
    SKIP("this platform writes serially, so the delays would not overlap");

  // Longer than kSyncWriteTimeoutSeconds, so the timeout is what ends the
  // write; not much longer, so the device is free again shortly afterwards.
  FaultyDevice device(64, FaultyDevice::SlowWrites{33000});
  if (!device.isReady())
    SKIP("the delaying device could not be created (dm-delay?)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async queue to fall back from");
  REQUIRE(ops->SetAsyncQueueDepth(8));

  constexpr std::size_t kChunk = 64u * 1024;
  auto buffer = alignedBuffer(kChunk, 0x9D);
  REQUIRE(buffer);

  for (int i = 0; i < 2; ++i)
    REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk, nullptr) ==
            FileError::kSuccess);
  REQUIRE(ops->GetPendingWriteCount() == 2);

  const auto start = std::chrono::steady_clock::now();
  const FileError result = ops->AttemptSyncFallback();
  const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();

  INFO("the fallback gave up after " << waited << "s");
  CHECK(result == FileError::kTimeout);

  // Bounded by the timeout rather than by the card: it came back well before
  // the device would have answered on its own.
  CHECK(waited < 33);

  // And the handle is shut, so nothing writes into the void afterwards.
  CHECK_FALSE(ops->IsOpen());
  CHECK(ops->WriteSequential(buffer.get(), kChunk) == FileError::kOpenError);
}

TEST_CASE("A buffered write that only the flush finds out about is reported",
          "[file-ops][faulty]") {
  // Direct I/O is the normal path and it fails at the write. Buffered is the
  // fallback -- taken whenever O_DIRECT is refused, and by the customisation
  // step, which reopens without it -- and there the write returns success as
  // soon as the bytes reach the page cache. Nothing has touched the card yet.
  // The card's answer arrives at the fsync, and if that answer is thrown away
  // the writer reports a finished, flushed image over data the card refused.
  using rpi_imager::testing::canInjectFaults;
  using rpi_imager::testing::FaultyDevice;

  if (!canInjectFaults())
    SKIP("fault injection is unavailable (needs passwordless sudo on Linux, the ctest-inserted interposer on macOS)");

  FaultyDevice device(64, 8);
  if (!device.isReady())
    SKIP("the faulty device could not be created");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(device.path().toStdString()) == FileError::kSuccess);
  REQUIRE(ops->SetDirectIOEnabled(false) == FileError::kSuccess);
  REQUIRE_FALSE(ops->IsDirectIOEnabled());

  constexpr std::size_t kChunk = 1u << 20;
  auto buffer = alignedBuffer(kChunk, 0x6B);
  REQUIRE(buffer);

  // Past the 8MB boundary, where the mapping returns EIO -- but buffered, so
  // the write itself is only a copy into the page cache and succeeds.
  REQUIRE(ops->Seek(16ull << 20) == FileError::kSuccess);
  REQUIRE(ops->WriteSequential(buffer.get(), kChunk) == FileError::kSuccess);

  // The card's answer, arriving late.
  const FileError synced = ops->ForceSync();
  INFO("errno was " << ops->GetLastErrorCode());
  CHECK(synced == FileError::kSyncError);
  CHECK(ops->GetLastErrorCode() == EIO);
  CHECK(ops->ClassifyLastWriteError() ==
        rpi_imager::WriteErrorClass::kIoDeviceError);

  ops->Close();
}

TEST_CASE("A host with no io_uring writes anyway", "[file-ops]") {
  // io_uring is not a given. An older kernel does not have it; a container
  // with a restrictive seccomp profile blocks the syscall; some distributions
  // ship it switched off. On any of those the ring fails to initialise in the
  // constructor, and every async entry point has to degrade to a synchronous
  // write rather than refuse -- the caller does not ask whether async is
  // available before handing over a buffer.
  struct rlimit original {};
  REQUIRE(::getrlimit(RLIMIT_NOFILE, &original) == 0);

  std::unique_ptr<FileOperations> ops;
  {
    struct rlimit tight = original;
    // Three: stdin, stdout and stderr are already open, so every new
    // descriptor would be numbered at or above the limit and none can be
    // allocated. Anything higher leaves a gap for io_uring_setup to land in.
    tight.rlim_cur = 3;
    if (::setrlimit(RLIMIT_NOFILE, &tight) != 0)
      SKIP("cannot lower RLIMIT_NOFILE on this host");
    ops = FileOperations::Create();
    REQUIRE(::setrlimit(RLIMIT_NOFILE, &original) == 0);
  }
  REQUIRE(ops != nullptr);

  if (ops->IsAsyncIOSupported())
    SKIP("io_uring initialised despite the descriptor limit");

  // Asking for a queue is answered honestly...
  CHECK_FALSE(ops->SetAsyncQueueDepth(16));
  // ...and cancelling one that was never created is not a crash.
  CHECK_NOTHROW(ops->CancelAsyncIO());
  CHECK(ops->WaitForPendingWrites() == FileError::kSuccess);

  // ...and the write still happens, synchronously, callback and all.
  const std::string path = makeImage("no-io-uring.img");
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

  const auto expected = pattern(16384, 0x4F);
  int calls = 0;
  std::size_t reported = 0;
  REQUIRE(ops->AsyncWriteSequential(expected.data(), expected.size(),
                                    [&](FileError e, std::size_t n) {
                                      ++calls;
                                      if (e == FileError::kSuccess) reported = n;
                                    }) == FileError::kSuccess);
  CHECK(calls == 1);
  CHECK(reported == expected.size());
  CHECK(ops->GetPendingWriteCount() == 0);

  ops->Close();
  CHECK(readBack(path, 0, expected.size()) == expected);
}

TEST_CASE("A write the card only half takes is an error, not a write",
          "[file-ops][loop]") {
  // The card accepting fewer bytes than it was handed is its own failure mode,
  // distinct from refusing them: the completion comes back positive, so
  // nothing about it looks like an error, and the count is simply smaller than
  // asked for. It happens at the end of a device that is smaller than the
  // image thinks it is -- a counterfeit card, or a declared size that does not
  // match the media.
  constexpr std::uint64_t kDeviceSize = 16u * 1024 * 1024 + 32u * 1024;
  constexpr std::size_t kChunk = 64u * 1024;

  const std::string backing = makeImage("half-taken.img", kDeviceSize);
  LoopDevice loop(backing);
  if (!loop.valid())
    SKIP("no loopback device available (needs CAP_SYS_ADMIN or passwordless sudo)");

  auto ops = FileOperations::Create();
  REQUIRE(ops->OpenDevice(loop.path()) == FileError::kSuccess);
  if (!ops->IsAsyncIOSupported())
    SKIP("io_uring is not available, so there is no async completion to read");
  REQUIRE(ops->SetAsyncQueueDepth(8));

  auto buffer = alignedBuffer(kChunk, 0xE1);
  REQUIRE(buffer);

  // Half of this fits. The other half is past the end of the device.
  REQUIRE(ops->Seek(16ull * 1024 * 1024) == FileError::kSuccess);

  int calls = 0;
  FileError reported = FileError::kSuccess;
  std::size_t reportedBytes = kChunk;
  REQUIRE(ops->AsyncWriteSequential(buffer.get(), kChunk,
                                    [&](FileError e, std::size_t n) {
                                      ++calls;
                                      reported = e;
                                      reportedBytes = n;
                                    }) == FileError::kSuccess);

  const FileError drained = ops->WaitForPendingWrites();

  CHECK(calls == 1);
  CHECK(reported == FileError::kWriteError);
  // Not "32768 written" either: a partial count would be taken as progress.
  CHECK(reportedBytes == 0);
  CHECK(drained == FileError::kWriteError);

  ops->Close();
}

// ---------------------------------------------------------------------------
// What the base class still implements for everyone.
//
// It used to carry a whole spare async implementation for a platform that
// provided none. All three platforms override every one of those, so the
// defaults were reachable only by a platform forgetting to write one -- and
// what it would then inherit was a WaitForPendingWrites() answering kSuccess
// for writes still in flight. They are pure virtual now.
//
// Two are still the base class's own, because nothing overrides them and
// every platform runs them. This is one; the other, IsInSyncFallbackMode, is
// covered by the sync-fallback cases above.
// ---------------------------------------------------------------------------

TEST_CASE("Statistics from a write that never happened are zero, not stale",
          "[file-ops]") {
  // Shown to the user at the end of a write, and read straight out of the
  // shared latency counters rather than from anything the platform keeps. With
  // no async writes recorded there is nothing to report, and the caller's
  // variables have to be overwritten rather than left as they were -- a stale
  // figure here is a number in front of the user that means nothing.
  auto ops = FileOperations::Create();
  REQUIRE(ops != nullptr);

  std::uint32_t wall = 1, count = 1, minUs = 1, maxUs = 1, avgUs = 1;
  ops->GetAsyncIOStats(wall, count, minUs, maxUs, avgUs);
  CHECK(count == 0);
  CHECK(wall == 0);
  CHECK(minUs == 0);
  CHECK(maxUs == 0);
  CHECK(avgUs == 0);
}

// posix_fadvise is advice, and advice the kernel refuses is not an error the
// write has to stop for -- but it is worth saying, because losing read-ahead
// on a verification pass turns a two-minute read into a much longer one. A
// FIFO is the case the kernel refuses: it has no page cache to advise about,
// so both calls come back ESPIPE.
TEST_CASE("Advice the kernel will not take is logged, not fatal", "[file-ops]") {
  const std::string path = scratch().file("advice.fifo");
  ::unlink(path.c_str());
  REQUIRE(::mkfifo(path.c_str(), 0600) == 0);

  auto ops = FileOperations::Create();
  // O_RDWR on a FIFO does not block for a reader on Linux.
  REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);
  REQUIRE(ops->IsOpen());

  // Asserted, not assumed: if a kernel ever starts accepting this the test
  // stops exercising the arm it is named for, and should say so.
  CHECK(::posix_fadvise(ops->GetHandle(), 0, 4096, POSIX_FADV_DONTNEED) != 0);

  CHECK_NOTHROW(ops->PrepareForSequentialRead(0, 4096));

  CHECK(ops->Close() == FileError::kSuccess);
  ::unlink(path.c_str());
}

// The two places that ask the kernel about a handle it no longer has. Both
// arms exist because a card pulled mid-write leaves exactly this state, and
// both had only ever been reached with a handle that was still good.
//
// The descriptor is closed behind the object's back rather than by pulling
// hardware. The window is one statement wide on purpose: a descriptor number
// that has been freed is a number the next open() in this process may be
// given.
TEST_CASE("A handle closed underneath us is reported, not assumed good",
          "[file-ops]") {
  SECTION("asking for the size") {
    const std::string path = makeImage("stale-size.img");
    auto ops = FileOperations::Create();
    REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

    const int handle = ops->GetHandle();
    REQUIRE(handle >= 0);
    REQUIRE(::close(handle) == 0);

    std::uint64_t size = 12345;
    CHECK(ops->GetSize(size) == FileError::kSizeError);
    CHECK(ops->GetLastErrorCode() == EBADF);
    // Left as it was rather than filled in with something invented.
    CHECK(size == 12345);
  }

  SECTION("closing it again") {
    const std::string path = makeImage("stale-close.img");
    auto ops = FileOperations::Create();
    REQUIRE(ops->OpenDevice(path) == FileError::kSuccess);

    const int handle = ops->GetHandle();
    REQUIRE(handle >= 0);
    REQUIRE(::close(handle) == 0);

    CHECK(ops->Close() == FileError::kCloseError);
    // Still let go of it: a second Close() must not try the same number
    // again, by then owned by something else.
    CHECK_FALSE(ops->IsOpen());
    CHECK(ops->Close() == FileError::kSuccess);
  }
}
