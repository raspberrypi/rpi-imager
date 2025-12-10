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

namespace rpi_imager {

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

LinuxFileOperations::LinuxFileOperations() : fd_(-1), last_error_code_(0), using_direct_io_(false) {}

bool LinuxFileOperations::IsBlockDevicePath(const std::string& path) {
  // Check for common block device paths
  return (path.find("/dev/") == 0);
}

LinuxFileOperations::~LinuxFileOperations() {
  Close();
}

FileError LinuxFileOperations::OpenDevice(const std::string& path) {
  // Use O_DIRECT for block devices to bypass the page cache
  // This provides:
  // 1. Better performance by avoiding double-buffering
  // 2. More accurate verification (reads from actual device, not cache)
  // 3. Reduced memory pressure on the system
  // Note: Requires sector-aligned buffers (already done via qMallocAligned with 4096 alignment)
  
  int flags = O_RDWR;
  bool isBlockDevice = IsBlockDevicePath(path);
  
  if (isBlockDevice) {
    flags |= O_DIRECT;
    using_direct_io_ = true;
  }
  
  FileError result = OpenInternal(path.c_str(), flags);
  
  // If O_DIRECT fails (e.g., filesystem doesn't support it), fall back to regular I/O
  if (result != FileError::kSuccess && isBlockDevice && using_direct_io_) {
    using_direct_io_ = false;
    result = OpenInternal(path.c_str(), O_RDWR);
  }
  
  return result;
}

FileError LinuxFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
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

  // For block devices (like /dev/sda, /dev/mmcblk0), st_size is always 0.
  // We need to use the BLKGETSIZE64 ioctl to get the actual device size.
  if (S_ISBLK(st.st_mode)) {
    std::uint64_t device_size = 0;
    if (ioctl(fd_, BLKGETSIZE64, &device_size) == -1) {
      last_error_code_ = errno;
      std::ostringstream oss;
      oss << "BLKGETSIZE64 ioctl failed for block device, errno: " << errno;
      Log(oss.str());
      return FileError::kSizeError;
    }
    size = device_size;
    return FileError::kSuccess;
  }

  // For regular files, use st_size
  size = static_cast<std::uint64_t>(st.st_size);
  return FileError::kSuccess;
}

FileError LinuxFileOperations::Close() {
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
  return FileError::kSuccess;
}

bool LinuxFileOperations::IsOpen() const {
  return fd_ >= 0;
}

FileError LinuxFileOperations::OpenInternal(const char* path, int flags, mode_t mode) {
  // Close any existing file
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

// Streaming I/O operations
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
      // EINTR - retry the write
      continue;
    }
    bytes_written += static_cast<std::size_t>(result);
  }

  last_error_code_ = 0;
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

  if (lseek(fd_, static_cast<off_t>(position), SEEK_SET) == -1) {
    return FileError::kSeekError;
  }

  return FileError::kSuccess;
}

std::uint64_t LinuxFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
  }

  off_t pos = lseek(fd_, 0, SEEK_CUR);
  return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
}

FileError LinuxFileOperations::ForceSync() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Force filesystem sync using fsync - same logic as LinuxFile::forceSync()
  if (fsync(fd_) != 0) {
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError LinuxFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // On Linux, fsync handles both buffer flush and disk sync
  // For streaming operations, we can use fdatasync for better performance
  if (fdatasync(fd_) != 0) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

void LinuxFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
  if (!IsOpen()) {
    return;
  }

  // 1. POSIX_FADV_DONTNEED: Invalidate cache to ensure we read from actual device
  //    This is critical for verification - we need to read what's on disk, not cached data
  int ret = posix_fadvise(fd_, static_cast<off_t>(offset), static_cast<off_t>(length), POSIX_FADV_DONTNEED);
  if (ret != 0) {
    std::ostringstream oss;
    oss << "Warning: posix_fadvise(POSIX_FADV_DONTNEED) failed with error: " << ret
        << " - verification may read cached data";
    Log(oss.str());
  }
  
  // 2. POSIX_FADV_SEQUENTIAL: Hint to kernel for aggressive read-ahead
  //    This doubles the read-ahead window and drops pages after reading
  ret = posix_fadvise(fd_, static_cast<off_t>(offset), static_cast<off_t>(length), POSIX_FADV_SEQUENTIAL);
  if (ret != 0) {
    std::ostringstream oss;
    oss << "Warning: posix_fadvise(POSIX_FADV_SEQUENTIAL) failed with error: " << ret
        << " - sequential read performance may be suboptimal";
    Log(oss.str());
  }
}

int LinuxFileOperations::GetHandle() const {
  return fd_;
}

int LinuxFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<LinuxFileOperations>();
}

} // namespace rpi_imager 