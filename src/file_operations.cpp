/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations.h"

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

// This function is implemented in the platform-specific source files
// Each platform provides its own implementation
extern std::unique_ptr<FileOperations> CreatePlatformFileOperations();

std::unique_ptr<FileOperations> FileOperations::Create() {
  return CreatePlatformFileOperations();
}

} // namespace rpi_imager 