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
    kReadError,
    kSeekError,
    kSizeError,
    kCloseError,
    kLockError,
    kSyncError,
    kFlushError,
};

enum class DetailedError {
    kNoDetails = 0,     // platform didn’t provide details
    kNoSpace,           // ENOSPC / ERROR_DISK_FULL
    kWriteProtected,    // EROFS / ERROR_WRITE_PROTECT
    kBadSector,         // low-level media error (e.g., ERROR_SECTOR_NOT_FOUND)
    kCrcError,          // CRC / data integrity error
    kInvalidParameter,  // EINVAL / ERROR_INVALID_PARAMETER
    kIoDevice,          // EIO / ERROR_IO_DEVICE
    kAccessDenied,      // EPERM/EACCES / ERROR_ACCESS_DENIED
    kBusy,              // EBUSY (device in use)
};

struct ErrorInfo {
    FileError coarse = FileError::kSuccess;
    DetailedError detailed = DetailedError::kNoDetails;
    int os_code = 0;                // errno or GetLastError()
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

    // Get platform-specific file handle (for compatibility with existing code)
    virtual int GetHandle() const = 0;

    // Query native error
    virtual ErrorInfo LastError() const = 0;

    // Factory method to create platform-specific implementation
    static std::unique_ptr<FileOperations> Create();
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_H_ 
