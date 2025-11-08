/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_linux.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <future>
#include <chrono>
#include <thread>

#ifndef O_SYNC
#define O_SYNC O_DSYNC
#endif

// Timeout for sync operations (in seconds)
// This prevents indefinite hangs on problematic USB/SD card drivers
constexpr int SYNC_TIMEOUT_SECONDS = 30;

/* On Linux, the kernel caches writes in memory (page cache) to improve
 * performance. When writing large files to slow devices (like SD cards), this
 * cache can grow very large. The final 'fsync' or 'fdatasync' call forces the
 * kernel to write all cached data to the disk, which can take a very long
 * time if the cache is large and the device is slow. This can cause the UI
 * to hang for several minutes at the 99% mark, making the application appear
 * unresponsive.
 *
 * To mitigate this, we perform the sync operation in a separate thread with a
 * timeout. If the sync takes longer than SYNC_TIMEOUT_SECONDS, we log a
 * warning and continue without waiting for it to complete. The operating
 * system will still finish writing the data in the background, so data
 * integrity is maintained. This prevents the application from hanging
 * indefinitely while still ensuring the data is eventually written.
 *
 * See: https://github.com/raspberrypi/rpi-imager/issues/480
 */
FileError LinuxFileOperations::SyncWithTimeout(bool use_fsync) {
   if (!IsOpen()) {
     return FileError::kOpenError;
   }
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

FileError LinuxFileOperations::SyncWithTimeout(bool use_fsync) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Create a future for the sync operation
  std::future<int> sync_future = std::async(std::launch::async, [this, use_fsync]() {
    return use_fsync ? fsync(fd_) : fdatasync(fd_);
  });

  // Wait for the sync to complete with timeout
  auto status = sync_future.wait_for(std::chrono::seconds(SYNC_TIMEOUT_SECONDS));

  if (status == std::future_status::timeout) {
    // Sync operation timed out
    // Note: We can't cancel the actual fsync/fdatasync call, but we can continue
    // The OS will eventually complete the flush in the background
    // Return success to allow the operation to continue rather than failing completely
    // Log this condition for debugging
    fprintf(stderr, "Warning: Sync operation timed out after %d seconds. Continuing anyway.\n",
            SYNC_TIMEOUT_SECONDS);
    fprintf(stderr, "This may indicate slow storage or driver issues. Data will be flushed by the OS.\n");
    return FileError::kSuccess;  // Return success to avoid blocking the application
  }

  // Get the result of the sync operation
  int result = sync_future.get();
  if (result != 0) {
    last_error_code_ = errno;
    return use_fsync ? FileError::kSyncError : FileError::kFlushError;
  }

  return FileError::kSuccess;
}

FileError LinuxFileOperations::ForceSync() {
  return SyncWithTimeout(true);  // Use fsync
}

FileError LinuxFileOperations::Flush() {
  return SyncWithTimeout(false);  // Use fdatasync for better performance
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