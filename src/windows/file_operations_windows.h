/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_WINDOWS_H_
#define FILE_OPERATIONS_WINDOWS_H_

#include "../file_operations.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace rpi_imager {

// Windows implementation using Win32 API with IOCP-based async I/O
class WindowsFileOperations : public FileOperations {
 public:
  WindowsFileOperations();
  ~WindowsFileOperations() override;

  // Non-copyable, non-movable (due to IOCP resources)
  WindowsFileOperations(const WindowsFileOperations&) = delete;
  WindowsFileOperations& operator=(const WindowsFileOperations&) = delete;
  WindowsFileOperations(WindowsFileOperations&&) = delete;
  WindowsFileOperations& operator=(WindowsFileOperations&&) = delete;

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
  
  // Handle access (Windows uses HANDLE, so we return a cast to int)
  int GetHandle() const override;

  // Get the last Windows error code
  int GetLastErrorCode() const override;

  // Check if direct I/O is enabled
  bool IsDirectIOEnabled() const override { return using_direct_io_; }
  
  // Enable or disable direct I/O
  FileError SetDirectIOEnabled(bool enabled) override;
  
  // Get direct I/O attempt details
  DirectIOInfo GetDirectIOInfo() const override { 
      DirectIOInfo info = direct_io_info_;
      info.currently_enabled = using_direct_io_;
      return info;
  }
  
  // ============= Async I/O API (Windows: using IOCP) =============
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
  HANDLE handle_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;
  DirectIOInfo direct_io_info_;
  
  // IOCP async I/O state
  int async_queue_depth_;
  std::atomic<int> pending_writes_;
  std::atomic<bool> cancelled_;  // Flag to cancel pending async I/O
  FileError first_async_error_;
  std::uint64_t async_write_offset_;
  std::uint64_t current_file_position_;  // Track position for overlapped sync reads
  HANDLE iocp_;  // I/O Completion Port handle
  
  // Extended OVERLAPPED structure to track per-write context
  struct AsyncWriteContext {
    OVERLAPPED overlapped;
    AsyncWriteCallback callback;
    std::size_t size;
    WindowsFileOperations* self;
  };
  
  std::mutex pending_mutex_;
  std::unordered_map<OVERLAPPED*, AsyncWriteContext*> pending_contexts_;
  
  FileError LockVolume();
  FileError UnlockVolume();
  FileError OpenInternal(const std::string& path, DWORD access, DWORD creation, DWORD flags = FILE_ATTRIBUTE_NORMAL);
  
  static bool IsPhysicalDrivePath(const std::string& path);
  
  bool InitIOCP();
  void CleanupIOCP();
  void ProcessCompletions(bool wait);
  
  // Wait for overlapped I/O with cancellation support
  // Returns true if completed successfully, false if cancelled or error
  bool WaitForOverlappedWithCancel(OVERLAPPED* overlapped, DWORD* bytes_transferred);
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_WINDOWS_H_ 