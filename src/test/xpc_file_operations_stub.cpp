// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Test-only stub for CreateXpcFileOperations.
//
// The macOS factory CreatePlatformFileOperations() defaults to the
// helper-routed XpcFileOperations, which lives in xpc_file_operations.mm
// and transitively pulls in privileged_io / protobuf. Some unit tests
// (e.g. fat_partition_test) only need the in-process MacOSFileOperations
// path against a real fd, so we provide a stub here that returns a
// MacOSFileOperations instance instead. The test still exercises the
// factory entry point so the public API surface is unchanged.

#include "../file_operations.h"
#include "../mac/file_operations_macos.h"

#include <memory>

namespace rpi_imager {

std::unique_ptr<FileOperations> CreateXpcFileOperations() {
    return std::make_unique<MacOSFileOperations>();
}

} // namespace rpi_imager
