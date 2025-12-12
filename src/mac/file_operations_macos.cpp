/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_macos.h"
#include "macfile.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/filio.h>
#include <sys/ioctl.h>
#include <sys/disk.h>
#include <errno.h>
#include <iostream>
#include <sstream>

namespace rpi_imager {

// Use the common logging function from file_operations.cpp
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

MacOSFileOperations::MacOSFileOperations() 
    : fd_(-1), last_error_code_(0), using_direct_io_(false),
      async_queue_depth_(1), pending_writes_(0), first_async_error_(FileError::kSuccess),
      async_queue_(nullptr), queue_semaphore_(nullptr), async_write_offset_(0) {
}

bool MacOSFileOperations::IsBlockDevicePath(const std::string& path) {
  // Check for block device paths (e.g., /dev/disk2, /dev/rdisk2)
  return (path.find("/dev/") == 0);
}

bool MacOSFileOperations::EnableDirectIO() {
  if (fd_ < 0) {
    return false;
  }
  
  // macOS uses F_NOCACHE to disable file caching (equivalent to O_DIRECT on Linux)
  // This provides direct I/O to bypass the unified buffer cache
  if (fcntl(fd_, F_NOCACHE, 1) == 0) {
    using_direct_io_ = true;
    FileOperationsLog("Enabled F_NOCACHE for direct I/O");
    return true;
  }
  
  std::ostringstream oss;
  oss << "Warning: Failed to enable F_NOCACHE, errno=" << errno;
  FileOperationsLog(oss.str());
  return false;
}

FileError MacOSFileOperations::SetDirectIOEnabled(bool enabled) {
  if (fd_ < 0) {
    return FileError::kOpenError;
  }
  
  // macOS uses F_NOCACHE to control caching
  // F_NOCACHE with value 1 enables direct I/O (no caching)
  // F_NOCACHE with value 0 disables direct I/O (normal caching)
  int value = enabled ? 1 : 0;
  
  if (fcntl(fd_, F_NOCACHE, value) == 0) {
    using_direct_io_ = enabled;
    std::ostringstream oss;
    oss << "F_NOCACHE " << (enabled ? "enabled" : "disabled");
    FileOperationsLog(oss.str());
    return FileError::kSuccess;
  }
  
  last_error_code_ = errno;
  std::ostringstream oss;
  oss << "Failed to " << (enabled ? "enable" : "disable") << " F_NOCACHE, errno=" << errno;
  FileOperationsLog(oss.str());
  return FileError::kOpenError;
}

MacOSFileOperations::~MacOSFileOperations() {
  Close();
  CleanupAsyncIO();
}

void MacOSFileOperations::InitAsyncIO() {
  if (async_queue_ == nullptr && async_queue_depth_ > 1) {
    async_queue_ = dispatch_queue_create("com.raspberrypi.imager.asyncio", DISPATCH_QUEUE_SERIAL);
    queue_semaphore_ = dispatch_semaphore_create(async_queue_depth_);
    async_write_offset_ = 0;
    pending_writes_.store(0);
    first_async_error_.store(FileError::kSuccess);
    Log("Initialized async I/O with queue depth " + std::to_string(async_queue_depth_));
  }
}

void MacOSFileOperations::CleanupAsyncIO() {
  // Wait for pending writes before cleanup
  WaitForPendingWrites();
  
  if (queue_semaphore_ != nullptr) {
    dispatch_release(queue_semaphore_);
    queue_semaphore_ = nullptr;
  }
  if (async_queue_ != nullptr) {
    dispatch_release(async_queue_);
    async_queue_ = nullptr;
  }
}

FileError MacOSFileOperations::OpenDevice(const std::string& path) {
  std::cout << "Opening macOS device: " << path << std::endl;
  
  bool isBlockDevice = IsBlockDevicePath(path);
  
  // For raw device access on macOS, we need to use the authorization mechanism
  // Similar to how MacFile::authOpen() works
  if (isBlockDevice) {
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
    
    // Enable direct I/O via F_NOCACHE for block devices
    // This bypasses the unified buffer cache for:
    // 1. Better performance by avoiding double-buffering
    // 2. More accurate verification (reads from actual device, not cache)
    // 3. Reduced memory pressure on the system
    if (!EnableDirectIO()) {
      std::cout << "Warning: Could not enable direct I/O, continuing with buffered I/O" << std::endl;
    }
    
    std::cout << "Successfully opened device with authorization, fd=" << fd_ 
              << (using_direct_io_ ? " (direct I/O enabled)" : " (buffered I/O)") << std::endl;
    return FileError::kSuccess;
  }
  
  // For regular files, use standard POSIX open
  return OpenInternal(path.c_str(), O_RDWR);
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
    last_error_code_ = errno;
    return FileError::kSizeError;
  }

  // For block devices (like /dev/disk2, /dev/rdisk2), st_size is always 0.
  // We need to use DKIOCGETBLOCKCOUNT and DKIOCGETBLOCKSIZE ioctls to get the actual size.
  if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
    // On macOS, both block devices (/dev/disk*) and character devices (/dev/rdisk*) 
    // need special handling to get their size
    std::uint64_t block_count = 0;
    std::uint32_t block_size = 0;
    
    if (ioctl(fd_, DKIOCGETBLOCKCOUNT, &block_count) == -1) {
      last_error_code_ = errno;
      std::cout << "DKIOCGETBLOCKCOUNT ioctl failed, errno=" << errno << std::endl;
      return FileError::kSizeError;
    }
    
    if (ioctl(fd_, DKIOCGETBLOCKSIZE, &block_size) == -1) {
      last_error_code_ = errno;
      std::cout << "DKIOCGETBLOCKSIZE ioctl failed, errno=" << errno << std::endl;
      return FileError::kSizeError;
    }
    
    size = block_count * block_size;
    std::cout << "Block device size: " << size << " bytes (blocks=" << block_count 
              << ", block_size=" << block_size << ")" << std::endl;
    return FileError::kSuccess;
  }

  // For regular files, use st_size
  size = static_cast<std::uint64_t>(st.st_size);
  return FileError::kSuccess;
}

FileError MacOSFileOperations::Close() {
  // Wait for any pending async writes
  WaitForPendingWrites();
  
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
  async_write_offset_ = 0;
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
    last_error_code_ = errno;
    std::cout << "OpenInternal: Failed to open " << path << ", errno=" << errno << std::endl;
    return FileError::kOpenError;
  }

  current_path_ = path;
  last_error_code_ = 0;
  std::cout << "OpenInternal: Successfully opened " << path << ", fd=" << fd_ << std::endl;
  return FileError::kSuccess;
}

// Streaming I/O operations
FileError MacOSFileOperations::WriteSequential(const std::uint8_t* data, std::size_t size) {
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

FileError MacOSFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
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

FileError MacOSFileOperations::Seek(std::uint64_t position) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Wait for pending async writes before seeking
  // This ensures all queued writes complete at their intended offsets
  WaitForPendingWrites();

  if (lseek(fd_, static_cast<off_t>(position), SEEK_SET) == -1) {
    return FileError::kSeekError;
  }

  // Update async write offset to match the new position
  // This is critical for subsequent AsyncWriteSequential calls
  async_write_offset_ = position;

  return FileError::kSuccess;
}

std::uint64_t MacOSFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
  }

  // If async I/O has been used, return the async write offset
  // (pwrite doesn't update the file descriptor position)
  if (async_write_offset_ > 0) {
    return async_write_offset_;
  }

  off_t pos = lseek(fd_, 0, SEEK_CUR);
  return (pos == -1) ? 0 : static_cast<std::uint64_t>(pos);
}

FileError MacOSFileOperations::ForceSync() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Force filesystem sync using fsync - same logic as MacFile::forceSync()
  if (::fsync(fd_) != 0) {
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError MacOSFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // On macOS, use fsync for both flush and sync operations
  if (::fsync(fd_) != 0) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

void MacOSFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
  if (!IsOpen()) {
    return;
  }

  (void)offset;  // macOS hints are file-wide
  (void)length;

  std::cout << "PrepareForSequentialRead: fd=" << fd_ << " path=" << current_path_ << std::endl;

  // F_RDAHEAD: Enable read-ahead for sequential access
  // This tells the kernel to speculatively read data ahead of the current position
  int rdahead_result = fcntl(fd_, F_RDAHEAD, 1);
  std::cout << "  F_RDAHEAD result: " << rdahead_result << (rdahead_result == -1 ? " (FAILED)" : " (OK)") << std::endl;
  if (rdahead_result == -1) {
    std::ostringstream oss;
    oss << "Warning: fcntl(F_RDAHEAD) failed, errno: " << errno
        << " - sequential read performance may be suboptimal";
    Log(oss.str());
  }
  
  // F_NOCACHE: Bypass the unified buffer cache
  // Critical for verification - ensures we read from actual device, not cached writes
  int nocache_result = fcntl(fd_, F_NOCACHE, 1);
  std::cout << "  F_NOCACHE result: " << nocache_result << (nocache_result == -1 ? " (FAILED)" : " (OK)") << std::endl;
  if (nocache_result == -1) {
    std::ostringstream oss;
    oss << "Warning: fcntl(F_NOCACHE) failed, errno: " << errno
        << " - verification may read cached data";
    Log(oss.str());
  }
}

int MacOSFileOperations::GetHandle() const {
  return fd_;
}

int MacOSFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// ============= Async I/O Implementation (using GCD) =============

bool MacOSFileOperations::SetAsyncQueueDepth(int depth) {
  if (depth < 1) depth = 1;
  
  // Clean up existing async resources if changing depth
  if (async_queue_ != nullptr && depth != async_queue_depth_) {
    CleanupAsyncIO();
  }
  
  async_queue_depth_ = depth;
  
  if (depth > 1) {
    InitAsyncIO();
  }
  
  std::ostringstream oss;
  oss << "Async queue depth set to " << depth;
  Log(oss.str());
  
  return true;
}

FileError MacOSFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                                     AsyncWriteCallback callback) {
  if (fd_ < 0) {
    if (callback) callback(FileError::kOpenError, 0);
    return FileError::kOpenError;
  }
  
  // If async not enabled (queue_depth == 1), fall back to sync
  if (async_queue_depth_ <= 1 || async_queue_ == nullptr) {
    FileError result = WriteSequential(data, size);
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
  }
  
  // Wait for a slot in the queue (blocks if queue is full)
  dispatch_semaphore_wait(queue_semaphore_, DISPATCH_TIME_FOREVER);
  
  // Check for previous errors
  if (first_async_error_.load() != FileError::kSuccess) {
    dispatch_semaphore_signal(queue_semaphore_);
    if (callback) callback(first_async_error_.load(), 0);
    return first_async_error_.load();
  }
  
  // Capture the offset for this write
  std::uint64_t write_offset = async_write_offset_;
  async_write_offset_ += size;
  
  pending_writes_.fetch_add(1);
  
  // Queue the async write using GCD
  // Note: We use pwrite to write at a specific offset, allowing concurrent writes
  dispatch_async(async_queue_, ^{
    ssize_t written = pwrite(fd_, data, size, static_cast<off_t>(write_offset));
    
    FileError result = FileError::kSuccess;
    if (written < 0 || static_cast<size_t>(written) != size) {
      result = FileError::kWriteError;
      // Store first error
      FileError expected = FileError::kSuccess;
      first_async_error_.compare_exchange_strong(expected, result);
    }
    
    // Free a slot in the queue for more writes
    dispatch_semaphore_signal(queue_semaphore_);
    
    // Notify caller if callback provided - BEFORE decrementing pending count
    // This ensures WaitForPendingWrites() blocks until callbacks complete
    if (callback) {
      callback(result, result == FileError::kSuccess ? size : 0);
    }
    
    // Decrement pending count AFTER callback completes
    // This is critical for proper synchronization with WaitForPendingWrites()
    pending_writes_.fetch_sub(1);
    
    // Wake up any waiters
    {
      std::lock_guard<std::mutex> lock(completion_mutex_);
      completion_cv_.notify_all();
    }
  });
  
  return FileError::kSuccess;
}

FileError MacOSFileOperations::WaitForPendingWrites() {
  if (async_queue_depth_ <= 1 || async_queue_ == nullptr) {
    return FileError::kSuccess;
  }
  
  // Wait for all pending writes to complete
  std::unique_lock<std::mutex> lock(completion_mutex_);
  completion_cv_.wait(lock, [this]() {
    return pending_writes_.load() == 0;
  });
  
  // Return any error that occurred
  return first_async_error_.load();
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<MacOSFileOperations>();
}

} // namespace rpi_imager 