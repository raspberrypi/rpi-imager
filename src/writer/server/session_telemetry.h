// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Per-session helper-side telemetry (§7a). Shared by the Windows and Linux
// wire helpers; mirrors the accumulator in macOS service_impl.mm.

#pragma once

#include "proto/imager.pb.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>

namespace rpi_imager::writer {

class SessionTelemetry {
public:
    void recordWriteLatency(std::uint64_t latency_us);
    void recordFsync(std::chrono::steady_clock::duration duration);
    void recordPrepareDevice(std::chrono::steady_clock::duration duration);
    void recordHashDevice(std::chrono::steady_clock::duration duration);

    void fillSessionStats(rpi_imager::privileged::proto::SessionStats& stats,
                          std::uint64_t bytes_written,
                          std::chrono::steady_clock::time_point session_start,
                          bool success,
                          const rpi_imager::privileged::proto::ErrorInfo* terminal_error) const;

private:
    static constexpr std::array<std::uint64_t, 10> kLatencyBucketUpperUs = {
        100ull, 250ull, 500ull, 1000ull, 2000ull, 5000ull,
        10000ull, 25000ull, 50000ull,
        std::numeric_limits<std::uint64_t>::max(),
    };

    static std::size_t latencyBucketIndex(std::uint64_t sample_us);
    static void updateLatencyMinMax(std::atomic<std::uint32_t>& min_v,
                                      std::atomic<std::uint32_t>& max_v,
                                      std::uint32_t sample_us);

    std::atomic<std::uint32_t> write_count_{0};
    std::atomic<std::uint64_t> total_write_latency_us_{0};
    std::atomic<std::uint32_t> min_write_latency_us_{UINT32_MAX};
    std::atomic<std::uint32_t> max_write_latency_us_{0};
    std::atomic<std::uint64_t> total_fsync_us_{0};
    std::atomic<std::uint32_t> fsync_count_{0};
    std::atomic<std::uint64_t> prepare_device_us_{0};
    std::atomic<std::uint64_t> hash_device_us_{0};
    std::array<std::atomic<std::uint64_t>, kLatencyBucketUpperUs.size()> latency_buckets_{};
};

} // namespace rpi_imager::writer
