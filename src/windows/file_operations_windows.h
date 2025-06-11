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

 private:
  HANDLE handle_;
  std::string current_path_;
  
   FileError LockVolume();
   FileError UnlockVolume();
   FileError OpenInternal(const std::string& path, DWORD access, DWORD creation);
 };

} // namespace rpi_imager

#endif // FILE_OPERATIONS_WINDOWS_H_ 