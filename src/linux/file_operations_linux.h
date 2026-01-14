/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_LINUX_H_
#define FILE_OPERATIONS_LINUX_H_

#include "../file_operations.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

// Forward declare io_uring struct to avoid header dependency in .h
// When HAVE_LIBURING is not defined, this is just used as an opaque pointer (always nullptr)
struct io_uring;

namespace rpi_imager {

// Linux implementation using POSIX file operations with io_uring async I/O
// io_uring provides kernel-level async I/O with minimal syscall overhead (Linux 5.1+)
class LinuxFileOperations : public FileOperations {
 public:
  LinuxFileOperations();
  ~LinuxFileOperations() override;

  // Non-copyable, non-movable
  LinuxFileOperations(const LinuxFileOperations&) = delete;
  LinuxFileOperations& operator=(const LinuxFileOperations&) = delete;
  LinuxFileOperations(LinuxFileOperations&&) = delete;
  LinuxFileOperations& operator=(LinuxFileOperations&&) = delete;

  FileError OpenDevice(const std::string& path) override;
  FileError CreateTestFile(const std::string& path, std::uint64_t size) override;
  FileError WriteAtOffset(
      std::uint64_t offset,
      const std::uint8_t* data,
      std::size_t size) override;
  FileError GetSize(std::uint64_t& size) override;
  FileError Close() override;
  bool IsOpen() const override;

  // Streaming I/O operations
  FileError WriteSequential(const std::uint8_t* data, std::size_t size) override;
  FileError ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) override;
  
  // File positioning
  FileError Seek(std::uint64_t position) override;
  std::uint64_t Tell() const override;
  
  // Sync operations
  FileError ForceSync() override;
  FileError Flush() override;
  
  // Sequential read optimization
  void PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) override;
  
  // Handle access
  int GetHandle() const override;

  // Get the last errno error code
  int GetLastErrorCode() const override;

  // Check if direct I/O is enabled
  bool IsDirectIOEnabled() const override { return using_direct_io_; }
  
  // Enable or disable direct I/O
  FileError SetDirectIOEnabled(bool enabled) override;
  
  // Get direct I/O attempt details (Linux: O_DIRECT attempted for block devices)
  DirectIOInfo GetDirectIOInfo() const override { 
      DirectIOInfo info;
      info.attempted = direct_io_attempted_;
      info.succeeded = using_direct_io_;
      info.currently_enabled = using_direct_io_;
      return info;
  }
  
  // ============= Async I/O API (Linux: using io_uring) =============
  bool SetAsyncQueueDepth(int depth) override;
  int GetAsyncQueueDepth() const override { return async_queue_depth_; }
  bool IsAsyncIOSupported() const override { return io_uring_available_; }
  FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                  AsyncWriteCallback callback = nullptr) override;
  int GetPendingWriteCount() const override { return pending_writes_.load(); }
  void PollAsyncCompletions() override;
  FileError WaitForPendingWrites() override;
  void CancelAsyncIO() override;
  std::vector<PendingWriteInfo> GetPendingWritesSorted() const override;
  void ReduceQueueDepthForRecovery(int newDepth) override;
  // GetAsyncIOStats() inherited from FileOperations base class

 private:
  int fd_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;
  bool direct_io_attempted_;  // True if O_DIRECT was attempted for this device
  
  // io_uring state
  int async_queue_depth_;
  std::atomic<int> pending_writes_;
  std::atomic<bool> cancelled_;
  FileError first_async_error_;
  std::uint64_t async_write_offset_;
  bool io_uring_available_;
  io_uring* ring_;
  
  // Track callbacks by user_data pointer
  struct PendingWrite {
    AsyncWriteCallback callback;
    const std::uint8_t* data;  // For sync fallback replay
    std::uint64_t offset;      // For sync fallback replay
    std::size_t size;
    std::chrono::steady_clock::time_point submit_time;
  };
  std::unordered_map<std::uint64_t, PendingWrite> pending_callbacks_;
  std::uint64_t next_write_id_;
  mutable std::mutex pending_mutex_;
  
  // Note: write_latency_stats_ is inherited from FileOperations base class
  
  FileError OpenInternal(const char* path, int flags, mode_t mode = 0);
  static bool IsBlockDevicePath(const std::string& path);
  
  bool InitIOUring();
  void CleanupIOUring();
  void ProcessCompletions(bool wait);
  FileError AttemptSyncFallback() override;
  bool DrainAndSwitchToSync(int timeoutSeconds) override;
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_LINUX_H_ 