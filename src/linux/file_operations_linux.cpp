/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_linux.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

namespace rpi_imager {

LinuxFileOperations::LinuxFileOperations() : fd_(-1) {}

LinuxFileOperations::~LinuxFileOperations() {
  Close();
}

FileError LinuxFileOperations::OpenDevice(const std::string& path) {
  return OpenInternal(path.c_str(), O_RDWR | O_SYNC);
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
    return FileError::kSizeError;
  }

  size = static_cast<std::uint64_t>(st.st_size);
  return FileError::kSuccess;
}

FileError LinuxFileOperations::Close() {
  if (fd_ >= 0) {
    if (close(fd_) != 0) {
      fd_ = -1;
      return FileError::kCloseError;
    }
    fd_ = -1;
  }
  current_path_.clear();
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
    return FileError::kOpenError;
  }

  current_path_ = path;
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
        return FileError::kWriteError;
      }
      // EINTR - retry the write
      continue;
    }
    bytes_written += static_cast<std::size_t>(result);
  }

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
  if (::fsync(fd_) != 0) {
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
  if (::fdatasync(fd_) != 0) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

int LinuxFileOperations::GetHandle() const {
  return fd_;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<LinuxFileOperations>();
}

} // namespace rpi_imager 