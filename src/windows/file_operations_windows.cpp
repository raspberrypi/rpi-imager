/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_windows.h"

#include <winioctl.h>
#include <iostream>

namespace rpi_imager {

WindowsFileOperations::WindowsFileOperations() : handle_(INVALID_HANDLE_VALUE), last_error_code_(0) {}

WindowsFileOperations::~WindowsFileOperations() {
  Close();
}

FileError WindowsFileOperations::OpenDevice(const std::string& path) {
  // Windows device paths like \\.\PHYSICALDRIVE0
  std::cout << "Opening Windows device: " << path << std::endl;
  
  // Check if this is a physical drive
  bool isPhysicalDrive = (path.find("\\\\.\\PHYSICALDRIVE") == 0);
  
  if (isPhysicalDrive) {
    std::cout << "Detected physical drive, ensuring no volumes are mounted" << std::endl;
    
    // For physical drives, we need to ensure no volumes are mounted
    // This is handled by the calling code in downloadthread.cpp
    // but we can add additional safety here
  }
  
  FileError result = OpenInternal(path, 
                                  GENERIC_READ | GENERIC_WRITE,
                                  OPEN_EXISTING);
  if (result != FileError::kSuccess) {
    std::cout << "Failed to open device: " << path << std::endl;
    return result;
  }

  // Enable extended DASD I/O for raw disk access
  DWORD bytes_returned;
  if (!DeviceIoControl(handle_, FSCTL_ALLOW_EXTENDED_DASD_IO, 
                       nullptr, 0, nullptr, 0, &bytes_returned, nullptr)) {
    // This may fail on some systems, but we can continue
    std::cout << "Warning: FSCTL_ALLOW_EXTENDED_DASD_IO failed, continuing anyway" << std::endl;
  }

  // Only try to lock volume if this is not a physical drive
  if (!isPhysicalDrive) {
    FileError lock_result = LockVolume();
    if (lock_result != FileError::kSuccess) {
      std::cout << "Warning: Failed to lock volume, continuing anyway" << std::endl;
    }
  } else {
    std::cout << "Skipping volume lock for physical drive" << std::endl;
  }
  
  std::cout << "Successfully opened device: " << path << std::endl;
  return FileError::kSuccess;
}

FileError WindowsFileOperations::CreateTestFile(const std::string& path, std::uint64_t size) {
  // For test files, create a regular file
  FileError result = OpenInternal(path,
                                  GENERIC_READ | GENERIC_WRITE,
                                  CREATE_ALWAYS);
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
    std::cout << "WriteAtOffset: Device not open" << std::endl;
    return FileError::kOpenError;
  }

  // Set file pointer to the desired offset
  LARGE_INTEGER offset_large;
  offset_large.QuadPart = static_cast<LONGLONG>(offset);
  
  if (!SetFilePointerEx(handle_, offset_large, nullptr, FILE_BEGIN)) {
    DWORD error = GetLastError();
    std::cout << "WriteAtOffset: SetFilePointerEx failed, offset=" << offset << ", error=" << error << std::endl;
    return FileError::kSeekError;
  }

  // Write data in chunks with retry logic
  std::size_t bytes_written = 0;
  int retry_count = 0;
  const int max_retries = 3;
  
  while (bytes_written < size) {
    DWORD chunk_size = static_cast<DWORD>(std::min(size - bytes_written, 
                                                   static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    
    if (!WriteFile(handle_, data + bytes_written, chunk_size, &written, nullptr)) {
      DWORD error = GetLastError();
      std::cout << "WriteAtOffset: WriteFile failed, offset=" << offset + bytes_written 
                << ", chunk_size=" << chunk_size << ", error=" << error << std::endl;
      
      // Handle specific Windows errors
      if (error == ERROR_ACCESS_DENIED) {
        std::cout << "WriteAtOffset: Access denied - volume may be locked or protected" << std::endl;
        return FileError::kWriteError;
      } else if (error == ERROR_DISK_FULL) {
        std::cout << "WriteAtOffset: Disk full" << std::endl;
        return FileError::kWriteError;
      } else if (error == ERROR_WRITE_PROTECT) {
        std::cout << "WriteAtOffset: Write protected" << std::endl;
        return FileError::kWriteError;
      } else if (error == ERROR_SECTOR_NOT_FOUND || error == ERROR_CRC) {
        std::cout << "WriteAtOffset: Media error detected" << std::endl;
        return FileError::kWriteError;
      }
      
      // For other errors, try to retry
      if (retry_count < max_retries) {
        retry_count++;
        std::cout << "WriteAtOffset: Retrying write operation, attempt " << retry_count << std::endl;
        
        // Wait a bit before retry
        Sleep(100 * retry_count);
        
        // Reset file pointer
        if (!SetFilePointerEx(handle_, offset_large, nullptr, FILE_BEGIN)) {
          std::cout << "WriteAtOffset: Failed to reset file pointer for retry" << std::endl;
          return FileError::kSeekError;
        }
        
        continue;
      }
      
      return FileError::kWriteError;
    }
    
    if (written == 0) {
      std::cout << "WriteAtOffset: WriteFile returned 0 bytes written" << std::endl;
      
      if (retry_count < max_retries) {
        retry_count++;
        std::cout << "WriteAtOffset: Retrying write operation, attempt " << retry_count << std::endl;
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
  if (handle_ != INVALID_HANDLE_VALUE) {
    // Only unlock volume if this is not a physical drive
    bool isPhysicalDrive = (current_path_.find("\\\\.\\PHYSICALDRIVE") == 0);
    if (!isPhysicalDrive) {
      UnlockVolume(); // Unlock if it was locked
    }
    
    if (!CloseHandle(handle_)) {
      handle_ = INVALID_HANDLE_VALUE;
      return FileError::kCloseError;
    }
    handle_ = INVALID_HANDLE_VALUE;
  }
  current_path_.clear();
  return FileError::kSuccess;
}

bool WindowsFileOperations::IsOpen() const {
  return handle_ != INVALID_HANDLE_VALUE;
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

FileError WindowsFileOperations::OpenInternal(const std::string& path, DWORD access, DWORD creation) {
  // Close any existing handle
  Close();

  handle_ = CreateFileA(path.c_str(),
                        access,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr,
                        creation,
                        FILE_ATTRIBUTE_NORMAL, // Removed FILE_FLAG_NO_BUFFERING to fix alignment issues
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

  DWORD bytes_written = 0;
  DWORD total_written = 0;
  
  while (total_written < size) {
    BOOL result = WriteFile(handle_, 
                           data + total_written, 
                           static_cast<DWORD>(size - total_written), 
                           &bytes_written, 
                           nullptr);
    
    if (!result) {
      last_error_code_ = static_cast<int>(GetLastError());
      return FileError::kWriteError;
    }
    
    total_written += bytes_written;
  }

  last_error_code_ = 0;
  return FileError::kSuccess;
}

FileError WindowsFileOperations::ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  DWORD win_bytes_read = 0;
  BOOL result = ReadFile(handle_, data, static_cast<DWORD>(size), &win_bytes_read, nullptr);
  
  if (!result) {
    bytes_read = 0;
    return FileError::kReadError;
  }

  bytes_read = static_cast<std::size_t>(win_bytes_read);
  return FileError::kSuccess;
}

FileError WindowsFileOperations::Seek(std::uint64_t position) {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  LARGE_INTEGER pos;
  pos.QuadPart = static_cast<LONGLONG>(position);
  
  LARGE_INTEGER new_pos;
  if (!SetFilePointerEx(handle_, pos, &new_pos, FILE_BEGIN)) {
    return FileError::kSeekError;
  }

  return FileError::kSuccess;
}

std::uint64_t WindowsFileOperations::Tell() const {
  if (!IsOpen()) {
    return 0;
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

  // Force filesystem sync using FlushFileBuffers - same logic as WinFile::forceSync()
  if (!FlushFileBuffers(handle_)) {
    return FileError::kSyncError;
  }

  return FileError::kSuccess;
}

FileError WindowsFileOperations::Flush() {
  if (!IsOpen()) {
    return FileError::kOpenError;
  }

  // On Windows, FlushFileBuffers handles both buffer flush and disk sync
  if (!FlushFileBuffers(handle_)) {
    return FileError::kFlushError;
  }

  return FileError::kSuccess;
}

int WindowsFileOperations::GetHandle() const {
  // Note: This is a compatibility method. Windows HANDLE cannot be safely cast to int.
  // For proper Windows code, use the handle_ member directly or add a GetWindowsHandle() method.
  return static_cast<int>(reinterpret_cast<intptr_t>(handle_));
}

int WindowsFileOperations::GetLastErrorCode() const {
  return last_error_code_;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<WindowsFileOperations>();
}

} // namespace rpi_imager 