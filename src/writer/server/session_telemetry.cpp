// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "session_telemetry.h"

#include <algorithm>

namespace rpi_imager::writer {

namespace proto = rpi_imager::privileged::proto;

std::size_t SessionTelemetry::latencyBucketIndex(std::uint64_t sample_us) {
    for (std::size_t i = 0; i < kLatencyBucketUpperUs.size(); ++i) {
        if (sample_us <= kLatencyBucketUpperUs[i]) {
            return i;
        }
    }
    return kLatencyBucketUpperUs.size() - 1;
}

void SessionTelemetry::updateLatencyMinMax(std::atomic<std::uint32_t>& min_v,
                                           std::atomic<std::uint32_t>& max_v,
                                           std::uint32_t sample_us) {
    std::uint32_t cur_min = min_v.load();
    while (sample_us < cur_min &&
           !min_v.compare_exchange_weak(cur_min, sample_us)) {}
    std::uint32_t cur_max = max_v.load();
    while (sample_us > cur_max &&
           !max_v.compare_exchange_weak(cur_max, sample_us)) {}
}

void SessionTelemetry::recordWriteLatency(std::uint64_t latency_us) {
    const auto sample = static_cast<std::uint32_t>(
        latency_us > UINT32_MAX ? UINT32_MAX : latency_us);
    write_count_.fetch_add(1);
    total_write_latency_us_.fetch_add(latency_us);
    updateLatencyMinMax(min_write_latency_us_, max_write_latency_us_, sample);
    latency_buckets_[latencyBucketIndex(latency_us)].fetch_add(1);
}

void SessionTelemetry::recordFsync(std::chrono::steady_clock::duration duration) {
    const auto us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
    total_fsync_us_.fetch_add(us);
    fsync_count_.fetch_add(1);
}

void SessionTelemetry::recordPrepareDevice(std::chrono::steady_clock::duration duration) {
    const auto us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
    prepare_device_us_.store(us);
}

void SessionTelemetry::recordHashDevice(std::chrono::steady_clock::duration duration) {
    const auto us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
    hash_device_us_.store(us);
}

void SessionTelemetry::fillSessionStats(
    proto::SessionStats& stats,
    std::uint64_t bytes_written,
    std::chrono::steady_clock::time_point session_start,
    bool success,
    const proto::ErrorInfo* terminal_error) const {
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t duration_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - session_start).count());

    stats.set_bytes_written(bytes_written);
    stats.set_duration_ms(duration_ms);
    stats.set_state(success ? proto::SESSION_STATE_SUCCEEDED
                            : proto::SESSION_STATE_FAILED);
    if (!success && terminal_error != nullptr) {
        *stats.mutable_terminal_error() = *terminal_error;
    }

    const std::uint32_t wcount = write_count_.load();
    const std::uint64_t total_lat = total_write_latency_us_.load();
    const std::uint32_t min_lat = wcount == 0 ? 0 : min_write_latency_us_.load();
    const std::uint32_t max_lat = max_write_latency_us_.load();
    const std::uint32_t avg_lat =
        wcount == 0 ? 0 : static_cast<std::uint32_t>(total_lat / wcount);

    auto* hist = stats.mutable_write_latency();
    hist->set_min_us(min_lat);
    hist->set_max_us(max_lat);
    hist->set_avg_us(avg_lat);
    hist->set_sample_count(wcount);
    for (std::size_t i = 0; i < kLatencyBucketUpperUs.size(); ++i) {
        hist->add_bucket_upper_us(kLatencyBucketUpperUs[i]);
        hist->add_bucket_counts(latency_buckets_[i].load());
    }

    const std::uint64_t fsync_us = total_fsync_us_.load();
    const std::uint64_t prep_us = prepare_device_us_.load();
    const std::uint64_t hash_us = hash_device_us_.load();

    auto add_event = [&](const char* type, std::uint64_t duration_us) {
        if (duration_us == 0) {
            return;
        }
        auto* ev = stats.add_events();
        ev->set_type(type);
        ev->set_start_ms(0);
        ev->set_duration_ms((duration_us + 500) / 1000);
        ev->set_success(true);
        ev->set_metadata("source:helper");
    };
    add_event("helper.prepareDevice", prep_us);
    add_event("helper.fsync", fsync_us);
    add_event("helper.hashDevice", hash_us);

    (void)session_start;
    (void)fsync_count_;
}

} // namespace rpi_imager::writer
