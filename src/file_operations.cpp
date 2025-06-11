/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_operations.h"

// Platform-specific implementations are included by the build system
// Only the implementation for the current platform will be compiled

namespace rpi_imager {

// This function is implemented in the platform-specific source files
// Each platform provides its own implementation
extern std::unique_ptr<FileOperations> CreatePlatformFileOperations();

std::unique_ptr<FileOperations> FileOperations::Create() {
  return CreatePlatformFileOperations();
}

} // namespace rpi_imager 