// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Throughput benchmark for the FileOperations write path.
//
// Triggered by `--benchmark-write <device>`. Opens the given device via
// `FileOperations::Create()` (so the platform factory selection rules
// apply: on macOS the helper is the default; legacy authopen is reached
// via `RPI_IMAGER_USE_LEGACY_AUTHOPEN=1`), writes a configurable amount
// of zero data through the async path, then reports wall-clock time and
// throughput in MB/s.
//
// Purpose: phase 1b acceptance check for the "Performance regression
// from IPC overhead" risk. Run the same invocation under both env-var
// settings and compare numbers.
//
// DESTRUCTIVE: overwrites the first N MB of the target device with
// zeros. Refuses to run without an explicit --benchmark-allow-destroy
// flag at the call site (enforced in main.cpp).

#pragma once

#include <cstddef>
#include <string>

namespace rpi_imager {

struct BenchmarkOptions {
    std::string device_path;
    std::size_t total_bytes = 1ull * 1024 * 1024 * 1024;  // 1 GB default
    std::size_t chunk_bytes = 4ull * 1024 * 1024;          // 4 MB per write
    int  queue_depth = 8;                                   // async queue depth
    bool direct_io = true;

    // Optional: dump a structured performance capture to this path (JSON).
    // Includes per-write submit/complete timings, configuration, summary
    // stats, and a tail of the helper's audit log (macOS, when reachable).
    // Empty = no capture.
    std::string output_path;

    // When true, also time a verify pass after the write: hash the
    // bytes back from the device using the implementation's fast path
    // (helper-side SHA-256 on macOS XpcFileOperations) and report
    // throughput. Falls back to the read-and-hash loop on
    // implementations without a fast path.
    bool verify_after_write = false;
};

// Returns 0 on success, non-zero on any failure. Prints a single-line
// machine-parseable summary on the final stdout line in the form:
//   BENCHMARK result=ok backend=<name> bytes=<N> elapsed_ms=<N> throughput_mb_s=<N.NN>
int runFileOperationsBenchmark(const BenchmarkOptions& opts);

} // namespace rpi_imager
