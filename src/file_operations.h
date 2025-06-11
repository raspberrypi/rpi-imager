/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_H_
#define FILE_OPERATIONS_H_

#include <cstdint>
#include <string>
#include <memory>

namespace rpi_imager {

// Error types for file operations
enum class FileError {
  kSuccess,
  kOpenError,
  kWriteError,
  kSeekError,
  kSizeError,
  kCloseError,
  kLockError
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

  // Factory method to create platform-specific implementation
  static std::unique_ptr<FileOperations> Create();
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_H_ 