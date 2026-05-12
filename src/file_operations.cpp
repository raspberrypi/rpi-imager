/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations.h"

#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Platform-specific implementations are included by the build system
// Only the implementation for the current platform will be compiled

namespace rpi_imager {

// Global log callback - allows Qt layer to capture debug output
static LogCallback g_logCallback = nullptr;

void SetFileOperationsLogCallback(LogCallback callback) {
    g_logCallback = callback;
}

// Internal helper for platform implementations to use
void FileOperationsLog(const std::string& msg) {
    if (g_logCallback) {
        g_logCallback(msg);
    }
    // Fallback to OutputDebugString on Windows if no callback
#ifdef _WIN32
    else {
        OutputDebugStringA(("[FileOps-fallback] " + msg + "\n").c_str());
    }
#endif
}

// These functions are implemented in the platform-specific source files
// Each platform provides its own implementation
extern std::unique_ptr<FileOperations> CreatePlatformFileOperations();
extern FileOperations::DeviceIOLimits QueryPlatformDeviceIOLimits(const std::string& path);

std::unique_ptr<FileOperations> FileOperations::Create() {
  return CreatePlatformFileOperations();
}

FileOperations::DeviceIOLimits FileOperations::QueryDeviceIOLimits(const std::string& path) {
  return QueryPlatformDeviceIOLimits(path);
}

FileError FileOperations::PrepareDevice(std::uint64_t device_size,
                                          bool zero_last_mb) {
  constexpr std::size_t kOneMB = 1024ull * 1024ull;
  std::vector<std::uint8_t> zeros(kOneMB, 0);

  if (Seek(0) != FileError::kSuccess) return FileError::kSeekError;
  if (auto r = WriteSequential(zeros.data(), kOneMB); r != FileError::kSuccess) return r;
  if (auto r = Flush(); r != FileError::kSuccess) return r;

  if (zero_last_mb && device_size > kOneMB) {
    if (Seek(device_size - kOneMB) != FileError::kSuccess) return FileError::kSeekError;
    if (auto r = WriteSequential(zeros.data(), kOneMB); r != FileError::kSuccess) return r;
    if (auto r = Flush(); r != FileError::kSuccess) return r;
    if (auto r = ForceSync(); r != FileError::kSuccess) return r;
  }

  (void)Seek(0);
  return FileError::kSuccess;
}

} // namespace rpi_imager