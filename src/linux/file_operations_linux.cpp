/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_linux.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
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

// io_uring support (Linux 5.1+)
#ifdef HAVE_LIBURING
#include <liburing.h>
#endif

namespace rpi_imager {

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

LinuxFileOperations::LinuxFileOperations() 
    : fd_(-1), last_error_code_(0), using_direct_io_(false), direct_io_attempted_(false),
      async_queue_depth_(1), pending_writes_(0), cancelled_(false), first_async_error_(FileError::kSuccess),
      async_write_offset_(0), io_uring_available_(false), ring_(nullptr), next_write_id_(1) {  // Start at 1, 0 is reserved for cancel operations
    
#ifdef HAVE_LIBURING
    // Probe for io_uring availability
    io_uring_available_ = InitIOUring();
    if (io_uring_available_) {
        Log("io_uring available and initialized");
    } else {
        Log("io_uring initialization failed, async I/O will fall back to sync");
    }
#else
    Log("io_uring support not compiled in, async I/O disabled");
#endif
}

bool LinuxFileOperations::IsBlockDevicePath(const std::string& path) {
  // Check for common block device paths
  return (path.find("/dev/") == 0);
}

LinuxFileOperations::~LinuxFileOperations() {
  WaitForPendingWrites();
  CleanupIOUring();
  Close();
}

#ifdef HAVE_LIBURING
bool LinuxFileOperations::InitIOUring() {
    if (ring_ != nullptr) {
        return true;  // Already initialized
    }
    
    ring_ = new io_uring;
    memset(ring_, 0, sizeof(io_uring));
    
    // Initialize with default queue depth
    int queue_size = 64;
    
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    
    int ret = io_uring_queue_init_params(queue_size, ring_, &params);
    if (ret < 0) {
        std::ostringstream oss;
        oss << "io_uring_queue_init failed: " << strerror(-ret) << " (error " << -ret << ")";
        Log(oss.str());
        delete ring_;
        ring_ = nullptr;
        return false;
    }
    
    std::ostringstream oss;
    oss << "io_uring initialized with queue size " << queue_size;
    Log(oss.str());
    
    return true;
}

void LinuxFileOperations::CleanupIOUring() {
    if (ring_ != nullptr) {
        io_uring_queue_exit(ring_);
        delete ring_;
        ring_ = nullptr;
    }
    pending_callbacks_.clear();
}

void LinuxFileOperations::ProcessCompletions(bool wait) {
    if (ring_ == nullptr || pending_writes_.load() == 0) {
        return;
    }
    
    struct io_uring_cqe* cqe;
    int ret;
    bool processed_at_least_one = false;
    
    if (wait && !cancelled_.load()) {
        // Use timeout-based wait so we can check for cancellation
        // Also add overall timeout to prevent infinite waiting if device stops responding
        struct __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 100000000};  // 100ms
        auto waitStart = std::chrono::steady_clock::now();
        
        ret = io_uring_wait_cqe_timeout(ring_, &cqe, &ts);
        // If timeout (-ETIME) and not cancelled, try again with overall limit
        while (ret == -ETIME && !cancelled_.load() && pending_writes_.load() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - waitStart;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= kAsyncFirstCompletionTimeoutMs) {
                Log("ProcessCompletions: No completion received in " + std::to_string(kAsyncFirstCompletionTimeoutMs) + 
                    "ms, returning to allow recovery");
                return;  // Return to caller so queue-wait timeout can trigger
            }
            ret = io_uring_wait_cqe_timeout(ring_, &cqe, &ts);
        }
    } else {
        ret = io_uring_peek_cqe(ring_, &cqe);
    }
    
    while (ret == 0) {
        std::uint64_t write_id = cqe->user_data;
        int result = cqe->res;
        
        // Skip cancel operation completions (user_data == 0)
        // Note: Real writes use write_id starting from 1 (next_write_id_ initialized to 1)
        // Cancel operations use user_data=0 as a sentinel value
        if (write_id == 0) {
            io_uring_cqe_seen(ring_, cqe);
            ret = io_uring_peek_cqe(ring_, &cqe);
            continue;
        }
        
        AsyncWriteCallback callback = nullptr;
        std::size_t expected_size = 0;
        std::chrono::steady_clock::time_point submit_time;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_callbacks_.find(write_id);
            if (it != pending_callbacks_.end()) {
                callback = it->second.callback;
                expected_size = it->second.size;
                submit_time = it->second.submit_time;
                pending_callbacks_.erase(it);
            }
        }
        
        // Record write latency (submit to completion) - uses base class's thread-safe stats
        auto completionTime = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(completionTime - submit_time).count();
        write_latency_stats_.recordCompletion(submit_time);
        
        // Adaptive recovery: if individual write latency is very high, reduce queue depth
        // This helps the system recover when conditions change (memory pressure, slow device)
        constexpr int kMinQueueDepthForReduction = kMinAsyncQueueDepth * 2;  // Trigger reduction above 2x minimum
        
        int currentPending = pending_writes_.load();
        
        // Only reduce if we've drained to the current depth (reached equilibrium)
        // This prevents rapid successive reductions before the system can stabilize
        if (latency > kHighLatencyThresholdMs && 
            async_queue_depth_ >= kMinQueueDepthForReduction && 
            currentPending <= async_queue_depth_ &&  // Must be at equilibrium first
            !sync_fallback_mode_) {
          int newDepth = async_queue_depth_ / 2;
          Log("High write latency detected (" + std::to_string(latency) + "ms) - reducing queue depth to " + std::to_string(newDepth));
          ReduceQueueDepthForRecovery(newDepth);
        }
        
        FileError error = FileError::kSuccess;
        if (result < 0) {
            if (result == -ECANCELED) {
                error = FileError::kCancelled;
            } else {
                error = FileError::kWriteError;
                if (first_async_error_ == FileError::kSuccess) {
                    first_async_error_ = error;
                }
                std::ostringstream oss;
                oss << "io_uring write failed: " << strerror(-result);
                Log(oss.str());
            }
        } else if (static_cast<std::size_t>(result) != expected_size) {
            error = FileError::kWriteError;
            if (first_async_error_ == FileError::kSuccess) {
                first_async_error_ = error;
            }
            std::ostringstream oss;
            oss << "io_uring short write: expected " << expected_size << ", got " << result;
            Log(oss.str());
        }
        
        if (callback) {
            callback(error, error == FileError::kSuccess ? expected_size : 0);
        }
        
        pending_writes_.fetch_sub(1);
        io_uring_cqe_seen(ring_, cqe);
        processed_at_least_one = true;
        
        // After processing at least one, only peek for more (non-blocking)
        ret = io_uring_peek_cqe(ring_, &cqe);
    }
    
    // If waiting and we processed at least one, return to let caller queue more
    // (This matches the Windows behavior we just fixed)
}
#else
// Stubs when liburing is not available
bool LinuxFileOperations::InitIOUring() { return false; }
void LinuxFileOperations::CleanupIOUring() { pending_callbacks_.clear(); }
void LinuxFileOperations::ProcessCompletions(bool) {}
#endif

FileError LinuxFileOperations::OpenDevice(const std::string& path) {
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
  
  return result;
}

FileError LinuxFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
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

FileError LinuxFileOperations::WriteAtOffset(
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

FileError LinuxFileOperations::GetSize(std::uint64_t& size) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  struct stat st;
  if (fstat(fd_, &st) != 0) {
    last_error_code_ = errno;
    return FileError::kSizeError;
  }

  // For block devices, use BLKGETSIZE64 ioctl
  if (S_ISBLK(st.st_mode)) {
    std::uint64_t device_size = 0;
    if (ioctl(fd_, BLKGETSIZE64, &device_size) == -1) {
      last_error_code_ = errno;
      return FileError::kSizeError;
    }
    size = device_size;
    return FileError::kSuccess;
  }

  size = static_cast<std::uint64_t>(st.st_size);
  return FileError::kSuccess;
}

FileError LinuxFileOperations::Close() {
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

bool LinuxFileOperations::IsOpen() const {
  return fd_ >= 0;
}

FileError LinuxFileOperations::SetDirectIOEnabled(bool enabled) {
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

FileError LinuxFileOperations::OpenInternal(const char* path, int flags, mode_t mode) {
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

FileError LinuxFileOperations::WriteSequential(const std::uint8_t* data, std::size_t size) {
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
  // This is needed because Seek() sets async_write_offset_, and Tell()
  // uses it if > 0. Without this update, Tell() would return a stale value.
  async_write_offset_ += size;
  
  return FileError::kSuccess;
}

FileError LinuxFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
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

FileError LinuxFileOperations::Seek(std::uint64_t position) {
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

std::uint64_t LinuxFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
  }

  // If async I/O has been used, return the async write offset
  // (pwrite/io_uring doesn't update the file descriptor position)
  if (async_write_offset_ > 0) {
    return async_write_offset_;
  }

  off_t pos = lseek(fd_, 0, SEEK_CUR);
  return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
}

FileError LinuxFileOperations::ForceSync() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Wait for async writes first
  WaitForPendingWrites();
  
  if (fsync(fd_) != 0) {
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError LinuxFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Wait for async writes first
  WaitForPendingWrites();
  
  if (fdatasync(fd_) != 0) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

void LinuxFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
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

int LinuxFileOperations::GetHandle() const {
  return fd_;
}

int LinuxFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// ============= Async I/O Implementation (using io_uring) =============

bool LinuxFileOperations::SetAsyncQueueDepth(int depth) {
  if (depth < 1) depth = 1;
  
  async_queue_depth_ = depth;
  
  if (depth > 1 && !io_uring_available_) {
    Log("Warning: Async I/O requested but io_uring not available");
    return false;
  }
  
  std::ostringstream oss;
  oss << "Async queue depth set to " << depth << " (io_uring: " << (io_uring_available_ ? "yes" : "no") << ")";
  Log(oss.str());
  
  return io_uring_available_;
}

FileError LinuxFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                                     AsyncWriteCallback callback) {
  if (fd_ < 0) {
    if (callback) callback(FileError::kOpenError, 0);
    return FileError::kOpenError;
  }
  
  // If async not enabled, io_uring not available, or in sync fallback mode, use sync
  if (async_queue_depth_ <= 1 || !io_uring_available_ || ring_ == nullptr || sync_fallback_mode_) {
    FileError result = WriteSequential(data, size);
    // Note: WriteSequential already updates async_write_offset_
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
  }
  
#ifdef HAVE_LIBURING
  // Check for previous errors
  if (first_async_error_ != FileError::kSuccess) {
    if (callback) callback(first_async_error_, 0);
    return first_async_error_;
  }
  
  // Process any completed writes first (non-blocking)
  ProcessCompletions(false);
  
  // If queue is full, wait for completions (checking for cancellation)
  // Note: Stall detection is handled by WriteProgressWatchdog at the ImageWriter level.
  // Here we just wait for a slot, with periodic cancellation checks.
  while (pending_writes_.load() >= async_queue_depth_) {
    if (cancelled_.load()) {
      if (callback) callback(FileError::kCancelled, 0);
      return FileError::kCancelled;
    }
    
    // Wait for completions
    ProcessCompletions(true);
  }
  
  // Get a submission queue entry
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_);
  if (sqe == nullptr) {
    // SQ full, flush and retry
    io_uring_submit(ring_);
    ProcessCompletions(true);
    sqe = io_uring_get_sqe(ring_);
    if (sqe == nullptr) {
      Log("io_uring: failed to get SQE even after flush");
      if (callback) callback(FileError::kWriteError, 0);
      return FileError::kWriteError;
    }
  }
  
  // Prepare the write
  std::uint64_t write_offset = async_write_offset_;
  async_write_offset_ += size;
  
  std::uint64_t write_id = next_write_id_++;
  auto submit_time = std::chrono::steady_clock::now();
  
  // Mark first submit for wall-clock timing (uses base class's thread-safe stats)
  write_latency_stats_.recordSubmit();
  
  // Store callback and info for later (includes data/offset for sync fallback)
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_callbacks_[write_id] = PendingWrite{callback, data, write_offset, size, submit_time};
  }
  
  pending_writes_.fetch_add(1);
  
  // Set up the SQE for a write
  io_uring_prep_write(sqe, fd_, data, static_cast<unsigned>(size), static_cast<off_t>(write_offset));
  io_uring_sqe_set_data64(sqe, write_id);
  
  // Submit the request
  int ret = io_uring_submit(ring_);
  if (ret < 0) {
    pending_writes_.fetch_sub(1);
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_callbacks_.erase(write_id);
    }
    std::ostringstream oss;
    oss << "io_uring_submit failed: " << strerror(-ret);
    Log(oss.str());
    if (callback) callback(FileError::kWriteError, 0);
    return FileError::kWriteError;
  }
  
  return FileError::kSuccess;
#else
  // Should never reach here, but just in case
  FileError result = WriteSequential(data, size);
  if (callback) callback(result, result == FileError::kSuccess ? size : 0);
  return result;
#endif
}

void LinuxFileOperations::PollAsyncCompletions() {
#ifdef HAVE_LIBURING
  if (io_uring_available_ && ring_ != nullptr && pending_writes_.load() > 0) {
    ProcessCompletions(false);  // Non-blocking poll
  }
#endif
}

void LinuxFileOperations::CancelAsyncIO() {
#ifdef HAVE_LIBURING
  // Set cancellation flag first
  cancelled_.store(true);
  
  if (!io_uring_available_ || ring_ == nullptr) {
    return;
  }
  
  // Cancel all pending I/O operations
  // We submit a cancel request for each pending write
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (const auto& [write_id, pending] : pending_callbacks_) {
      struct io_uring_sqe* sqe = io_uring_get_sqe(ring_);
      if (sqe != nullptr) {
        io_uring_prep_cancel64(sqe, write_id, 0);
        io_uring_sqe_set_data64(sqe, 0);  // No callback for cancel operations
      }
    }
  }
  io_uring_submit(ring_);
  
  // Process completions (cancelled ops will return -ECANCELED)
  ProcessCompletions(false);
#endif
}

FileError LinuxFileOperations::WaitForPendingWrites() {
#ifdef HAVE_LIBURING
  if (!io_uring_available_ || ring_ == nullptr) {
    return FileError::kSuccess;
  }
  
  // Wait for pending writes to complete or be cancelled.
  // 
  // DESIGN: Stall detection is handled by WriteProgressWatchdog at the ImageWriter level.
  // This function simply waits, responding to cancellation. We keep a very long safety-net
  // timeout (5 minutes) only as emergency fallback if cancellation somehow fails.
  constexpr int kEmergencyTimeoutSeconds = 300;  // 5 minute emergency fallback
  auto startTime = std::chrono::steady_clock::now();
  int lastLogSecond = 0;
  
  while (pending_writes_.load() > 0) {
    // Check cancellation
    if (cancelled_.load()) {
      CancelAsyncIO();
    }
    
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
#else
  return FileError::kSuccess;
#endif
}

// GetAsyncIOStats() inherited from FileOperations base class

std::vector<FileOperations::PendingWriteInfo> LinuxFileOperations::GetPendingWritesSorted() const {
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

FileError LinuxFileOperations::AttemptSyncFallback() {
#ifdef HAVE_LIBURING
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
  CancelAsyncIO();
  
  // Give async operations a moment to respond to cancellation
  usleep(100000);  // 100ms
  
  // Switch to sync mode for all future writes
  sync_fallback_mode_ = true;
  
  // Clear the pending callbacks (we'll handle them synchronously)
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_callbacks_.clear();
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
  first_async_error_ = FileError::kSuccess;
  
  Log("Sync fallback successful - continuing in sync mode");
  return FileError::kSuccess;
#else
  return FileError::kSuccess;
#endif
}

bool LinuxFileOperations::DrainAndSwitchToSync(int stallTimeoutSeconds) {
#ifdef HAVE_LIBURING
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
    // Actively poll for completions
    ProcessCompletions(false);
    
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
#else
  (void)stallTimeoutSeconds;
  sync_fallback_mode_ = true;
  return true;
#endif
}

void LinuxFileOperations::ReduceQueueDepthForRecovery(int newDepth) {
#ifdef HAVE_LIBURING
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
#else
  (void)newDepth;
#endif
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<LinuxFileOperations>();
}

} // namespace rpi_imager
