/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The platform's own FileOperations implementation, whichever this build has.
 *
 * Cases that need a scripted device derive from the concrete platform class
 * rather than from FileOperations itself, so that they override the three or
 * four members they care about instead of the thirty pure virtuals of the
 * interface. Naming LinuxFileOperations to do it made those cases build on
 * Linux alone: on macOS the class does not exist, and two test binaries
 * failed to link against a page of undefined symbols.
 *
 * Deriving from PlatformFileOperations keeps the same convenience and says
 * what was actually meant -- the real implementation this build uses, with
 * one answer replaced.
 */
#ifndef RPI_TEST_PLATFORM_FILE_OPERATIONS_H
#define RPI_TEST_PLATFORM_FILE_OPERATIONS_H

#if defined(_WIN32)
#include "windows/file_operations_windows.h"
#elif defined(__APPLE__)
#include "mac/file_operations_macos.h"
#else
#include "linux/file_operations_linux.h"
#endif

namespace rpi_imager {

#if defined(_WIN32)
using PlatformFileOperations = WindowsFileOperations;
#elif defined(__APPLE__)
using PlatformFileOperations = MacOSFileOperations;
#else
using PlatformFileOperations = LinuxFileOperations;
#endif

} // namespace rpi_imager

#endif // RPI_TEST_PLATFORM_FILE_OPERATIONS_H
