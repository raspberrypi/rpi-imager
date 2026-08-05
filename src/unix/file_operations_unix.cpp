/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 */

#include "file_operations_unix.h"
#include "../file_operations.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <thread>
#include <functional>
#include "../timeout_utils.h"

using rpi_imager::TimeoutResult;
using rpi_imager::TimeoutConfig;
using rpi_imager::runWithTimeout;
using rpi_imager::TimeoutDefaults::kSyncWriteTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kSyncFsyncTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kMinAsyncQueueDepth;
using rpi_imager::TimeoutDefaults::kHighLatencyThresholdMs;
using rpi_imager::TimeoutDefaults::kAsyncFirstCompletionTimeoutMs;

namespace rpi_imager {

FileOperations::DeviceIOLimits QueryPlatformDeviceIOLimits(const std::string&);

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

UnixFileOperations::UnixFileOperations()
    : fd_(-1), last_error_code_(0), using_direct_io_(false), direct_io_attempted_(false),
      async_queue_depth_(1), pending_writes_(0), cancelled_(false), first_async_error_(FileError::kSuccess),
      async_write_offset_(0), next_write_id_(0) {
}

UnixFileOperations::~UnixFileOperations() {
    Close();
}

FileError UnixFileOperations::OpenDevice(const std::string& path) {
    // Reset direct I/O tracking for new device
    direct_io_attempted_ = false;

    // Use O_DIRECT for block devices to bypass the page cache
    int flags = O_RDWR;
    bool isBlockDevice = IsBlockDevicePath(path);

    if (isBlockDevice) {
        flags |= O_DIRECT;
        using_direct_io_ = true;
        direct_io_attempted_ = true;
    }

    FileError result = OpenInternal(path.c_str(), flags);

    // If O_DIRECT fails, fall back to regular I/O
    if (result != FileError::kSuccess && isBlockDevice && using_direct_io_) {
        using_direct_io_ = false;
        result = OpenInternal(path.c_str(), O_RDWR);
    }

    // Reset async state for new file
    async_write_offset_ = 0;
    first_async_error_ = FileError::kSuccess;
    cancelled_.store(false);
    write_latency_stats_.reset();

    if (result == FileError::kSuccess) {
        device_io_limits_ = QueryPlatformDeviceIOLimits(current_path_);
        if (device_io_limits_.max_transfer_bytes > 0 || device_io_limits_.suggested_queue_depth > 0) {
            std::ostringstream oss;
            oss << "Device I/O limits: max_transfer=" << device_io_limits_.max_transfer_bytes
                << " bytes, suggested_queue_depth=" << device_io_limits_.suggested_queue_depth;
            Log(oss.str());
        }
    }

    return result;
}

FileError UnixFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
    FileError result = OpenInternal(path.c_str(),
                                    O_CREAT | O_RDWR | O_TRUNC,
                                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (result != FileError::kSuccess) {
        return result;
    }

    if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
        Close();
        return FileError::kSizeError;
    }

    return FileError::kSuccess;
}

FileError UnixFileOperations::WriteAtOffset(
    std::uint64_t offset,
    const std::uint8_t* data,
    std::size_t size) {

    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1) {
        return FileError::kSeekError;
    }

    std::size_t bytes_written = 0;
    while (bytes_written < size) {
        ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
        if (result <= 0) {
            return FileError::kWriteError;
        }
        bytes_written += static_cast<std::size_t>(result);
    }

    return FileError::kSuccess;
}

FileError UnixFileOperations::GetSize(std::uint64_t& size) {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    struct stat st;
    if (fstat(fd_, &st) != 0) {
        last_error_code_ = errno;
        return FileError::kSizeError;
    }

    // Platform-specific handling for block devices
    if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
        return GetDeviceSize(size);
    }

    size = static_cast<std::uint64_t>(st.st_size);
    return FileError::kSuccess;
}

FileError UnixFileOperations::Close() {
    WaitForPendingWrites();

    if (fd_ >= 0) {
        if (close(fd_) != 0) {
            fd_ = -1;
            using_direct_io_ = false;
            return FileError::kCloseError;
        }
        fd_ = -1;
    }
    current_path_.clear();
    using_direct_io_ = false;
    async_write_offset_ = 0;
    return FileError::kSuccess;
}

bool UnixFileOperations::IsOpen() const {
    return fd_ >= 0;
}

FileError UnixFileOperations::SetDirectIOEnabled(bool enabled) {
    if (!IsOpen() || current_path_.empty()) {
        return FileError::kOpenError;
    }

    if (using_direct_io_ == enabled) {
        return FileError::kSuccess;
    }

    // Save current position before reopening
    off_t currentPos = lseek(fd_, 0, SEEK_CUR);
    std::string savedPath = current_path_;

    close(fd_);
    fd_ = -1;

    int flags = O_RDWR;
    if (enabled && IsBlockDevicePath(savedPath)) {
        flags |= O_DIRECT;
    }

    FileError result = OpenInternal(savedPath.c_str(), flags);
    if (result != FileError::kSuccess) {
        if (enabled) {
            result = OpenInternal(savedPath.c_str(), O_RDWR);
            using_direct_io_ = false;
            Log("Failed to enable O_DIRECT, reopened without it");
        }
        if (result != FileError::kSuccess) {
            return result;
        }
    } else {
        using_direct_io_ = enabled;
    }

    if (currentPos > 0) {
        lseek(fd_, currentPos, SEEK_SET);
    }

    std::ostringstream oss;
    oss << "O_DIRECT " << (using_direct_io_ ? "enabled" : "disabled");
    Log(oss.str());

    return FileError::kSuccess;
}

FileError UnixFileOperations::OpenInternal(const char* path, int flags, mode_t mode) {
    Close();

    fd_ = open(path, flags, mode);
    if (fd_ < 0) {
        last_error_code_ = errno;
        return FileError::kOpenError;
    }

    current_path_ = path;
    last_error_code_ = 0;
    return FileError::kSuccess;
}

FileError UnixFileOperations::WriteSequential(const std::uint8_t* data, std::size_t size) {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    std::size_t bytes_written = 0;
    while (bytes_written < size) {
        ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
        if (result <= 0) {
            if (result == 0 || errno != EINTR) {
                last_error_code_ = errno;
                return FileError::kWriteError;
            }
            continue;
        }
        bytes_written += static_cast<std::size_t>(result);
    }

    last_error_code_ = 0;

    // Update async_write_offset_ so Tell() returns correct position
    async_write_offset_ += size;

    return FileError::kSuccess;
}

FileError UnixFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    ssize_t result = read(fd_, data, size);
    if (result < 0) {
        bytes_read = 0;
        return FileError::kReadError;
    }

    bytes_read = static_cast<std::size_t>(result);
    return FileError::kSuccess;
}

FileError UnixFileOperations::Seek(std::uint64_t position) {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    // Wait for pending async writes before seeking
    WaitForPendingWrites();

    if (lseek(fd_, static_cast<off_t>(position), SEEK_SET) == -1) {
        return FileError::kSeekError;
    }

    // Also update async write offset
    async_write_offset_ = position;

    return FileError::kSuccess;
}

std::uint64_t UnixFileOperations::Tell() const {
    if (!IsOpen()) {
        return 0;
    }

    // If async I/O has been used, return the async write offset
    if (async_write_offset_ > 0) {
        return async_write_offset_;
    }

    off_t pos = lseek(fd_, 0, SEEK_CUR);
    return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
}

FileError UnixFileOperations::ForceSync() {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    WaitForPendingWrites();

    if (fsync(fd_) != 0) {
        return FileError::kSyncError;
    }

    return FileError::kSuccess;
}

FileError UnixFileOperations::Flush() {
    if (!IsOpen()) {
        return FileError::kOpenError;
    }

    WaitForPendingWrites();

    if (fdatasync(fd_) != 0) {
        return FileError::kFlushError;
    }

    return FileError::kSuccess;
}

void UnixFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
    if (!IsOpen()) {
        return;
    }

    // Invalidate cache and set up read-ahead
    int ret = posix_fadvise(fd_, static_cast<off_t>(offset), static_cast<off_t>(length), POSIX_FADV_DONTNEED);
    if (ret != 0) {
        std::ostringstream oss;
        oss << "Warning: posix_fadvise(DONTNEED) failed: " << ret;
        Log(oss.str());
    }

    ret = posix_fadvise(fd_, static_cast<off_t>(offset), static_cast<off_t>(length), POSIX_FADV_SEQUENTIAL);
    if (ret != 0) {
        std::ostringstream oss;
        oss << "Warning: posix_fadvise(SEQUENTIAL) failed: " << ret;
        Log(oss.str());
    }
}

int UnixFileOperations::GetHandle() const {
    return fd_;
}

int UnixFileOperations::GetLastErrorCode() const {
    return last_error_code_;
}


FileError UnixFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size,
                                                    AsyncWriteCallback callback) {
    // Default: fall back to sync write
    FileError result = WriteSequential(data, size);
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
}

void UnixFileOperations::PollAsyncCompletions() {
    // Intentionally a no-op on Linux.
    //
    // Unlike Windows IOCP, io_uring's CQ is not thread-safe for multiple
    // consumers. The extract thread is the sole CQ consumer — it polls via
    // ProcessCompletions() inside AsyncWriteSequential() and
    // WaitForPendingWrites(). External callers (watchdog timer, download
    // thread's _updateBottleneckState) must not touch the CQ directly, as
    // concurrent peek/cqe_seen calls cause double-processing or skipped
    // completions.
    //
    // This is safe because the extract thread's blocking wait uses 100ms
    // timeouts with cancellation checks, so completions are always drained
    // promptly without external prodding.
    //
    // On FreeBSD, ProcessCompletions() polls for completions too, so
    // no-op there too.
}

FileError UnixFileOperations::WaitForPendingWrites() {
  // Wait for pending writes to complete or be cancelled.
  //
  // DESIGN: Stall detection is handled by WriteProgressWatchdog at the ImageWriter level.
  // This function simply waits, responding to cancellation. We keep a very long safety-net
  // timeout (5 minutes) only as emergency fallback if cancellation somehow fails.
  constexpr int kEmergencyTimeoutSeconds = 300;  // 5 minute emergency fallback
  auto startTime = std::chrono::steady_clock::now();
  int lastLogSecond = 0;

  while (pending_writes_.load() > 0) {
    ProcessCompletions(true);

    // Emergency safety-net: if we've been waiting 5 minutes, something is very wrong
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    int elapsedSeconds = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());

    if (elapsedSeconds >= kEmergencyTimeoutSeconds) {
      int remaining = pending_writes_.load();
      Log("WaitForPendingWrites: EMERGENCY timeout after " + std::to_string(elapsedSeconds) +
          "s with " + std::to_string(remaining) + " writes still pending - forcing sync fallback");
      return AttemptSyncFallback();
    }

    // Log progress every 30 seconds (informational only, not stall detection)
    if (elapsedSeconds >= 30 && elapsedSeconds % 30 == 0 && elapsedSeconds != lastLogSecond) {
      lastLogSecond = elapsedSeconds;
      Log("WaitForPendingWrites: " + std::to_string(pending_writes_.load()) +
          " writes pending after " + std::to_string(elapsedSeconds) + "s");
    }
  }

  return first_async_error_;
}

void UnixFileOperations::CancelAsyncIO() {
    cancelled_.store(true);
}

std::vector<FileOperations::PendingWriteInfo> UnixFileOperations::GetPendingWritesSorted() const {
      std::vector<PendingWriteInfo> result;
    std::lock_guard<std::mutex> lock(pending_mutex_);
    result.reserve(pending_callbacks_.size());

    for (const auto& [write_id, pw] : pending_callbacks_) {
        result.push_back(PendingWriteInfo{pw.offset, pw.data, pw.size, pw.callback});
    }

    // Sort by offset for sequential replay
    std::sort(result.begin(), result.end(),
                [](const PendingWriteInfo& a, const PendingWriteInfo& b) {
                return a.offset < b.offset;
                });

    return result;
}

FileError UnixFileOperations::AttemptSyncFallback() {
    sync_fallback_mode_ = true;
    return FileError::kSuccess;
}

bool UnixFileOperations::DrainAndSwitchToSync(int stallTimeoutSeconds) {
  // First, prevent new async writes by switching to sync mode
  sync_fallback_mode_ = true;

  int pending = pending_writes_.load();
  if (pending == 0) {
    Log("DrainAndSwitchToSync: No pending writes, switching to sync mode");
    return true;
  }

  Log("DrainAndSwitchToSync: Waiting for " + std::to_string(pending) +
      " pending writes to drain (stall timeout: " + std::to_string(stallTimeoutSeconds) + "s per completion)");

  auto startTime = std::chrono::steady_clock::now();
  auto lastProgressTime = startTime;
  int lastPending = pending;

  // Wait for the extract thread to drain pending writes.
  // Once sync_fallback_mode_ is set above, the extract thread will stop
  // submitting new writes and drain the remaining ones via its own
  // ProcessCompletions calls in AsyncWriteSequential/WaitForPendingWrites.
  while (pending_writes_.load() > 0) {
    int currentPending = pending_writes_.load();
    auto now = std::chrono::steady_clock::now();

    if (currentPending < lastPending) {
      // Progress! Reset the stall timer
      Log("DrainAndSwitchToSync: Draining... " + std::to_string(currentPending) + " remaining");
      lastPending = currentPending;
      lastProgressTime = now;
    } else {
      // No progress - check stall timeout
      auto stallDuration = std::chrono::duration_cast<std::chrono::seconds>(now - lastProgressTime);
      if (stallDuration.count() >= stallTimeoutSeconds) {
        int remaining = pending_writes_.load();
        auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
        Log("DrainAndSwitchToSync: Stalled - no completions for " +
            std::to_string(stallTimeoutSeconds) + "s, " + std::to_string(remaining) +
            " writes still pending after " + std::to_string(totalElapsed.count()) + "s total");
        return false;
      }
    }

    // Brief sleep to avoid spinning — the extract thread is doing the actual draining
    usleep(100000);  // 100ms
  }

  auto elapsed = std::chrono::steady_clock::now() - startTime;
  int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

  Log("DrainAndSwitchToSync: Successfully drained all writes in " +
      std::to_string(elapsedMs) + "ms - now in sync mode");
  return true;
}

// Platform-specific device size query - to be implemented by Linux/FreeBSD
FileError UnixFileOperations::GetDeviceSize(std::uint64_t& size) {
    (void)size;
    return FileError::kSizeError;  // Default: not implemented
}

} // namespace rpi_imager
