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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
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

// Run a command, capturing stdout. Returns exit status, or -1 on failure.
int runCapture(const char* path, const std::vector<const char*>& argv,
               std::string* out) {
  int pipefd[2];
  if (::pipe(pipefd) != 0) return -1;

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    return -1;
  }
  if (pid == 0) {
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::close(pipefd[1]);
    int devnull = ::open("/dev/null", O_WRONLY);
    if (devnull >= 0) { ::dup2(devnull, STDERR_FILENO); ::close(devnull); }
    ::execv(path, const_cast<char* const*>(argv.data()));
    ::_exit(127);
  }

  ::close(pipefd[1]);
  char buf[256] = {};
  ssize_t n = ::read(pipefd[0], buf, sizeof(buf) - 1);
  ::close(pipefd[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);
  if (out && n > 0) {
    out->assign(buf, static_cast<std::size_t>(n));
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A loopback device backed by one of our scratch images, detached on
// destruction. Yields an empty path when the host will not give us one, which
// is the normal case in CI and in containers: no CAP_SYS_ADMIN, or no
// passwordless sudo. Callers skip rather than fail.
class LoopDevice {
 public:
  explicit LoopDevice(const std::string& backingFile) {
    std::string out;
    // -P so the partition table inside the image is scanned; several of the
    // things worth testing live in partitions, not at the raw offset 0.
    if (losetup({"--find", "--show", "-P", backingFile.c_str()}, &out) != 0) return;

    // Trim, then insist on exactly /dev/loop<digits> before we ever hand this
    // string to a detach command.
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    if (!isLoopPath(out)) return;

    device_ = out;
  }

  ~LoopDevice() {
    if (device_.empty()) return;
    std::string ignored;
    losetup({"-d", device_.c_str()}, &ignored);
  }

  LoopDevice(const LoopDevice&) = delete;
  LoopDevice& operator=(const LoopDevice&) = delete;

  bool valid() const { return !device_.empty(); }
  const std::string& path() const { return device_; }

 private:
  // Run losetup, directly when we are already root and via sudo -n otherwise.
  // Going straight to losetup matters for rootful CI containers and VMs, where
  // sudo is frequently not installed at all; -n on the fallback means a host
  // that would prompt for a password fails immediately instead of hanging the
  // suite.
  static int losetup(std::vector<const char*> args, std::string* out) {
    std::vector<const char*> argv;
    const char* binary = nullptr;
    if (::geteuid() == 0) {
      binary = "/usr/sbin/losetup";
      argv.push_back("losetup");
    } else {
      binary = "/usr/bin/sudo";
      argv.insert(argv.end(), {"sudo", "-n", "losetup"});
    }
    argv.insert(argv.end(), args.begin(), args.end());
    argv.push_back(nullptr);
    return runCapture(binary, argv, out);
  }

  static bool isLoopPath(const std::string& s) {
    if (s.rfind("/dev/loop", 0) != 0) return false;
    const std::string digits = s.substr(9);
    if (digits.empty()) return false;
    return std::all_of(digits.begin(), digits.end(),
                       [](unsigned char c) { return c >= '0' && c <= '9'; });
  }

  std::string device_;
};

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
