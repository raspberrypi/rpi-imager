/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * This file provides common implementations for
 * FileOperations methods used by both FreeBSD and Linux
 */

#ifndef FILE_OPERATIONS_UNIX_H_
#define FILE_OPERATIONS_UNIX_H_

#include "../file_operations.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

namespace rpi_imager {

class UnixFileOperations : public FileOperations {
 public:
  UnixFileOperations();
  virtual ~UnixFileOperations();

  // Non-copyable, non-movable
  UnixFileOperations(const UnixFileOperations&) = delete;
  UnixFileOperations& operator=(const UnixFileOperations&) = delete;
  UnixFileOperations(UnixFileOperations&&) = delete;
  UnixFileOperations& operator=(UnixFileOperations&&) = delete;

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

  // Get direct I/O attempt details
  DirectIOInfo GetDirectIOInfo() const override {
      DirectIOInfo info;
      info.attempted = direct_io_attempted_;
      info.succeeded = using_direct_io_;
      info.currently_enabled = using_direct_io_;
      return info;
  }

  // ============= Async I/O API =============
  int GetAsyncQueueDepth() const override { return async_queue_depth_; }
  int GetPendingWriteCount() const override { return pending_writes_.load(); }
  void PollAsyncCompletions() override;
  std::vector<PendingWriteInfo> GetPendingWritesSorted() const override;
  void ReduceQueueDepthForRecovery(int newDepth) override {
      async_queue_depth_ = newDepth;
  }

  // Platform-dependent implementations
  virtual void CancelAsyncIO() override;
  virtual FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size,
                                         AsyncWriteCallback callback = nullptr) override;
  // GetAsyncIOStats() inherited from FileOperations base class

  virtual FileError AttemptSyncFallback() override;
  virtual bool DrainAndSwitchToSync(int timeoutSeconds) override;
  virtual FileError WaitForPendingWrites() override;

 protected:
  int fd_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;
  bool direct_io_attempted_;  // True if O_DIRECT was attempted for this device

  // async state
  int async_queue_depth_;
  std::atomic<int> pending_writes_;
  std::atomic<bool> cancelled_;
  FileError first_async_error_;
  std::uint64_t async_write_offset_;
  std::uint64_t next_write_id_;

  // Track callbacks by user_data pointer
  struct PendingWrite {
    AsyncWriteCallback callback;
    const std::uint8_t* data;  // For sync fallback replay
    std::uint64_t offset;      // For sync fallback replay
    std::size_t size;
    std::chrono::steady_clock::time_point submit_time;
  };
  std::unordered_map<std::uint64_t, PendingWrite> pending_callbacks_;
  mutable std::mutex pending_mutex_;
  // Note: write_latency_stats_ is inherited from FileOperations base class

  // Platform-specific methods to be implemented by Linux/FreeBSD
  virtual bool IsBlockDevicePath(const std::string& path) = 0;
  virtual FileError GetDeviceSize(std::uint64_t& size) = 0;

  FileError OpenInternal(const char* path, int flags, mode_t mode = 0);
  virtual void ProcessCompletions(bool wait) = 0;
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_UNIX_H_
