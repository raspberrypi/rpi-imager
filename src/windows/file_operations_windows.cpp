/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations_windows.h"

#include <winioctl.h>
#include <iostream>

namespace rpi_imager {

WindowsFileOperations::WindowsFileOperations() : handle_(INVALID_HANDLE_VALUE) {}

WindowsFileOperations::~WindowsFileOperations() {
  Close();
}

FileError WindowsFileOperations::OpenDevice(const std::string& path) {
  // Windows device paths like \\.\PHYSICALDRIVE0
  FileError result = OpenInternal(path, 
                                  GENERIC_READ | GENERIC_WRITE,
                                  OPEN_EXISTING);
  if (result != FileError::kSuccess) {
    return result;
  }

  // Enable extended DASD I/O for raw disk access
  DWORD bytes_returned;
  if (!DeviceIoControl(handle_, FSCTL_ALLOW_EXTENDED_DASD_IO, 
                       nullptr, 0, nullptr, 0, &bytes_returned, nullptr)) {
    // This may fail on some systems, but we can continue
  }

  // Lock the volume for exclusive access
  return LockVolume();
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
    return FileError::kOpenError;
  }

  // Set file pointer to the desired offset
  LARGE_INTEGER offset_large;
  offset_large.QuadPart = static_cast<LONGLONG>(offset);
  
  if (!SetFilePointerEx(handle_, offset_large, nullptr, FILE_BEGIN)) {
    return FileError::kSeekError;
  }

  // Write data in chunks
  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    DWORD chunk_size = static_cast<DWORD>(std::min(size - bytes_written, 
                                                   static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    
    if (!WriteFile(handle_, data + bytes_written, chunk_size, &written, nullptr)) {
      return FileError::kWriteError;
    }
    
    if (written == 0) {
      return FileError::kWriteError;
    }
    
    bytes_written += written;
  }

  // Flush to ensure data is written
  FlushFileBuffers(handle_);
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
    UnlockVolume(); // Unlock if it was locked
    
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
                        FILE_FLAG_NO_BUFFERING, // Important for raw disk access
                        nullptr);

  if (handle_ == INVALID_HANDLE_VALUE) {
    return FileError::kOpenError;
  }

  current_path_ = path;
  return FileError::kSuccess;
}

// Platform-specific factory function implementation
std::unique_ptr<FileOperations> CreatePlatformFileOperations() {
  return std::make_unique<WindowsFileOperations>();
}

} // namespace rpi_imager 