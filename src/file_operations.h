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