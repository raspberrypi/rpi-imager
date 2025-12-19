/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_MACOS_H_
#define FILE_OPERATIONS_MACOS_H_

#include "../file_operations.h"
#include <dispatch/dispatch.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace rpi_imager {

// macOS implementation using POSIX file operations with optional GCD async I/O
class MacOSFileOperations : public FileOperations {
 public:
  MacOSFileOperations();
  ~MacOSFileOperations() override;

  // Non-copyable, non-movable (due to dispatch resources)
  MacOSFileOperations(const MacOSFileOperations&) = delete;
  MacOSFileOperations& operator=(const MacOSFileOperations&) = delete;
  MacOSFileOperations(MacOSFileOperations&&) = delete;
  MacOSFileOperations& operator=(MacOSFileOperations&&) = delete;

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
  
  // Get direct I/O attempt details (macOS: F_NOCACHE always attempted)
  DirectIOInfo GetDirectIOInfo() const override { 
    DirectIOInfo info;
    info.attempted = true;
    info.succeeded = using_direct_io_;
    info.currently_enabled = using_direct_io_;
    return info;
  }
  
  // ============= Async I/O API (macOS: using GCD dispatch_io) =============
  bool SetAsyncQueueDepth(int depth) override;
  int GetAsyncQueueDepth() const override { return async_queue_depth_; }
  bool IsAsyncIOSupported() const override { return true; }
  FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                  AsyncWriteCallback callback = nullptr) override;
  int GetPendingWriteCount() const override { return pending_writes_.load(); }
  void PollAsyncCompletions() override;
  FileError WaitForPendingWrites() override;
  void CancelAsyncIO() override;

 private:
  int fd_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;  // Track if we're using F_NOCACHE
  
  // Async I/O state
  int async_queue_depth_;
  std::atomic<int> pending_writes_;
  std::atomic<bool> cancelled_;
  std::atomic<FileError> first_async_error_;
  dispatch_queue_t async_queue_;
  dispatch_semaphore_t queue_semaphore_;  // Limits in-flight writes
  std::mutex completion_mutex_;
  std::condition_variable completion_cv_;
  std::uint64_t async_write_offset_;  // Current write position for async
  
  FileError OpenInternal(const char* path, int flags, mode_t mode = 0);
  
  // Helper to determine if path is a block device
  static bool IsBlockDevicePath(const std::string& path);
  
  // Enable direct I/O mode using F_NOCACHE
  bool EnableDirectIO();
  
  // Initialize async I/O resources
  void InitAsyncIO();
  
  // Cleanup async I/O resources
  void CleanupAsyncIO();
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_MACOS_H_ 