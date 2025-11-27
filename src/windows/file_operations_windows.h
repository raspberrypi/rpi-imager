/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_WINDOWS_H_
#define FILE_OPERATIONS_WINDOWS_H_

#include "../file_operations.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rpi_imager {

// Windows implementation using Win32 API
class WindowsFileOperations : public FileOperations {
 public:
  WindowsFileOperations();
  ~WindowsFileOperations() override;

  // Non-copyable, movable
  WindowsFileOperations(const WindowsFileOperations&) = delete;
  WindowsFileOperations& operator=(const WindowsFileOperations&) = delete;
  WindowsFileOperations(WindowsFileOperations&&) = default;
  WindowsFileOperations& operator=(WindowsFileOperations&&) = default;

  FileError OpenDevice(const std::string& path) override;
  FileError CreateTestFile(const std::string& path, std::uint64_t size) override;
  FileError WriteAtOffset(
      std::uint64_t offset,
      const std::uint8_t* data,
      std::size_t size) override;
  FileError GetSize(std::uint64_t& size) override;
  FileError Close() override;
  bool IsOpen() const override;

  // Streaming I/O operations
  FileError WriteSequential(const std::uint8_t* data, std::size_t size) override;
  FileError ReadSequential(std::uint8_t* data, std::size_t size, std::size_t& bytes_read) override;
  
  // File positioning
  FileError Seek(std::uint64_t position) override;
  std::uint64_t Tell() const override;
  
  // Sync operations
  FileError ForceSync() override;
  FileError Flush() override;
  
  // Handle access (Windows uses HANDLE, so we return a cast to int)
  int GetHandle() const override;

  // Get the last Windows error code
  int GetLastErrorCode() const override;

 private:
  HANDLE handle_;
  std::string current_path_;
  int last_error_code_;
  bool using_direct_io_;  // Track if we opened with direct I/O flags
  
   FileError LockVolume();
   FileError UnlockVolume();
   FileError OpenInternal(const std::string& path, DWORD access, DWORD creation, DWORD flags = FILE_ATTRIBUTE_NORMAL);
   
   // Helper to determine if path is a physical drive
   static bool IsPhysicalDrivePath(const std::string& path);
 };

} // namespace rpi_imager

#endif // FILE_OPERATIONS_WINDOWS_H_ 