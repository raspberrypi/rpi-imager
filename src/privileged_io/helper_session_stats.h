// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Converts helper-reported proto::SessionStats into FileOperations::HelperSessionStats.

#pragma once

#include "../file_operations.h"
#include "proto/imager.pb.h"

#include <algorithm>

namespace rpi_imager::privileged {

inline FileOperations::HelperSessionStats helperSessionStatsFromProto(
    const proto::SessionStats& s) {
    FileOperations::HelperSessionStats hss;
    hss.valid = true;
    hss.bytes_written = s.bytes_written();
    hss.duration_ms = s.duration_ms();
    if (s.has_write_latency()) {
        const auto& h = s.write_latency();
        hss.write_count = h.sample_count();
        hss.min_write_latency_us = static_cast<std::uint32_t>(h.min_us());
        hss.avg_write_latency_us = static_cast<std::uint32_t>(h.avg_us());
        hss.max_write_latency_us = static_cast<std::uint32_t>(h.max_us());
        const int nb = std::min(h.bucket_upper_us_size(), h.bucket_counts_size());
        hss.write_latency_bucket_upper_us.reserve(nb);
        hss.write_latency_bucket_counts.reserve(nb);
        for (int i = 0; i < nb; ++i) {
            hss.write_latency_bucket_upper_us.push_back(h.bucket_upper_us(i));
            hss.write_latency_bucket_counts.push_back(h.bucket_counts(i));
        }
    }
    for (const auto& ev : s.events()) {
        if (ev.type() == "helper.fsync") {
            hss.total_fsync_us = ev.duration_ms() * 1000ull;
        } else if (ev.type() == "helper.prepareDevice") {
            hss.prepare_device_us = ev.duration_ms() * 1000ull;
        } else if (ev.type() == "helper.hashDevice") {
            hss.hash_device_us = ev.duration_ms() * 1000ull;
        }
    }
    return hss;
}

} // namespace rpi_imager::privileged
