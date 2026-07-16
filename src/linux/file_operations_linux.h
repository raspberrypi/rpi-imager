/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef FILE_OPERATIONS_LINUX_H_
#define FILE_OPERATIONS_LINUX_H_

#include "../unix/file_operations_unix.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

// Forward declare io_uring struct to avoid header dependency in .h
// When HAVE_LIBURING is not defined, this is just used as an opaque pointer (always nullptr)
struct io_uring;

namespace rpi_imager {

// Linux implementation using POSIX file operations with io_uring async I/O
// io_uring provides kernel-level async I/O with minimal syscall overhead (Linux 5.1+)
class LinuxFileOperations : public UnixFileOperations {
 public:
  LinuxFileOperations();
  ~LinuxFileOperations() override;

  // Non-copyable, non-movable
  LinuxFileOperations(const LinuxFileOperations&) = delete;
  LinuxFileOperations& operator=(const LinuxFileOperations&) = delete;
  LinuxFileOperations(LinuxFileOperations&&) = delete;
  LinuxFileOperations& operator=(LinuxFileOperations&&) = delete;

  // ============= Async I/O API (Linux: using io_uring) =============
  bool SetAsyncQueueDepth(int depth) override;
  bool IsAsyncIOSupported() const override { return io_uring_available_; }
  FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size, 
                                  AsyncWriteCallback callback = nullptr) override;
  FileError WaitForPendingWrites() override;
  void CancelAsyncIO() override;

#ifndef HAVE_LIBURING
  bool DrainAndSwitchToSync(int stallTimeoutSeconds) override { return true; }
#endif

 private:
  // io_uring state
  bool io_uring_available_;
  io_uring* ring_;

  bool IsBlockDevicePath(const std::string& path) override;
  FileError GetDeviceSize(std::uint64_t& size) override;

  bool InitIOUring();
  void CleanupIOUring();
  void ProcessCompletions(bool wait) override;
  FileError AttemptSyncFallback() override;
};

} // namespace rpi_imager

#endif // FILE_OPERATIONS_LINUX_H_ 
