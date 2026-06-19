// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Per-session async bulk writer for wire helpers. Submits via
// FileOperations::AsyncWriteSequential (IOCP / io_uring) and sends wire
// responses from completion callbacks so the dispatch loop can accept
// the next RPC while writes are in flight (mirrors macOS bulkWriteFromBuffer).

#pragma once

#include "../../file_operations.h"
#include "session_telemetry.h"
#include "wire_outbound.h"
#include "proto/imager.pb.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace rpi_imager::writer {

constexpr int kDefaultBulkInFlightCap = 8;

class SessionBulkWriter {
public:
    SessionBulkWriter() = default;
    ~SessionBulkWriter() { shutdown(); }

    SessionBulkWriter(const SessionBulkWriter&) = delete;
    SessionBulkWriter& operator=(const SessionBulkWriter&) = delete;

    void start(rpi_imager::FileOperations* fops, WireOutbound* outbound);

    // Configure IOCP/io_uring queue depth when the client maps its bulk ring.
    void configureAsyncQueueDepth(int depth);

    int asyncQueueDepth() const { return async_queue_depth_; }

    // Queue a bulk write. Returns true when the wire response will be sent
    // asynchronously (caller must not reply on the dispatch thread).
    bool submit(std::uint64_t request_id,
                const rpi_imager::privileged::proto::WriteRequest& req,
                const std::uint8_t* src,
                std::uint64_t max_session_bytes,
                std::atomic<std::uint64_t>* session_bytes_written,
                SessionTelemetry* telemetry,
                rpi_imager::privileged::proto::ErrorInfo& sync_err);

    // Block until queued jobs and in-flight callbacks complete, then drain
    // the FileOperations async queue. Safe to call from the dispatch thread.
    void drainBeforeSync(rpi_imager::FileOperations* fops);

    // Stop the worker and join it. Does not call WaitForPendingWrites; the
    // caller should do that before closing the device handle.
    void shutdown();

private:
    struct Job {
        std::uint64_t request_id = 0;
        rpi_imager::privileged::proto::WriteRequest req;
        const std::uint8_t* src = nullptr;
        std::uint64_t max_session_bytes = 0;
        std::atomic<std::uint64_t>* session_bytes_written = nullptr;
        SessionTelemetry* telemetry = nullptr;
    };

    void workerLoop();
    void runJob(Job job);
    void jobFinished();
    void sendResponse(std::uint64_t request_id,
                      const rpi_imager::privileged::proto::ErrorInfo& err,
                      const std::string& payload);

    rpi_imager::FileOperations* fops_ = nullptr;
    WireOutbound* outbound_ = nullptr;
    int async_queue_depth_ = 0;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool shutdown_ = false;
    int active_jobs_ = 0;
};

} // namespace rpi_imager::writer
