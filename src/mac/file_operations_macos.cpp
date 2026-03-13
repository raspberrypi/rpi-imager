/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_macos.h"
#include "macfile.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <errno.h>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <functional>
#include "../timeout_utils.h"

using rpi_imager::TimeoutResult;
using rpi_imager::TimeoutConfig;
using rpi_imager::runWithTimeout;
using rpi_imager::TimeoutDefaults::kSyncWriteTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kSyncFsyncTimeoutSeconds;
using rpi_imager::TimeoutDefaults::kMinAsyncQueueDepth;

namespace rpi_imager {

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

MacOSFileOperations::MacOSFileOperations() 
    : fd_(-1), last_error_code_(0), using_direct_io_(false),
      async_queue_depth_(1), pending_writes_(0), cancelled_(false), first_async_error_(FileError::kSuccess),
      async_queue_(nullptr), queue_semaphore_(nullptr), async_write_offset_(0),
      next_write_id_(1) {
}

bool MacOSFileOperations::IsBlockDevicePath(const std::string& path) {
  // Check for block device paths (e.g., /dev/disk2, /dev/rdisk2)
  return (path.find("/dev/") == 0);
}

bool MacOSFileOperations::EnableDirectIO() {
  if (fd_ < 0) {
    return false;
  }
  
  // macOS uses F_NOCACHE to disable file caching (equivalent to O_DIRECT on Linux)
  // This provides direct I/O to bypass the unified buffer cache
  if (fcntl(fd_, F_NOCACHE, 1) == 0) {
    using_direct_io_ = true;
    FileOperationsLog("Enabled F_NOCACHE for direct I/O");
    return true;
  }
  
  std::ostringstream oss;
  oss << "Warning: Failed to enable F_NOCACHE, errno=" << errno;
  FileOperationsLog(oss.str());
  return false;
}

FileError MacOSFileOperations::SetDirectIOEnabled(bool enabled) {
  if (fd_ < 0) {
    return FileError::kOpenError;
  }
  
  // macOS uses F_NOCACHE to control caching
  // F_NOCACHE with value 1 enables direct I/O (no caching)
  // F_NOCACHE with value 0 disables direct I/O (normal caching)
  int value = enabled ? 1 : 0;
  
  if (fcntl(fd_, F_NOCACHE, value) == 0) {
    using_direct_io_ = enabled;
    std::ostringstream oss;
    oss << "F_NOCACHE " << (enabled ? "enabled" : "disabled");
    FileOperationsLog(oss.str());
    return FileError::kSuccess;
  }
  
  last_error_code_ = errno;
  std::ostringstream oss;
  oss << "Failed to " << (enabled ? "enable" : "disable") << " F_NOCACHE, errno=" << errno;
  FileOperationsLog(oss.str());
  return FileError::kOpenError;
}

MacOSFileOperations::~MacOSFileOperations() {
  Close();
  CleanupAsyncIO();
}

void MacOSFileOperations::InitAsyncIO() {
  if (async_queue_ == nullptr && async_queue_depth_ > 1) {
    async_queue_ = dispatch_queue_create("com.raspberrypi.imager.asyncio", DISPATCH_QUEUE_SERIAL);
    queue_semaphore_ = dispatch_semaphore_create(async_queue_depth_);
    async_write_offset_ = 0;
    pending_writes_.store(0);
    cancelled_.store(false);
    first_async_error_.store(FileError::kSuccess);
    Log("Initialized async I/O with queue depth " + std::to_string(async_queue_depth_));
  }
}

void MacOSFileOperations::CleanupAsyncIO() {
  // Wait for pending writes before cleanup
  WaitForPendingWrites();
  
  if (queue_semaphore_ != nullptr) {
    dispatch_release(queue_semaphore_);
    queue_semaphore_ = nullptr;
  }
  if (async_queue_ != nullptr) {
    dispatch_release(async_queue_);
    async_queue_ = nullptr;
  }
}

FileError MacOSFileOperations::OpenDevice(const std::string& path) {
  std::cout << "Opening macOS device: " << path << std::endl;
  
  bool isBlockDevice = IsBlockDevicePath(path);
  
  // For raw device access on macOS, we need to use the authorization mechanism
  // Similar to how MacFile::authOpen() works
  if (isBlockDevice) {
    std::cout << "Device path detected, using macOS authorization..." << std::endl;
    
    // Convert /dev/diskX to /dev/rdiskX for raw device access (like dd uses)
    // Raw devices (/dev/rdisk) have better buffering behavior than block devices (/dev/disk)
    std::string rawPath = path;
    size_t diskPos = rawPath.find("/dev/disk");
    if (diskPos != std::string::npos) {
      rawPath.replace(diskPos + 5, 4, "rdisk");  // Replace "disk" with "rdisk"
      std::cout << "Using raw device path: " << rawPath << " (instead of " << path << ")" << std::endl;
    }
    
    // Create a MacFile instance to handle authorization
    MacFile macfile;
    QByteArray devicePath = QByteArray::fromStdString(rawPath);
    
    auto authResult = macfile.authOpen(devicePath);
    if (authResult == MacFile::authOpenCancelled) {
      std::cout << "Authorization cancelled by user" << std::endl;
      return FileError::kOpenError;
    } else if (authResult == MacFile::authOpenError) {
      std::cout << "Authorization failed" << std::endl;
      return FileError::kOpenError;
    }
    
    // Get the file descriptor from the authorized MacFile
    fd_ = macfile.handle();
    if (fd_ < 0) {
      std::cout << "Failed to get file descriptor from MacFile" << std::endl;
      return FileError::kOpenError;
    }
    
    // Duplicate the file descriptor so we can manage it independently
    int duplicated_fd = dup(fd_);
    if (duplicated_fd < 0) {
      std::cout << "Failed to duplicate file descriptor" << std::endl;
      return FileError::kOpenError;
    }
    
    // Close the MacFile (this will close the original fd)
    macfile.close();
    
    // Use our duplicated fd
    fd_ = duplicated_fd;
    current_path_ = path;
    
    // Use buffered I/O for writes (like v1.9.6) - much faster!
    // Direct I/O (F_NOCACHE) will be enabled only for verification reads
    // in PrepareForSequentialRead() to ensure accurate verification.
    // This gives us: fast writes + accurate verification
    using_direct_io_ = false;
    
    std::cout << "Successfully opened device with authorization, fd=" << fd_ 
              << " (buffered I/O for writes, direct I/O for verification)" << std::endl;
    return FileError::kSuccess;
  }
  
  // For regular files, use standard POSIX open
  return OpenInternal(path.c_str(), O_RDWR);
}

FileError MacOSFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
  // First create/truncate the file
  FileError result = OpenInternal(path.c_str(), 
                                  O_CREAT | O_RDWR | O_TRUNC, 
                                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (result != FileError::kSuccess) {
    return result;
  }

  // Set the file size
  if (ftruncate(fd_, static_cast<off_t>(size)) != 0) {
    Close();
    return FileError::kSizeError;
  }

  return FileError::kSuccess;
}

FileError MacOSFileOperations::WriteAtOffset(
    std::uint64_t offset,
    const std::uint8_t* data,
    std::size_t size) {
  
  if (!IsOpen()) {
    std::cout << "WriteAtOffset: Device not open" << std::endl;
    return FileError::kOpenError;
  }

  if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1) {
    std::cout << "WriteAtOffset: lseek failed, offset=" << offset << ", errno=" << errno << std::endl;
    return FileError::kSeekError;
  }

  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
    if (result <= 0) {
      std::cout << "WriteAtOffset: write failed, bytes_written=" << bytes_written << ", errno=" << errno << std::endl;
      return FileError::kWriteError;
    }
    bytes_written += static_cast<std::size_t>(result);
  }

  std::cout << "WriteAtOffset: Successfully wrote " << bytes_written << " bytes at offset " << offset << std::endl;
  return FileError::kSuccess;
}

FileError MacOSFileOperations::GetSize(std::uint64_t& size) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  struct stat st;
  if (fstat(fd_, &st) != 0) {
    last_error_code_ = errno;
    return FileError::kSizeError;
  }

  // For block devices (like /dev/disk2, /dev/rdisk2), st_size is always 0.
  // We need to use DKIOCGETBLOCKCOUNT and DKIOCGETBLOCKSIZE ioctls to get the actual size.
  if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
    // On macOS, both block devices (/dev/disk*) and character devices (/dev/rdisk*) 
    // need special handling to get their size
    std::uint64_t block_count = 0;
    std::uint32_t block_size = 0;
    
    if (ioctl(fd_, DKIOCGETBLOCKCOUNT, &block_count) == -1) {
      last_error_code_ = errno;
      std::cout << "DKIOCGETBLOCKCOUNT ioctl failed, errno=" << errno << std::endl;
      return FileError::kSizeError;
    }
    
    if (ioctl(fd_, DKIOCGETBLOCKSIZE, &block_size) == -1) {
      last_error_code_ = errno;
      std::cout << "DKIOCGETBLOCKSIZE ioctl failed, errno=" << errno << std::endl;
      return FileError::kSizeError;
    }
    
    size = block_count * block_size;
    std::cout << "Block device size: " << size << " bytes (blocks=" << block_count 
              << ", block_size=" << block_size << ")" << std::endl;
    return FileError::kSuccess;
  }

  // For regular files, use st_size
  size = static_cast<std::uint64_t>(st.st_size);
  return FileError::kSuccess;
}

FileError MacOSFileOperations::Close() {
  // Wait for any pending async writes
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

bool MacOSFileOperations::IsOpen() const {
  return fd_ >= 0;
}

FileError MacOSFileOperations::OpenInternal(const char* path, int flags, mode_t mode) {
  // Close any existing file
  Close();

  fd_ = open(path, flags, mode);
  if (fd_ < 0) {
    last_error_code_ = errno;
    std::cout << "OpenInternal: Failed to open " << path << ", errno=" << errno << std::endl;
    return FileError::kOpenError;
  }

  current_path_ = path;
  last_error_code_ = 0;
  std::cout << "OpenInternal: Successfully opened " << path << ", fd=" << fd_ << std::endl;
  return FileError::kSuccess;
}

// Streaming I/O operations
FileError MacOSFileOperations::WriteSequential(const std::uint8_t* data, std::size_t size) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // With buffered I/O (no F_NOCACHE), let the kernel handle buffering.
  // Write the full size at once - kernel will buffer and write asynchronously.
  // This matches v1.9.6 behavior which was much faster.
  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
    if (result <= 0) {
      if (result == 0 || errno != EINTR) {
        last_error_code_ = errno;
        return FileError::kWriteError;
      }
      // EINTR - retry the write
      continue;
    }
    bytes_written += static_cast<std::size_t>(result);
  }

  last_error_code_ = 0;
  
  // Update async_write_offset_ so Tell() returns correct position
  // This is needed because Seek() sets async_write_offset_, and Tell()
  // uses it if > 0. Without this update, Tell() would return a stale value.
  async_write_offset_ += size;
  
  return FileError::kSuccess;
}

FileError MacOSFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
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

FileError MacOSFileOperations::Seek(std::uint64_t position) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Wait for pending async writes before seeking
  // This ensures all queued writes complete at their intended offsets
  WaitForPendingWrites();

  if (lseek(fd_, static_cast<off_t>(position), SEEK_SET) == -1) {
    return FileError::kSeekError;
  }

  // Update async write offset to match the new position
  // This is critical for subsequent AsyncWriteSequential calls
  async_write_offset_ = position;

  return FileError::kSuccess;
}

std::uint64_t MacOSFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
  }

  // If async I/O has been used, return the async write offset
  // (pwrite doesn't update the file descriptor position)
  if (async_write_offset_ > 0) {
    return async_write_offset_;
  }

  off_t pos = lseek(fd_, 0, SEEK_CUR);
  return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
}

FileError MacOSFileOperations::ForceSync() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Force filesystem sync using fsync - same logic as MacFile::forceSync()
  if (::fsync(fd_) != 0) {
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError MacOSFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // On macOS, use fsync for both flush and sync operations
  if (::fsync(fd_) != 0) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

void MacOSFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
  if (!IsOpen()) {
    return;
  }

  (void)offset;  // macOS hints are file-wide
  (void)length;

  std::cout << "PrepareForSequentialRead: fd=" << fd_ << " path=" << current_path_ << std::endl;

  // F_RDAHEAD: Enable read-ahead for sequential access
  // This tells the kernel to speculatively read data ahead of the current position
  int rdahead_result = fcntl(fd_, F_RDAHEAD, 1);
  std::cout << "  F_RDAHEAD result: " << rdahead_result << (rdahead_result == -1 ? " (FAILED)" : " (OK)") << std::endl;
  if (rdahead_result == -1) {
    std::ostringstream oss;
    oss << "Warning: fcntl(F_RDAHEAD) failed, errno: " << errno
        << " - sequential read performance may be suboptimal";
    Log(oss.str());
  }
  
  // F_NOCACHE: Bypass the unified buffer cache
  // Critical for verification - ensures we read from actual device, not cached writes
  int nocache_result = fcntl(fd_, F_NOCACHE, 1);
  std::cout << "  F_NOCACHE result: " << nocache_result << (nocache_result == -1 ? " (FAILED)" : " (OK)") << std::endl;
  if (nocache_result == -1) {
    std::ostringstream oss;
    oss << "Warning: fcntl(F_NOCACHE) failed, errno: " << errno
        << " - verification may read cached data";
    Log(oss.str());
  }
}

int MacOSFileOperations::GetHandle() const {
  return fd_;
}

int MacOSFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// ============= Async I/O Implementation (using GCD) =============

bool MacOSFileOperations::SetAsyncQueueDepth(int depth) {
  if (depth < 1) depth = 1;
  
  // Clean up existing async resources if changing depth
  if (async_queue_ != nullptr && depth != async_queue_depth_) {
    CleanupAsyncIO();
  }
  
  async_queue_depth_ = depth;
  
  if (depth > 1) {
    InitAsyncIO();
  }
  
  std::ostringstream oss;
  oss << "Async queue depth set to " << depth;
  Log(oss.str());
  
  return true;
}

FileError MacOSFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                                     AsyncWriteCallback callback) {
  if (fd_ < 0) {
    if (callback) callback(FileError::kOpenError, 0);
    return FileError::kOpenError;
  }
  
  // Check for cancellation
  if (cancelled_.load()) {
    if (callback) callback(FileError::kCancelled, 0);
    return FileError::kCancelled;
  }
  
  // If async not enabled (queue_depth == 1) or in sync fallback mode, use sync
  if (async_queue_depth_ <= 1 || async_queue_ == nullptr || sync_fallback_mode_) {
    FileError result = WriteSequential(data, size);
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
  }
  
  // Wait for a slot in the queue (checking for cancellation)
  // Note: Stall detection is handled by WriteProgressWatchdog at the ImageWriter level.
  // Here we just wait for a slot, with periodic cancellation checks via semaphore timeout.
  dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC);
  while (dispatch_semaphore_wait(queue_semaphore_, timeout) != 0) {
    if (cancelled_.load()) {
      if (callback) callback(FileError::kCancelled, 0);
      return FileError::kCancelled;
    }
    timeout = dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC);
  }
  
  // Check for cancellation after acquiring slot
  if (cancelled_.load()) {
    dispatch_semaphore_signal(queue_semaphore_);
    if (callback) callback(FileError::kCancelled, 0);
    return FileError::kCancelled;
  }
  
  // Check for previous errors
  if (first_async_error_.load() != FileError::kSuccess) {
    dispatch_semaphore_signal(queue_semaphore_);
    if (callback) callback(first_async_error_.load(), 0);
    return first_async_error_.load();
  }
  
  // Capture the offset for this write
  std::uint64_t write_offset = async_write_offset_;
  async_write_offset_ += size;
  
  // Record submit time for latency tracking (uses base class's thread-safe stats)
  auto submit_time = std::chrono::steady_clock::now();
  write_latency_stats_.recordSubmit();
  
  // Generate write ID and track pending write for potential sync fallback
  std::uint64_t write_id = next_write_id_++;
  {
    std::lock_guard<std::mutex> lock(pending_writes_mutex_);
    pending_writes_map_[write_id] = PendingAsyncWrite{write_offset, data, size, callback, submit_time};
  }
  
  pending_writes_.fetch_add(1);
  
  // Queue the async write using GCD
  // Note: We use pwrite to write at a specific offset, allowing concurrent writes
  // Capture stats pointer for block - the base class's write_latency_stats_ is thread-safe
  rpi_imager::WriteLatencyStats* stats = &write_latency_stats_;
  dispatch_async(async_queue_, ^{
    ssize_t written = pwrite(fd_, data, size, static_cast<off_t>(write_offset));
    
    // Record completion latency (thread-safe via atomic operations)
    stats->recordCompletion(submit_time);
    
    FileError result = FileError::kSuccess;
    if (written < 0 || static_cast<size_t>(written) != size) {
      result = FileError::kWriteError;
      // Store first error
      FileError expected = FileError::kSuccess;
      first_async_error_.compare_exchange_strong(expected, result);
    }
    
    // Remove from pending writes map
    {
      std::lock_guard<std::mutex> lock(pending_writes_mutex_);
      pending_writes_map_.erase(write_id);
    }
    
    // Free a slot in the queue for more writes
    dispatch_semaphore_signal(queue_semaphore_);
    
    // Notify caller if callback provided - BEFORE decrementing pending count
    // This ensures WaitForPendingWrites() blocks until callbacks complete
    if (callback) {
      callback(result, result == FileError::kSuccess ? size : 0);
    }
    
    // Decrement pending count AFTER callback completes
    // This is critical for proper synchronization with WaitForPendingWrites()
    pending_writes_.fetch_sub(1);
    
    // Wake up any waiters
    {
      std::lock_guard<std::mutex> lock(completion_mutex_);
      completion_cv_.notify_all();
    }
  });
  
  return FileError::kSuccess;
}

void MacOSFileOperations::PollAsyncCompletions() {
  // On macOS, dispatch queues handle completions automatically on background threads.
  // Callbacks are invoked asynchronously, so no explicit polling is needed.
  // This is a no-op on macOS.
}

void MacOSFileOperations::CancelAsyncIO() {
  // Set cancellation flag - this will cause AsyncWriteSequential to return
  // immediately with kCancelled, and will unblock any semaphore waits
  cancelled_.store(true);
  
  // Wake up WaitForPendingWrites if blocked
  {
    std::lock_guard<std::mutex> lock(completion_mutex_);
    completion_cv_.notify_all();
  }
  
  // Note: We can't cancel already-dispatched GCD blocks, but they will
  // complete relatively quickly. The key is unblocking the semaphore wait
  // in AsyncWriteSequential so the caller can respond to cancellation.
}

std::vector<FileOperations::PendingWriteInfo> MacOSFileOperations::GetPendingWritesSorted() const {
  std::vector<PendingWriteInfo> result;
  
  std::lock_guard<std::mutex> lock(pending_writes_mutex_);
  result.reserve(pending_writes_map_.size());
  
  for (const auto& [id, pw] : pending_writes_map_) {
    result.push_back(PendingWriteInfo{pw.offset, pw.data, pw.size, pw.callback});
  }
  
  // Sort by offset for sequential replay
  std::sort(result.begin(), result.end(), 
            [](const PendingWriteInfo& a, const PendingWriteInfo& b) {
              return a.offset < b.offset;
            });
  
  return result;
}

FileError MacOSFileOperations::WaitForPendingWrites() {
  if (async_queue_depth_ <= 1 || async_queue_ == nullptr) {
    return FileError::kSuccess;
  }
  
  // Wait for pending writes to complete or be cancelled.
  // 
  // DESIGN: Stall detection is handled by WriteProgressWatchdog at the ImageWriter level.
  // This function simply waits, responding to cancellation. We keep a very long safety-net
  // timeout (5 minutes) only as emergency fallback if cancellation somehow fails.
  constexpr int kEmergencyTimeoutMs = 300000;  // 5 minute emergency fallback
  constexpr int kPollIntervalMs = 500;
  int totalWaitMs = 0;
  
  std::unique_lock<std::mutex> lock(completion_mutex_);
  while (pending_writes_.load() > 0 && !cancelled_.load()) {
    auto result = completion_cv_.wait_for(lock, std::chrono::milliseconds(kPollIntervalMs));
    
    if (result == std::cv_status::timeout) {
      totalWaitMs += kPollIntervalMs;
      
      // Emergency safety-net: if we've been waiting 5 minutes, something is very wrong
      if (totalWaitMs >= kEmergencyTimeoutMs) {
        int remaining = pending_writes_.load();
        Log("WaitForPendingWrites: EMERGENCY timeout after " + std::to_string(totalWaitMs / 1000) + 
            "s with " + std::to_string(remaining) + " writes still pending - forcing sync fallback");
        
        lock.unlock();
        return AttemptSyncFallback();
      }
      
      // Log progress every 30 seconds (informational only, not stall detection)
      if (totalWaitMs % 30000 == 0) {
        Log("WaitForPendingWrites: " + std::to_string(pending_writes_.load()) + 
            " writes pending after " + std::to_string(totalWaitMs / 1000) + "s");
      }
    }
  }
  
  if (cancelled_.load() && pending_writes_.load() > 0) {
    return FileError::kCancelled;
  }
  
  // Return any error that occurred
  return first_async_error_.load();
}

FileError MacOSFileOperations::AttemptSyncFallback() {
  // Get pending writes before cancelling (they're still valid in ring buffer)
  auto pendingWrites = GetPendingWritesSorted();
  
  if (pendingWrites.empty()) {
    Log("Sync fallback: no pending writes to replay");
    sync_fallback_mode_ = true;
    return FileError::kSuccess;
  }
  
  Log("Sync fallback: replaying " + std::to_string(pendingWrites.size()) + " writes synchronously");
  
  // Cancel any remaining async operations
  cancelled_.store(true);
  
  // Give async operations a moment to respond to cancellation
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Switch to sync mode for all future writes
  sync_fallback_mode_ = true;
  
  // Clear the pending writes map (we'll handle them synchronously)
  {
    std::lock_guard<std::mutex> lock(pending_writes_mutex_);
    pending_writes_map_.clear();
  }
  pending_writes_.store(0);
  
  // Replay pending writes synchronously with timeout protection
  for (const auto& pw : pendingWrites) {
    ssize_t written = -1;
    
    auto result = runWithTimeout(
        [this, &pw, &written]() {
          written = pwrite(fd_, pw.data, pw.size, static_cast<off_t>(pw.offset));
        },
        TimeoutConfig(kSyncWriteTimeoutSeconds)
            .withOnTimeout([this, &pw]() {
              Log("Timeout: write at offset " + std::to_string(pw.offset) + " - closing fd");
              int fd_copy = fd_;
              fd_ = -1;
              close(fd_copy);
            })
    );
    
    if (result == TimeoutResult::TimedOut) {
      Log("Sync fallback: write timed out at offset " + std::to_string(pw.offset));
      return FileError::kTimeout;
    }
    
    if (written < 0 || static_cast<std::size_t>(written) != pw.size) {
      Log("Sync fallback: write failed at offset " + std::to_string(pw.offset));
      return FileError::kWriteError;
    }
    
    if (pw.callback) {
      pw.callback(FileError::kSuccess, pw.size);
    }
  }
  
  if (fd_ < 0) {
    Log("Sync fallback: fd was closed - device unresponsive");
    return FileError::kTimeout;
  }
  
  // Sync to device with timeout protection
  int syncResult = -1;
  auto fsyncResult = runWithTimeout(
      [this, &syncResult]() { syncResult = fsync(fd_); },
      TimeoutConfig(kSyncFsyncTimeoutSeconds)
          .withOnTimeout([this]() {
            Log("Timeout: fsync - closing fd");
            int fd_copy = fd_;
            fd_ = -1;
            close(fd_copy);
          })
  );
  
  if (fsyncResult == TimeoutResult::TimedOut) {
    Log("Sync fallback: fsync timed out");
    return FileError::kTimeout;
  }
  
  if (syncResult != 0) {
    Log("Sync fallback: fsync failed");
    return FileError::kSyncError;
  }
  
  // Update async_write_offset_ to reflect completed writes
  if (!pendingWrites.empty()) {
    const auto& lastWrite = pendingWrites.back();
    async_write_offset_ = lastWrite.offset + lastWrite.size;
  }
  
  // Reset cancelled flag so future operations can proceed (in sync mode)
  cancelled_.store(false);
  first_async_error_.store(FileError::kSuccess);
  
  Log("Sync fallback successful - continuing in sync mode");
  return FileError::kSuccess;
}

bool MacOSFileOperations::DrainAndSwitchToSync(int stallTimeoutSeconds) {
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
  
  while (pending_writes_.load() > 0) {
    // GCD completions happen automatically on their dispatch queue
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
    
    // Brief sleep to avoid spinning
    usleep(100000);  // 100ms
  }
  
  auto elapsed = std::chrono::steady_clock::now() - startTime;
  int elapsedMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  
  Log("DrainAndSwitchToSync: Successfully drained all writes in " + 
      std::to_string(elapsedMs) + "ms - now in sync mode");
  return true;
}

void MacOSFileOperations::ReduceQueueDepthForRecovery(int newDepth) {
  int oldDepth = async_queue_depth_;
  
  // Only reduce, never increase during recovery
  if (newDepth >= oldDepth) {
    return;
  }
  
  // Ensure minimum viable depth (2 still allows some pipelining)
  newDepth = std::max(newDepth, TimeoutDefaults::kMinAsyncQueueDepth);
  
  async_queue_depth_ = newDepth;
  
  Log("Queue depth reduced for recovery: " + std::to_string(oldDepth) + " -> " + std::to_string(newDepth) +
      " (pending: " + std::to_string(pending_writes_.load()) + ")");
}

// GetAsyncIOStats() inherited from FileOperations base class

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<MacOSFileOperations>();
}

} // namespace rpi_imager 