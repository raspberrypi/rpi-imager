/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_LINUX_H_
#define FILE_OPERATIONS_LINUX_H_

#include "../file_operations.h"

namespace rpi_imager {

// Linux implementation using POSIX file operations
class LinuxFileOperations : public FileOperations {
 public:
  LinuxFileOperations();
  ~LinuxFileOperations() override;

  // Non-copyable, movable
  LinuxFileOperations(const LinuxFileOperations&) = delete;
  LinuxFileOperations& operator=(const LinuxFileOperations&) = delete;
  LinuxFileOperations(LinuxFileOperations&&) = default;
  LinuxFileOperations& operator=(LinuxFileOperations&&) = default;

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
  int fd_;
  std::string current_path_;
  
  FileError OpenInternal(const char* path, int flags, mode_t mode = 0);
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_LINUX_H_ 