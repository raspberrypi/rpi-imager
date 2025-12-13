/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_H_
#define FILE_OPERATIONS_H_

#include <cstdint>
#include <string>
#include <memory>
#include <functional>

namespace rpi_imager {

// Logging callback type - allows Qt layer to capture debug output without Qt dependency
using LogCallback = std::function<void(const std::string&)>;
void SetFileOperationsLogCallback(LogCallback callback);

// Internal helper for platform implementations to log messages
void FileOperationsLog(const std::string& msg);

// Error types for file operations
enum class FileError {
  kSuccess,
  kOpenError,
  kWriteError,
  kReadError,
  kSeekError,
  kSizeError,
  kCloseError,
  kLockError,
  kSyncError,
  kFlushError
};

// Abstract interface for platform-specific file operations
class FileOperations {
 public:
  virtual ~FileOperations() = default;

  // Open a file or device for read/write access
  virtual FileError OpenDevice(const std::string& path) = 0;
  
  // Open/create a file for testing purposes
  virtual FileError CreateTestFile(const std::string& path, std::uint64_t size) = 0;
  
  // Write data at a specific offset
  virtual FileError WriteAtOffset(
      std::uint64_t offset,
      const std::uint8_t* data,
      std::size_t size) = 0;
  
  // Get the size of the opened device/file
  virtual FileError GetSize(std::uint64_t& size) = 0;
  
  // Close the current file/device
  virtual FileError Close() = 0;
  
  // Check if a file/device is currently open
  virtual bool IsOpen() const = 0;

  // Streaming I/O operations (for sequential writing like image downloads)
  virtual FileError WriteSequential(const std::uint8_t* data, std::size_t size) = 0;
  virtual FileError ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) = 0;
  
  // ============= Async I/O API =============
  // Async writes allow overlapping I/O latency with data preparation/hashing.
  // This can significantly improve performance on slow media (SD cards over USB).
  //
  // Usage pattern:
  //   1. Call SetAsyncQueueDepth() to enable async mode (default is 1 = sync)
  //   2. Call AsyncWriteSequential() to queue writes without blocking
  //   3. The implementation will block when queue is full
  //   4. Call WaitForPendingWrites() before verification or close
  //
  // Note: Caller must ensure buffers remain valid until write completes!
  
  // Completion callback for async writes
  using AsyncWriteCallback = std::function<void(FileError result, std::size_t bytes_written)>;
  
  // Configure async I/O queue depth (1 = synchronous, >1 = async with that many in-flight)
  // Must be called before writes. Returns false if async I/O is not supported.
  virtual bool SetAsyncQueueDepth(int depth) { (void)depth; return false; }
  
  // Get current queue depth setting
  virtual int GetAsyncQueueDepth() const { return 1; }
  
  // Check if async I/O is supported on this platform
  virtual bool IsAsyncIOSupported() const { return false; }
  
  // Queue an async write. May block if queue is full.
  // The buffer must remain valid until the write completes (callback is called or WaitForPendingWrites returns).
  // If callback is null, completion is silent (just check for errors in WaitForPendingWrites).
  // Returns immediately with kSuccess if queued, or error if queueing failed.
  virtual FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                          AsyncWriteCallback callback = nullptr) {
    // Default implementation: fall back to sync write
    FileError result = WriteSequential(data, size);
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
  }
  
  // Get number of writes currently in flight
  virtual int GetPendingWriteCount() const { return 0; }
  
  // Wait for all pending async writes to complete. Returns first error encountered, or kSuccess.
  virtual FileError WaitForPendingWrites() { return FileError::kSuccess; }
  
  // File positioning for streaming operations
  virtual FileError Seek(std::uint64_t position) = 0;
  virtual std::uint64_t Tell() const = 0;
  
  // Force filesystem sync (for page cache management)
  virtual FileError ForceSync() = 0;
  virtual FileError Flush() = 0;
  
  // Prepare for sequential read (e.g., verification)
  // Invalidates cache and enables read-ahead hints for optimal sequential read performance
  virtual void PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) = 0;
  
  // Get platform-specific file handle (for compatibility with existing code)
  virtual int GetHandle() const = 0;

  // Get the last platform-specific error code (Windows error code, errno on Unix)
  virtual int GetLastErrorCode() const = 0;

  // Check if direct I/O (bypassing page cache) is enabled
  virtual bool IsDirectIOEnabled() const = 0;
  
  // Enable or disable direct I/O (bypassing page cache)
  // Must be called after OpenDevice(). Some platforms may not support disabling after open.
  // Returns kSuccess if the change was applied, or an error if not supported/failed.
  virtual FileError SetDirectIOEnabled(bool enabled) = 0;
  
  // Get direct I/O attempt details (for performance logging)
  // Returns: attempted (bool), succeeded (bool), error_code (int), error_message (string)
  struct DirectIOInfo {
      bool attempted = false;      // Did we try to enable direct I/O?
      bool succeeded = false;      // Did it succeed?
      bool currently_enabled = false; // Is direct I/O currently active? (should match succeeded)
      int error_code = 0;          // Platform error code if failed
      std::string error_message;   // Human-readable error description
  };
  virtual DirectIOInfo GetDirectIOInfo() const = 0;

  // Factory method to create platform-specific implementation
  static std::unique_ptr<FileOperations> Create();
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_H_ 