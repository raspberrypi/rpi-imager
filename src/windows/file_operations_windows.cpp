/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_windows.h"

#include <winioctl.h>
#include <sstream>

namespace rpi_imager {

// Use the common logging function from file_operations.cpp
// Note: We're inside namespace rpi_imager, so no prefix needed
static void Log(const std::string& msg) {
    FileOperationsLog(msg);
}

WindowsFileOperations::WindowsFileOperations() 
    : handle_(INVALID_HANDLE_VALUE), last_error_code_(0), using_direct_io_(false),
      async_queue_depth_(1), pending_writes_(0), cancelled_(false), first_async_error_(FileError::kSuccess),
      async_write_offset_(0), current_file_position_(0), iocp_(INVALID_HANDLE_VALUE) {
}

bool WindowsFileOperations::IsPhysicalDrivePath(const std::string& path) {
  // Check for physical drive paths like \\.\PHYSICALDRIVE0 (case-insensitive)
  if (path.length() < 17) return false;  // Minimum: \\.\PHYSICALDRIVE0
  
  std::string prefix = path.substr(0, 17);
  // Convert to uppercase for comparison
  for (char& c : prefix) {
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  
  return prefix == "\\\\.\\PHYSICALDRIVE";
}

WindowsFileOperations::~WindowsFileOperations() {
  WaitForPendingWrites();
  CleanupIOCP();
  Close();
}

bool WindowsFileOperations::InitIOCP() {
  if (iocp_ != INVALID_HANDLE_VALUE) {
    return true;  // Already initialized
  }
  
  if (handle_ == INVALID_HANDLE_VALUE) {
    return false;
  }
  
  // Create I/O Completion Port associated with our file handle
  iocp_ = CreateIoCompletionPort(handle_, NULL, 0, 0);
  if (iocp_ == NULL) {
    DWORD error = GetLastError();
    std::ostringstream oss;
    oss << "CreateIoCompletionPort failed, error: " << error;
    Log(oss.str());
    iocp_ = INVALID_HANDLE_VALUE;
    return false;
  }
  
  Log("IOCP initialized for async I/O");
  return true;
}

void WindowsFileOperations::CleanupIOCP() {
  // Clean up any pending contexts
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto& pair : pending_contexts_) {
      delete pair.second;
    }
    pending_contexts_.clear();
  }
  
  if (iocp_ != INVALID_HANDLE_VALUE) {
    CloseHandle(iocp_);
    iocp_ = INVALID_HANDLE_VALUE;
  }
}

void WindowsFileOperations::ProcessCompletions(bool wait) {
  if (iocp_ == INVALID_HANDLE_VALUE || pending_writes_.load() == 0) {
    return;
  }
  
  // Use a short timeout when waiting, so we can check for cancellation
  DWORD timeout = wait ? 100 : 0;  // 100ms when waiting, 0 when polling
  bool processed_at_least_one = false;
  
  while (pending_writes_.load() > 0) {
    // Check for cancellation
    if (cancelled_.load()) {
      break;
    }
    
    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    LPOVERLAPPED overlapped = nullptr;
    
    BOOL success = GetQueuedCompletionStatus(
        iocp_,
        &bytes_transferred,
        &completion_key,
        &overlapped,
        timeout);
    
    if (overlapped == nullptr) {
      // Timeout or error with no overlapped
      if (!wait || cancelled_.load()) break;
      // If we already processed at least one, don't keep blocking - return
      // so caller can queue more work. Only block if we haven't processed any.
      if (processed_at_least_one) break;
      continue;  // Keep waiting for the first completion
    }
    
    // Find the context for this completion
    AsyncWriteContext* ctx = nullptr;
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      auto it = pending_contexts_.find(overlapped);
      if (it != pending_contexts_.end()) {
        ctx = it->second;
        pending_contexts_.erase(it);
      }
    }
    
    if (ctx == nullptr) {
      Log("Warning: Unknown OVERLAPPED in completion");
      continue;
    }
    
    FileError error = FileError::kSuccess;
    
    if (!success) {
      DWORD err = GetLastError();
      // ERROR_OPERATION_ABORTED means the I/O was cancelled
      if (err == ERROR_OPERATION_ABORTED) {
        error = FileError::kCancelled;
      } else {
        error = FileError::kWriteError;
        if (first_async_error_ == FileError::kSuccess) {
          first_async_error_ = error;
        }
        std::ostringstream oss;
        oss << "Async write failed, error: " << err;
        Log(oss.str());
      }
    } else if (bytes_transferred != ctx->size) {
      error = FileError::kWriteError;
      if (first_async_error_ == FileError::kSuccess) {
        first_async_error_ = error;
      }
      std::ostringstream oss;
      oss << "Async short write: expected " << ctx->size << ", got " << bytes_transferred;
      Log(oss.str());
    }
    
    // Invoke callback if provided
    if (ctx->callback) {
      ctx->callback(error, error == FileError::kSuccess ? ctx->size : 0);
    }
    
    pending_writes_.fetch_sub(1);
    delete ctx;
    processed_at_least_one = true;
    
    // After processing at least one, switch to non-blocking to grab any
    // other ready completions, but don't wait for more
    timeout = 0;
  }
}

bool WindowsFileOperations::WaitForOverlappedWithCancel(OVERLAPPED* overlapped, DWORD* bytes_transferred) {
  const DWORD kWaitTimeout = 100;  // Check for cancel every 100ms
  
  while (true) {
    // Check for cancellation first
    if (cancelled_.load()) {
      // CancelIoEx cancels I/O regardless of which thread initiated it
      // (unlike CancelIo which only cancels I/O from the calling thread)
      CancelIoEx(handle_, overlapped);
      // Get the result anyway to clean up the overlapped structure
      // This will return FALSE with ERROR_OPERATION_ABORTED
      GetOverlappedResult(handle_, overlapped, bytes_transferred, TRUE);
      last_error_code_ = ERROR_OPERATION_ABORTED;
      return false;
    }
    
    DWORD wait_result = WaitForSingleObject(overlapped->hEvent, kWaitTimeout);
    
    if (wait_result == WAIT_OBJECT_0) {
      // I/O completed - get the result
      if (!GetOverlappedResult(handle_, overlapped, bytes_transferred, FALSE)) {
        last_error_code_ = GetLastError();
        return false;
      }
      return true;
    } else if (wait_result == WAIT_TIMEOUT) {
      // Continue waiting, loop will check for cancellation
      continue;
    } else {
      // WAIT_FAILED or other error
      last_error_code_ = GetLastError();
      CancelIoEx(handle_, overlapped);
      return false;
    }
  }
}

FileError WindowsFileOperations::OpenDevice(const std::string& path) {
  // Reset direct I/O info
  direct_io_info_ = DirectIOInfo();
  
  // Windows device paths like \\.\PHYSICALDRIVE0
  Log("Opening Windows device: " + path);
  
  // Check if this is a physical drive
  bool isPhysicalDrive = IsPhysicalDrivePath(path);
  Log("Path '" + path + "' isPhysicalDrive=" + (isPhysicalDrive ? "YES" : "NO"));
  
  if (isPhysicalDrive) {
    Log("Detected physical drive, will attempt direct I/O");
    direct_io_info_.attempted = true;
  }
  
  // Use direct I/O flags for physical drives to bypass OS page cache
  // This provides:
  // 1. Better performance by avoiding double-buffering
  // 2. More accurate verification (reads from actual device, not cache)
  // 3. Reduced memory pressure on the system
  // Note: Requires sector-aligned buffers (already done via qMallocAligned with 4096 alignment)
  //
  // FILE_FLAG_OVERLAPPED is always included to enable async I/O via IOCP
  DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
  bool attemptingDirectIO = false;
  if (isPhysicalDrive) {
    flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
    attemptingDirectIO = true;
    Log("Using direct I/O (FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED) for physical drive");
  }
  
  FileError result = OpenInternal(path, 
                                  GENERIC_READ | GENERIC_WRITE,
                                  OPEN_EXISTING,
                                  flags);
  if (result != FileError::kSuccess) {
    // If direct I/O failed, try without it as a fallback
    if (isPhysicalDrive && attemptingDirectIO) {
      DWORD directIoError = static_cast<DWORD>(last_error_code_);
      direct_io_info_.error_code = static_cast<int>(directIoError);
      
      std::ostringstream oss;
      oss << "Direct I/O open failed with error " << directIoError << " (0x" << std::hex << directIoError << ")";
      Log(oss.str());
      
      // Common error codes:
      // ERROR_INVALID_PARAMETER (87) - alignment/size issues
      // ERROR_ACCESS_DENIED (5) - permission or volume in use
      // ERROR_SHARING_VIOLATION (32) - file in use by another process
      switch (directIoError) {
        case ERROR_INVALID_PARAMETER:
          direct_io_info_.error_message = "ERROR_INVALID_PARAMETER: Sector size/alignment issue";
          Log("  -> ERROR_INVALID_PARAMETER: May be sector size/alignment issue");
          break;
        case ERROR_ACCESS_DENIED:
          direct_io_info_.error_message = "ERROR_ACCESS_DENIED: Volume in use or need admin rights";
          Log("  -> ERROR_ACCESS_DENIED: Volume may still be in use or need admin rights");
          break;
        case ERROR_SHARING_VIOLATION:
          direct_io_info_.error_message = "ERROR_SHARING_VIOLATION: Another process has device open";
          Log("  -> ERROR_SHARING_VIOLATION: Another process has the device open");
          break;
        default:
          direct_io_info_.error_message = "Unknown error, fell back to buffered I/O";
          Log("  -> Falling back to buffered I/O");
          break;
      }
      
      using_direct_io_ = false;
      result = OpenInternal(path, 
                           GENERIC_READ | GENERIC_WRITE,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED);
    }
    
    if (result != FileError::kSuccess) {
      std::ostringstream oss;
      oss << "Failed to open device: " << path << " (error " << last_error_code_ << ")";
      Log(oss.str());
      return result;
    }
  } else if (isPhysicalDrive) {
    // Direct I/O succeeded!
    using_direct_io_ = true;  // Set AFTER OpenInternal (which calls Close() and resets it)
    direct_io_info_.succeeded = true;
  }

  // Enable extended DASD I/O for raw disk access
  DWORD bytes_returned;
  if (!DeviceIoControl(handle_, FSCTL_ALLOW_EXTENDED_DASD_IO, 
                       nullptr, 0, nullptr, 0, &bytes_returned, nullptr)) {
    // This may fail on some systems, but we can continue
    Log("Warning: FSCTL_ALLOW_EXTENDED_DASD_IO failed, continuing anyway");
  }

  // Only try to lock volume if this is not a physical drive
  if (!isPhysicalDrive) {
    FileError lock_result = LockVolume();
    if (lock_result != FileError::kSuccess) {
      Log("Warning: Failed to lock volume, continuing anyway");
    }
  } else {
    Log("Skipping volume lock for physical drive");
  }
  
  // Reset async state for new file
  async_write_offset_ = 0;
  current_file_position_ = 0;
  first_async_error_ = FileError::kSuccess;
  cancelled_.store(false);
  
  Log("Successfully opened device: " + path + (using_direct_io_ ? " (direct I/O enabled)" : " (buffered I/O)"));
  return FileError::kSuccess;
}

FileError WindowsFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
  // For test files, create a regular file (not using direct I/O)
  // Still use FILE_FLAG_OVERLAPPED for async I/O support
  using_direct_io_ = false;
  FileError result = OpenInternal(path,
                                  GENERIC_READ | GENERIC_WRITE,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED);
  if (result != FileError::kSuccess) {
    return result;
  }

  // Set file size using SetFilePointer and SetEndOfFile
  LARGE_INTEGER file_size;
  file_size.QuadPart = static_cast<LONGLONG>(size);
  
  if (!SetFilePointerEx(handle_, file_size, nullptr, FILE_BEGIN)) {
    Close();
    return FileError::kSeekError;
  }

  if (!SetEndOfFile(handle_)) {
    Close();
    return FileError::kSizeError;
  }

  return FileError::kSuccess;
}

FileError WindowsFileOperations::WriteAtOffset(
    std::uint64_t offset,
    const std::uint8_t* data,
    std::size_t size) {
  
  if (!IsOpen()) {
    Log("WriteAtOffset: Device not open");
    return FileError::kOpenError;
  }

  // Check for cancellation before starting
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // With FILE_FLAG_OVERLAPPED, we MUST use an OVERLAPPED structure
  // and specify the offset there, not via SetFilePointerEx
  
  // Write data in chunks with retry logic
  std::size_t bytes_written = 0;
  int retry_count = 0;
  const int max_retries = 3;
  
  while (bytes_written < size) {
    // Check for cancellation in the loop
    if (cancelled_.load()) {
      return FileError::kCancelled;
    }
    
    OVERLAPPED overlapped = {};
    
    // Create a manual-reset event for this write operation
    overlapped.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
      last_error_code_ = GetLastError();
      Log("WriteAtOffset: Failed to create event");
      return FileError::kWriteError;
    }
    
    // Set the file offset in the OVERLAPPED structure
    LARGE_INTEGER write_offset;
    write_offset.QuadPart = static_cast<LONGLONG>(offset + bytes_written);
    overlapped.Offset = write_offset.LowPart;
    overlapped.OffsetHigh = write_offset.HighPart;
    
    DWORD chunk_size = static_cast<DWORD>(std::min(size - bytes_written, 
                                                   static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    
    BOOL result = WriteFile(handle_, data + bytes_written, chunk_size, &written, &overlapped);
    
    if (!result) {
      DWORD error = GetLastError();
      
      if (error == ERROR_IO_PENDING) {
        // I/O is pending - wait for completion with cancellation support
        if (!WaitForOverlappedWithCancel(&overlapped, &written)) {
          error = GetLastError();
          CloseHandle(overlapped.hEvent);
          
          // Check if cancelled
          if (cancelled_.load() || error == ERROR_OPERATION_ABORTED) {
            return FileError::kCancelled;
          }
          
          std::ostringstream oss;
          oss << "WriteAtOffset: GetOverlappedResult failed, offset=" << (offset + bytes_written)
              << ", chunk_size=" << chunk_size << ", error=" << error;
          Log(oss.str());
          
          // Handle specific Windows errors
          if (error == ERROR_ACCESS_DENIED || error == ERROR_DISK_FULL ||
              error == ERROR_WRITE_PROTECT || error == ERROR_SECTOR_NOT_FOUND || 
              error == ERROR_CRC) {
            return FileError::kWriteError;
          }
          
          // For other errors, try to retry
          if (retry_count < max_retries) {
            retry_count++;
            Sleep(100 * retry_count);
            continue;
          }
          return FileError::kWriteError;
        }
      } else {
        // Real error (not pending)
        std::ostringstream oss;
        oss << "WriteAtOffset: WriteFile failed, offset=" << (offset + bytes_written)
            << ", chunk_size=" << chunk_size << ", error=" << error;
        Log(oss.str());
        CloseHandle(overlapped.hEvent);
        
        // Handle specific Windows errors
        if (error == ERROR_ACCESS_DENIED) {
          Log("WriteAtOffset: Access denied - volume may be locked or protected");
          return FileError::kWriteError;
        } else if (error == ERROR_DISK_FULL) {
          Log("WriteAtOffset: Disk full");
          return FileError::kWriteError;
        } else if (error == ERROR_WRITE_PROTECT) {
          Log("WriteAtOffset: Write protected");
          return FileError::kWriteError;
        } else if (error == ERROR_SECTOR_NOT_FOUND || error == ERROR_CRC) {
          Log("WriteAtOffset: Media error detected");
          return FileError::kWriteError;
        }
        
        // For other errors, try to retry
        if (retry_count < max_retries) {
          retry_count++;
          std::ostringstream oss2;
          oss2 << "WriteAtOffset: Retrying write operation, attempt " << retry_count;
          Log(oss2.str());
          Sleep(100 * retry_count);
          continue;
        }
        
        return FileError::kWriteError;
      }
    }
    
    CloseHandle(overlapped.hEvent);
    
    if (written == 0) {
      Log("WriteAtOffset: WriteFile returned 0 bytes written");
      
      if (retry_count < max_retries) {
        retry_count++;
        std::ostringstream oss;
        oss << "WriteAtOffset: Retrying write operation, attempt " << retry_count;
        Log(oss.str());
        Sleep(100 * retry_count);
        continue;
      }
      
      return FileError::kWriteError;
    }
    
    bytes_written += written;
    retry_count = 0; // Reset retry count on successful write
  }
 
  return FileError::kSuccess;
}

FileError WindowsFileOperations::GetSize(std::uint64_t& size) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(handle_, &file_size)) {
    // For devices, try to get geometry information
    DISK_GEOMETRY geometry;
    DWORD bytes_returned;
    
    if (DeviceIoControl(handle_, IOCTL_DISK_GET_DRIVE_GEOMETRY,
                        nullptr, 0, &geometry, sizeof(geometry),
                        &bytes_returned, nullptr)) {
      
      size = static_cast<std::uint64_t>(geometry.Cylinders.QuadPart) *
             geometry.TracksPerCylinder *
             geometry.SectorsPerTrack *
             geometry.BytesPerSector;
      return FileError::kSuccess;
    }
    
    return FileError::kSizeError;
  }

  size = static_cast<std::uint64_t>(file_size.QuadPart);
  return FileError::kSuccess;
}

FileError WindowsFileOperations::Close() {
  // Wait for pending async writes
  WaitForPendingWrites();
  
  if (handle_ != INVALID_HANDLE_VALUE) {
    // Only unlock volume if this is not a physical drive
    bool isPhysicalDrive = IsPhysicalDrivePath(current_path_);
    if (!isPhysicalDrive) {
      UnlockVolume();
    }
    
    if (!CloseHandle(handle_)) {
      handle_ = INVALID_HANDLE_VALUE;
      using_direct_io_ = false;
      // Don't reset direct_io_info_ here - it should persist for diagnostics
      // and only be reset at the start of OpenDevice()
      return FileError::kCloseError;
    }
    handle_ = INVALID_HANDLE_VALUE;
  }
  current_path_.clear();
  using_direct_io_ = false;
  // Don't reset direct_io_info_ here - it's needed for post-operation diagnostics
  // and is properly reset at the start of OpenDevice() when opening a new device
  async_write_offset_ = 0;
  current_file_position_ = 0;
  
  // Clean up IOCP - it's tied to the file handle
  CleanupIOCP();
  
  return FileError::kSuccess;
}

bool WindowsFileOperations::IsOpen() const {
  return handle_ != INVALID_HANDLE_VALUE;
}

FileError WindowsFileOperations::SetDirectIOEnabled(bool enabled) {
  if (!IsOpen() || current_path_.empty()) {
    return FileError::kOpenError;
  }
  
  // If already in the desired state, no action needed
  if (using_direct_io_ == enabled) {
    return FileError::kSuccess;
  }
  
  // On Windows, FILE_FLAG_NO_BUFFERING is an open-time flag
  // To change it, we need to reopen the file
  
  // First, save the current position
  LARGE_INTEGER liPos = {0};
  LARGE_INTEGER liCurrent;
  if (!SetFilePointerEx(handle_, liPos, &liCurrent, FILE_CURRENT)) {
    return FileError::kSeekError;
  }
  
  std::string savedPath = current_path_;
  bool isPhysicalDrive = IsPhysicalDrivePath(savedPath);
  
  // Close the current handle
  CloseHandle(handle_);
  handle_ = INVALID_HANDLE_VALUE;
  
  // Determine flags based on desired state
  // Always include FILE_FLAG_OVERLAPPED for async I/O support
  DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
  if (enabled && isPhysicalDrive) {
    flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
  } else if (isPhysicalDrive) {
    flags = FILE_FLAG_WRITE_THROUGH | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OVERLAPPED;
  }
  
  FileError result = OpenInternal(savedPath, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING, flags);
  if (result != FileError::kSuccess) {
    result = OpenInternal(savedPath, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED);
    using_direct_io_ = false;
    if (result != FileError::kSuccess) {
      return result;
    }
    Log("Failed to change direct I/O state, reopened with default flags");
  } else {
    using_direct_io_ = enabled;
  }
  
  // Restore the file position
  if (liCurrent.QuadPart > 0) {
    SetFilePointerEx(handle_, liCurrent, nullptr, FILE_BEGIN);
  }
  
  Log("FILE_FLAG_NO_BUFFERING " + std::string(using_direct_io_ ? "enabled" : "disabled") + " (reopened file)");
  
  return FileError::kSuccess;
}

FileError WindowsFileOperations::LockVolume() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  DWORD bytes_returned;
  if (!DeviceIoControl(handle_, FSCTL_LOCK_VOLUME,
                       nullptr, 0, nullptr, 0, &bytes_returned, nullptr)) {
    // Lock may fail for files (not devices), which is okay
    DWORD error = GetLastError();
    if (error != ERROR_INVALID_FUNCTION) {
      return FileError::kLockError;
    }
  }

  return FileError::kSuccess;
}

FileError WindowsFileOperations::UnlockVolume() {
  if (!IsOpen()) {
    return FileError::kSuccess; // Already closed
  }

  DWORD bytes_returned;
  DeviceIoControl(handle_, FSCTL_UNLOCK_VOLUME,
                  nullptr, 0, nullptr, 0, &bytes_returned, nullptr);
  // Ignore errors here as we're likely closing anyway
  
  return FileError::kSuccess;
}

FileError WindowsFileOperations::OpenInternal(const std::string& path, DWORD access, DWORD creation, DWORD flags) {
  // Close any existing handle
  Close();

  handle_ = CreateFileA(path.c_str(),
                        access,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        creation,
                        flags,
                        nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    last_error_code_ = static_cast<int>(GetLastError());
    return FileError::kOpenError;
  }

  current_path_ = path;
  last_error_code_ = 0;
  return FileError::kSuccess;
}

// Streaming I/O operations
FileError WindowsFileOperations::WriteSequential(const std::uint8_t* data, std::size_t size) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Check for cancellation before starting
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // With FILE_FLAG_OVERLAPPED, we MUST use an OVERLAPPED structure
  // even for synchronous-style writes. We use a manual-reset event
  // and wait on it to achieve synchronous behavior.
  
  DWORD total_written = 0;
  
  while (total_written < size) {
    // Check for cancellation
    if (cancelled_.load()) {
      return FileError::kCancelled;
    }
    
    OVERLAPPED overlapped = {};
    
    // Create a manual-reset event for this write operation
    overlapped.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr) {
      last_error_code_ = GetLastError();
      return FileError::kWriteError;
    }
    
    // Set the file offset in the OVERLAPPED structure
    LARGE_INTEGER offset;
    offset.QuadPart = static_cast<LONGLONG>(current_file_position_ + total_written);
    overlapped.Offset = offset.LowPart;
    overlapped.OffsetHigh = offset.HighPart;
    
    DWORD bytes_written = 0;
    BOOL result = WriteFile(handle_, 
                           data + total_written, 
                           static_cast<DWORD>(size - total_written), 
                           &bytes_written, 
                           &overlapped);
    
    if (!result) {
      DWORD error = GetLastError();
      if (error == ERROR_IO_PENDING) {
        // I/O is pending - wait for completion with cancellation support
        if (!WaitForOverlappedWithCancel(&overlapped, &bytes_written)) {
          CloseHandle(overlapped.hEvent);
          if (cancelled_.load()) {
            return FileError::kCancelled;
          }
          return FileError::kWriteError;
        }
      } else {
        // Real error
        last_error_code_ = error;
        CloseHandle(overlapped.hEvent);
        return FileError::kWriteError;
      }
    }
    
    CloseHandle(overlapped.hEvent);
    total_written += bytes_written;
  }

  // Update our tracked file position
  current_file_position_ += total_written;
  
  // Update async_write_offset_ so Tell() returns correct position
  // This is needed because Seek() sets async_write_offset_, and Tell()
  // uses it if > 0. Without this update, Tell() would return a stale value.
  async_write_offset_ += total_written;

  last_error_code_ = 0;
  return FileError::kSuccess;
}

FileError WindowsFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Check for cancellation before starting
  if (cancelled_.load()) {
    bytes_read = 0;
    return FileError::kCancelled;
  }

  // With FILE_FLAG_OVERLAPPED, we MUST use an OVERLAPPED structure
  // even for synchronous-style reads. We use a manual-reset event
  // and wait on it to achieve synchronous behavior.
  
  OVERLAPPED overlapped = {};
  
  // Create a manual-reset event for this read operation
  overlapped.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (overlapped.hEvent == nullptr) {
    last_error_code_ = GetLastError();
    bytes_read = 0;
    return FileError::kReadError;
  }
  
  // Set the file offset in the OVERLAPPED structure
  LARGE_INTEGER offset;
  offset.QuadPart = static_cast<LONGLONG>(current_file_position_);
  overlapped.Offset = offset.LowPart;
  overlapped.OffsetHigh = offset.HighPart;
  
  DWORD win_bytes_read = 0;
  BOOL result = ReadFile(handle_, data, static_cast<DWORD>(size), &win_bytes_read, &overlapped);
  
  if (!result) {
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
      // I/O is pending - wait for completion with cancellation support
      if (!WaitForOverlappedWithCancel(&overlapped, &win_bytes_read)) {
        CloseHandle(overlapped.hEvent);
        bytes_read = 0;
        if (cancelled_.load()) {
          return FileError::kCancelled;
        }
        return FileError::kReadError;
      }
    } else {
      // Real error
      last_error_code_ = error;
      CloseHandle(overlapped.hEvent);
      bytes_read = 0;
      return FileError::kReadError;
    }
  }
  
  CloseHandle(overlapped.hEvent);
  
  // Update our tracked file position
  current_file_position_ += win_bytes_read;
  
  bytes_read = static_cast<std::size_t>(win_bytes_read);
  return FileError::kSuccess;
}

FileError WindowsFileOperations::Seek(std::uint64_t position) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Wait for pending async writes before seeking
  WaitForPendingWrites();

  // With FILE_FLAG_OVERLAPPED, file position is specified in OVERLAPPED struct,
  // not via SetFilePointerEx. We just track the position ourselves.
  // SetFilePointerEx still works for getting current position but may not be
  // reliable for subsequent synchronous operations on overlapped handles.
  
  // Update our tracked positions for both async writes and sync reads
  async_write_offset_ = position;
  current_file_position_ = position;
  
  return FileError::kSuccess;
}

std::uint64_t WindowsFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
  }

  // If async I/O has been used, return the async write offset
  // (overlapped I/O uses explicit offsets, not the file pointer)
  if (async_write_offset_ > 0) {
    return async_write_offset_;
  }

  LARGE_INTEGER zero = {};
  LARGE_INTEGER current_pos;
  
  if (!SetFilePointerEx(handle_, zero, &current_pos, FILE_CURRENT)) {
    return 0;
  }

  return static_cast<std::uint64_t>(current_pos.QuadPart);
}

FileError WindowsFileOperations::ForceSync() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Check for cancellation - skip sync entirely if cancelled
  // FlushFileBuffers is a blocking syscall that cannot be interrupted
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // Wait for pending async writes first
  FileError wait_result = WaitForPendingWrites();
  if (wait_result != FileError::kSuccess) {
    return wait_result;
  }

  // Check for cancellation again after waiting - skip the blocking sync
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // WARNING: FlushFileBuffers is a blocking call that CANNOT be cancelled.
  // With direct I/O (FILE_FLAG_NO_BUFFERING), this mainly flushes the device's
  // internal cache and should be fast. For buffered I/O, this can take a long
  // time on slow devices.
  if (!FlushFileBuffers(handle_)) {
    DWORD error = GetLastError();
    // ERROR_INVALID_FUNCTION is returned by some devices that don't support flush
    if (error == ERROR_INVALID_FUNCTION) {
      return FileError::kSuccess;
    }
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError WindowsFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // Check for cancellation - skip flush entirely if cancelled
  // FlushFileBuffers is a blocking syscall that cannot be interrupted
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // Wait for pending async writes first
  FileError wait_result = WaitForPendingWrites();
  if (wait_result != FileError::kSuccess) {
    return wait_result;
  }

  // Check for cancellation again after waiting - skip the blocking flush
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }

  // On Windows, FlushFileBuffers handles both buffer flush and disk sync.
  // WARNING: This is a blocking call that CANNOT be cancelled. On slow/stuck
  // devices, this may block for an extended period. With direct I/O 
  // (FILE_FLAG_NO_BUFFERING), there's typically nothing to flush.
  if (!FlushFileBuffers(handle_)) {
    DWORD error = GetLastError();
    // ERROR_INVALID_FUNCTION is returned by some devices that don't support flush
    if (error == ERROR_INVALID_FUNCTION) {
      return FileError::kSuccess;
    }
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

void WindowsFileOperations::PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) {
  if (!IsOpen()) {
    return;
  }

  // Windows: FILE_FLAG_SEQUENTIAL_SCAN is already set during OpenDevice() for physical drives
  // For additional optimization, we can flush the file buffers to ensure we read from disk
  // Note: Windows doesn't have direct equivalents to posix_fadvise
  
  // Flush to ensure any cached writes are committed before verification read
  if (!FlushFileBuffers(handle_)) {
    DWORD error = GetLastError();
    // Don't warn on ERROR_INVALID_FUNCTION - some devices don't support flush
    if (error != ERROR_INVALID_FUNCTION) {
      std::ostringstream oss;
      oss << "Warning: FlushFileBuffers failed before verification, error: " << error
          << " - verification may read cached data";
      Log(oss.str());
    }
  }
  
  // The FILE_FLAG_SEQUENTIAL_SCAN hint (set at open time) tells Windows to:
  // 1. Optimize for sequential access patterns
  // 2. Use larger read-ahead buffers
  // 3. Not cache data aggressively (since it won't be re-read)
  
  (void)offset;  // Unused on Windows - hints are file-wide, not range-specific
  (void)length;
}

int WindowsFileOperations::GetHandle() const {
  // Note: This is a compatibility method. Windows HANDLE cannot be safely cast to int.
  // For proper Windows code, use the handle_ member directly or add a GetWindowsHandle() method.
  return static_cast<int>(reinterpret_cast<intptr_t>(handle_));
}

int WindowsFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// ============= Async I/O Implementation (using IOCP) =============

bool WindowsFileOperations::SetAsyncQueueDepth(int depth) {
  if (depth < 1) depth = 1;
  
  async_queue_depth_ = depth;
  
  if (depth > 1 && handle_ != INVALID_HANDLE_VALUE) {
    if (!InitIOCP()) {
      Log("Warning: Failed to initialize IOCP for async I/O");
      return false;
    }
  }
  
  std::ostringstream oss;
  oss << "Async queue depth set to " << depth;
  Log(oss.str());
  
  return true;
}

FileError WindowsFileOperations::AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                                       AsyncWriteCallback callback) {
  if (handle_ == INVALID_HANDLE_VALUE) {
    if (callback) callback(FileError::kOpenError, 0);
    return FileError::kOpenError;
  }
  
  // Check for cancellation
  if (cancelled_.load()) {
    if (callback) callback(FileError::kCancelled, 0);
    return FileError::kCancelled;
  }
  
  // If async not enabled or IOCP not initialized, fall back to sync
  if (async_queue_depth_ <= 1 || iocp_ == INVALID_HANDLE_VALUE) {
    FileError result = WriteSequential(data, size);
    // Note: WriteSequential already updates async_write_offset_
    if (callback) callback(result, result == FileError::kSuccess ? size : 0);
    return result;
  }
  
  // Check for previous errors
  if (first_async_error_ != FileError::kSuccess) {
    if (callback) callback(first_async_error_, 0);
    return first_async_error_;
  }
  
  // Process any completed writes first (non-blocking)
  ProcessCompletions(false);
  
  // If queue is full, wait for completions (checking for cancellation)
  while (pending_writes_.load() >= async_queue_depth_) {
    if (cancelled_.load()) {
      if (callback) callback(FileError::kCancelled, 0);
      return FileError::kCancelled;
    }
    ProcessCompletions(true);
  }
  
  // Allocate context for this write
  AsyncWriteContext* ctx = new AsyncWriteContext();
  ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));
  ctx->callback = callback;
  ctx->size = size;
  ctx->self = this;
  
  // Set up the offset for this write
  LARGE_INTEGER offset;
  offset.QuadPart = static_cast<LONGLONG>(async_write_offset_);
  ctx->overlapped.Offset = offset.LowPart;
  ctx->overlapped.OffsetHigh = offset.HighPart;
  
  async_write_offset_ += size;
  
  // Track the pending context
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_contexts_[&ctx->overlapped] = ctx;
  }
  
  pending_writes_.fetch_add(1);
  
  // Issue the async write
  BOOL success = WriteFile(
      handle_,
      data,
      static_cast<DWORD>(size),
      nullptr,  // Bytes written returned via completion
      &ctx->overlapped);
  
  if (!success) {
    DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      // Real error
      pending_writes_.fetch_sub(1);
      {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_contexts_.erase(&ctx->overlapped);
      }
      delete ctx;
      
      std::ostringstream oss;
      oss << "Async WriteFile failed, error: " << error;
      Log(oss.str());
      
      if (callback) callback(FileError::kWriteError, 0);
      return FileError::kWriteError;
    }
    // ERROR_IO_PENDING is expected - the operation is in progress
  }
  
  return FileError::kSuccess;
}

void WindowsFileOperations::PollAsyncCompletions() {
  if (iocp_ != INVALID_HANDLE_VALUE && pending_writes_.load() > 0) {
    ProcessCompletions(false);  // Non-blocking poll
  }
}

void WindowsFileOperations::CancelAsyncIO() {
  // Set the cancellation flag
  cancelled_.store(true);
  
  // Cancel all pending I/O on the handle
  // CancelIoEx with NULL overlapped cancels ALL pending I/O regardless of thread
  if (handle_ != INVALID_HANDLE_VALUE) {
    CancelIoEx(handle_, nullptr);
  }
  
  // Note: We don't wait for completions here - the thread doing I/O will
  // receive ERROR_OPERATION_ABORTED and clean up. Trying to process 
  // completions here could race with the I/O thread.
}

FileError WindowsFileOperations::WaitForPendingWrites() {
  if (iocp_ == INVALID_HANDLE_VALUE) {
    return FileError::kSuccess;
  }
  
  // Process completions until all pending writes are done or cancelled
  // Unlike the normal ProcessCompletions flow, we must drain everything here.
  while (pending_writes_.load() > 0) {
    if (cancelled_.load()) {
      // Cancel all pending I/O - this will cause them to complete with ERROR_OPERATION_ABORTED
      CancelIoEx(handle_, nullptr);
      // We still need to drain all completions to avoid leaking AsyncWriteContext objects
      // Continue the loop to process the ERROR_OPERATION_ABORTED completions
    }
    
    // Block waiting for completions, processing one at a time until empty
    DWORD bytes_transferred = 0;
    ULONG_PTR completion_key = 0;
    LPOVERLAPPED overlapped = nullptr;
    
    BOOL success = GetQueuedCompletionStatus(
        iocp_,
        &bytes_transferred,
        &completion_key,
        &overlapped,
        cancelled_.load() ? 100 : INFINITE);  // Short timeout if cancelled
    
    if (overlapped == nullptr) {
      // Timeout - only happens if cancelled, keep trying
      continue;
    }
    
    // Find and process the context
    AsyncWriteContext* ctx = nullptr;
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      auto it = pending_contexts_.find(overlapped);
      if (it != pending_contexts_.end()) {
        ctx = it->second;
        pending_contexts_.erase(it);
      }
    }
    
    if (ctx == nullptr) {
      Log("Warning: Unknown OVERLAPPED in WaitForPendingWrites");
      continue;
    }
    
    FileError error = FileError::kSuccess;
    if (!success) {
      DWORD err = GetLastError();
      if (err == ERROR_OPERATION_ABORTED) {
        error = FileError::kCancelled;
      } else {
        error = FileError::kWriteError;
        if (first_async_error_ == FileError::kSuccess) {
          first_async_error_ = error;
        }
      }
    } else if (bytes_transferred != ctx->size) {
      error = FileError::kWriteError;
      if (first_async_error_ == FileError::kSuccess) {
        first_async_error_ = error;
      }
    }
    
    if (ctx->callback) {
      ctx->callback(error, error == FileError::kSuccess ? ctx->size : 0);
    }
    
    pending_writes_.fetch_sub(1);
    delete ctx;
  }
  
  if (cancelled_.load()) {
    return FileError::kCancelled;
  }
  
  return first_async_error_;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<WindowsFileOperations>();
}

} // namespace rpi_imager 