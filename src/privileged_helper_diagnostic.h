// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// End-to-end diagnostic for the privileged helper architecture.
//
// Triggered via the --test-privileged-helper CLI flag on macOS. Installs
// the helper via SMAppService (prompting the user if needed), opens the
// given device, queries its size, and optionally writes + verifies a
// 4 KB test pattern at offset 0. Prints clear progress and pass/fail
// markers to stdout so a tester running it on someone else's Mac can
// confirm the architecture is alive end-to-end.

#pragma once

#include <string>

namespace rpi_imager {

// Returns a process exit code: 0 on success, non-zero on any failure
// (with a diagnostic message printed to stdout/stderr).
//
// Safe to call before QML / GUI initialisation - this function does its
// own minimal setup and never returns to the caller.
//
// `allow_write`: write a 4 KB pattern at offset 0 via the synchronous
//                writeChunk path and verify it reads back.
// `exercise_bulk`: in addition, allocate a 1 MB shared-memory bulk
//                buffer and exercise mapBulkBuffer / bulkWriteFromBuffer
//                / readChunk, proving the zero-copy path is alive.
//
// On macOS, Linux, and Windows when the native helper is built (default).
// Opt out with RPI_IMAGER_USE_LEGACY_INPROCESS=1. Returns 0 on
// success. Safe before QML init.
int runPrivilegedHelperDiagnostic(const std::string& device_path,
                                    bool allow_write,
                                    bool exercise_bulk = false);

} // namespace rpi_imager
