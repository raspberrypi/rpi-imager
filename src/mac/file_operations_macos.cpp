/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_macos.h"
#include "macfile.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <iostream>
#include <QDebug>

namespace rpi_imager {

MacOSFileOperations::MacOSFileOperations() : fd_(-1) {}

MacOSFileOperations::~MacOSFileOperations() {
  Close();
}

FileError MacOSFileOperations::OpenDevice(const std::string& path) {
  std::cout << "Opening macOS device: " << path << std::endl;
  
  // For raw device access on macOS, we need to use the authorization mechanism
  // Similar to how MacFile::authOpen() works
  if (path.find("/dev/") == 0) {
    std::cout << "Device path detected, using macOS authorization..." << std::endl;
    
    // Create a MacFile instance to handle authorization
    MacFile macfile;
    QByteArray devicePath = QByteArray::fromStdString(path);
    
    auto authResult = macfile.authOpen(devicePath);
    if (authResult == MacFile::authOpenCancelled) {
      std::cout << "Authorization cancelled by user" << std::endl;
      return FileError::kOpenError;
    } else if (authResult == MacFile::authOpenError) {
      std::cout << "Authorization failed" << std::endl;
      return FileError::kOpenError;
    }
    
    // Get the file descriptor from the authorized MacFile
    fd_ = macfile.handle();
    if (fd_ < 0) {
      std::cout << "Failed to get file descriptor from MacFile" << std::endl;
      return FileError::kOpenError;
    }
    
    // Duplicate the file descriptor so we can manage it independently
    int duplicated_fd = dup(fd_);
    if (duplicated_fd < 0) {
      std::cout << "Failed to duplicate file descriptor" << std::endl;
      return FileError::kOpenError;
    }
    
    // Close the MacFile (this will close the original fd)
    macfile.close();
    
    // Use our duplicated fd
    fd_ = duplicated_fd;
    current_path_ = path;
    
    std::cout << "Successfully opened device with authorization, fd=" << fd_ << std::endl;
    return FileError::kSuccess;
  }
  
  // For regular files, use standard POSIX open
  return OpenInternal(path.c_str(), O_RDWR | O_SYNC);
}

FileError MacOSFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
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

FileError MacOSFileOperations::WriteAtOffset(
    std::uint64_t offset,
    const std::uint8_t* data,
    std::size_t size) {
  
  if (!IsOpen()) {
    std::cout << "WriteAtOffset: Device not open" << std::endl;
    return FileError::kOpenError;
  }

  if (lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1) {
    std::cout << "WriteAtOffset: lseek failed, offset=" << offset << ", errno=" << errno << std::endl;
    return FileError::kSeekError;
  }

  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd_, data + bytes_written, size - bytes_written);
    if (result <= 0) {
      std::cout << "WriteAtOffset: write failed, bytes_written=" << bytes_written << ", errno=" << errno << std::endl;
      return FileError::kWriteError;
    }
    bytes_written += static_cast<std::size_t>(result);
  }

  std::cout << "WriteAtOffset: Successfully wrote " << bytes_written << " bytes at offset " << offset << std::endl;
  return FileError::kSuccess;
}

FileError MacOSFileOperations::GetSize(std::uint64_t& size) {
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

FileError MacOSFileOperations::Close() {
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

bool MacOSFileOperations::IsOpen() const {
  return fd_ >= 0;
}

FileError MacOSFileOperations::OpenInternal(const char* path, int flags, mode_t mode) {
  // Close any existing file
  Close();

  fd_ = open(path, flags, mode);
  if (fd_ < 0) {
    std::cout << "OpenInternal: Failed to open " << path << ", errno=" << errno << std::endl;
    return FileError::kOpenError;
  }

  current_path_ = path;
  std::cout << "OpenInternal: Successfully opened " << path << ", fd=" << fd_ << std::endl;
  return FileError::kSuccess;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<MacOSFileOperations>();
}

} // namespace rpi_imager 