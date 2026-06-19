// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "bulk_write_async.h"

#include <chrono>

namespace proto = rpi_imager::privileged::proto;

namespace rpi_imager::writer {

namespace {

proto::ErrorInfo ok() { return proto::ErrorInfo(); }

proto::ErrorInfo fail(proto::ErrorCode code, const std::string& detail) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    return e;
}

proto::ErrorCode mapFileError(FileError e) {
    switch (e) {
        case FileError::kSuccess: return proto::ERROR_NONE;
        case FileError::kOpenError: return proto::ERROR_DEVICE_NOT_FOUND;
        case FileError::kWriteError: return proto::ERROR_WRITE_FAILED;
        case FileError::kReadError: return proto::ERROR_DEVICE_IO;
        case FileError::kSeekError: return proto::ERROR_DEVICE_IO;
        case FileError::kSizeError: return proto::ERROR_DEVICE_IO;
        case FileError::kCloseError: return proto::ERROR_DEVICE_IO;
        case FileError::kLockError: return proto::ERROR_DEVICE_BUSY;
        case FileError::kSyncError: return proto::ERROR_SYNC_FAILED;
        case FileError::kFlushError: return proto::ERROR_SYNC_FAILED;
        case FileError::kCancelled: return proto::ERROR_CANCELLED;
        case FileError::kTimeout: return proto::ERROR_DEVICE_IO;
        default: return proto::ERROR_UNKNOWN;
    }
}

} // namespace

void SessionBulkWriter::start(rpi_imager::FileOperations* fops, WireOutbound* outbound) {
    shutdown();
    fops_ = fops;
    outbound_ = outbound;
    async_queue_depth_ = 0;
    worker_ = std::thread(&SessionBulkWriter::workerLoop, this);
}

void SessionBulkWriter::configureAsyncQueueDepth(const int depth) {
    async_queue_depth_ = depth;
    if (fops_ && depth > 1) {
        (void)fops_->SetAsyncQueueDepth(depth);
    }
}

void SessionBulkWriter::shutdown() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        shutdown_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    fops_ = nullptr;
    outbound_ = nullptr;
    shutdown_ = false;
    active_jobs_ = 0;
    queue_.clear();
}

void SessionBulkWriter::drainBeforeSync(rpi_imager::FileOperations* fops) {
    {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return queue_.empty() && active_jobs_ == 0; });
    }
    if (fops) {
        (void)fops->WaitForPendingWrites();
    }
}

bool SessionBulkWriter::submit(std::uint64_t request_id,
                               const proto::WriteRequest& req,
                               const std::uint8_t* src,
                               const std::uint64_t max_session_bytes,
                               std::atomic<std::uint64_t>* session_bytes_written,
                               SessionTelemetry* telemetry,
                               proto::ErrorInfo& sync_err) {
    if (!fops_ || !outbound_ || !session_bytes_written || !telemetry || !src) {
        sync_err = fail(proto::ERROR_UNKNOWN, "bulkWrite: writer not started");
        return false;
    }

    const std::uint64_t len = req.length();
    if (req.offset() != fops_->Tell()) {
        sync_err = fail(proto::ERROR_WRITE_FAILED,
                        "bulkWrite: non-sequential device offset");
        return false;
    }

    const std::uint64_t already = session_bytes_written->load();
    if (already + len > max_session_bytes) {
        sync_err = fail(proto::ERROR_WRITE_FAILED,
                        "bulkWrite: session bytes-written cap reached");
        return false;
    }

    Job job;
    job.request_id = request_id;
    job.req = req;
    job.src = src;
    job.max_session_bytes = max_session_bytes;
    job.session_bytes_written = session_bytes_written;
    job.telemetry = telemetry;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (shutdown_) {
            sync_err = fail(proto::ERROR_SESSION_NOT_FOUND, "bulkWrite: session closing");
            return false;
        }
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
    return true;
}

void SessionBulkWriter::sendResponse(const std::uint64_t request_id,
                                     const proto::ErrorInfo& err,
                                     const std::string& payload) {
    if (!outbound_) {
        return;
    }
    proto::WireResponse resp;
    resp.set_request_id(request_id);
    *resp.mutable_error() = err;
    if (err.code() == proto::ERROR_NONE) {
        resp.set_payload(payload);
    }
    (void)outbound_->sendResponse(resp);
}

void SessionBulkWriter::jobFinished() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        --active_jobs_;
    }
    cv_.notify_all();
}

void SessionBulkWriter::runJob(Job job) {
    rpi_imager::FileOperations* fops = fops_;
    if (!fops) {
        sendResponse(job.request_id,
                     fail(proto::ERROR_UNKNOWN, "bulkWrite: session closed"),
                     {});
        jobFinished();
        return;
    }

    const std::size_t len = static_cast<std::size_t>(job.req.length());
    std::atomic<std::uint64_t>* bytes_written = job.session_bytes_written;
    SessionTelemetry* telemetry = job.telemetry;

    std::uint64_t current = bytes_written->load();
    bool cap_ok = false;
    while (current + len <= job.max_session_bytes) {
        if (bytes_written->compare_exchange_weak(current, current + len)) {
            cap_ok = true;
            break;
        }
    }
    if (!cap_ok) {
        sendResponse(job.request_id,
                     fail(proto::ERROR_WRITE_FAILED,
                          "bulkWrite: session bytes-written cap reached"),
                     {});
        jobFinished();
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t request_id = job.request_id;
    const std::uint32_t slot_index = job.req.slot_index();

    (void)fops->AsyncWriteSequential(
        job.src, len,
        [this, request_id, slot_index, len, t0, bytes_written, telemetry](
            FileError fe, std::size_t bytes) {
            if (fe != FileError::kSuccess) {
                bytes_written->fetch_sub(len);
                sendResponse(request_id,
                             fail(mapFileError(fe), "bulkWrite: AsyncWriteSequential failed"),
                             {});
                jobFinished();
                return;
            }
            if (bytes != len) {
                bytes_written->fetch_sub(len - bytes);
            }

            const auto t1 = std::chrono::steady_clock::now();
            const auto lat_us =
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            telemetry->recordWriteLatency(static_cast<std::uint64_t>(lat_us));

            proto::WriteResult wr;
            wr.set_slot_index(slot_index);
            wr.set_bytes_written(bytes);
            wr.set_latency_us(static_cast<std::uint64_t>(lat_us));
            std::string payload;
            wr.SerializeToString(&payload);
            sendResponse(request_id, ok(), payload);
            jobFinished();
        });
}

void SessionBulkWriter::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return shutdown_ || !queue_.empty(); });
            if (shutdown_ && queue_.empty() && active_jobs_ == 0) {
                break;
            }
            if (queue_.empty()) {
                continue;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            ++active_jobs_;
        }
        runJob(std::move(job));
    }
}

} // namespace rpi_imager::writer
