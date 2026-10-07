// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "duplex_connection.h"

#include "frame.h"
#include "server_message.h"

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace rpi_imager::privileged::wire {

namespace {

proto::ErrorInfo makeIoError(proto::ErrorCode code, const char* detail, int kernel_errno) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    if (kernel_errno != 0) {
        e.set_kernel_errno(kernel_errno);
    }
    return e;
}

} // namespace

DuplexConnection::~DuplexConnection() {
    detach();
}

bool DuplexConnection::attach(std::unique_ptr<DuplexStream> stream) {
    if (!stream) {
        return false;
    }
    detach();
    {
        std::lock_guard<std::mutex> lk(write_mutex_);
        stream_ = std::move(stream);
    }
    stop_.store(false);
    attached_.store(true);
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}

void DuplexConnection::failAllOutstandingAsyncLocked(const proto::ErrorInfo& err) {
    std::vector<AsyncCallback> callbacks;
    callbacks.reserve(outstanding_async_.size());
    for (auto& [id, cb] : outstanding_async_) {
        (void)id;
        callbacks.push_back(std::move(cb));
    }
    outstanding_async_.clear();
    async_cv_.notify_all();

    RpcResult result;
    result.ok = false;
    result.error = err;
    for (auto& cb : callbacks) {
        if (cb) {
            cb(result);
        }
    }
}

RpcResult DuplexConnection::rpcResultFromResponse(const proto::WireResponse& response,
                                                  int ancillary_fd) {
    RpcResult out;
    if (response.error().code() != proto::ERROR_NONE) {
        out.ok = false;
        out.error = response.error();
    } else {
        out.ok = true;
        out.payload = response.payload();
        out.ancillary_fd = ancillary_fd;
    }
    return out;
}

void DuplexConnection::detach() {
    stop_.store(true);
    attached_.store(false);
    // The reader blocks reading the connection until the peer speaks;
    // joining without waking it hung every client at exit. The reader also
    // detaches itself on end of stream, and must not join itself. Cancelling
    // also aborts a write blocked on the stream, which the release below
    // would otherwise wait on.
    if (stream_) {
        stream_->cancel();
    }
    if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id()) {
        reader_.join();
    }
    {
        std::lock_guard<std::mutex> lk(write_mutex_);
        stream_.reset();
    }
    acc_ = FrameAccumulator{};

    const proto::ErrorInfo disconnected =
        makeIoError(proto::ERROR_DEVICE_DISCONNECTED, "connection detached", 0);

    {
        std::lock_guard<std::mutex> lk(rpc_mutex_);
        if (pending_) {
            pending_->complete = true;
            pending_->result.ok = false;
            pending_->result.error = disconnected;
            rpc_cv_.notify_all();
            pending_.reset();
        }
    }

    {
        std::lock_guard<std::mutex> lk(async_mutex_);
        failAllOutstandingAsyncLocked(disconnected);
    }
}

void DuplexConnection::setEventCallback(EventCallback cb) {
    std::lock_guard<std::mutex> lk(event_mutex_);
    event_cb_ = std::move(cb);
}

bool DuplexConnection::writeRequest(const proto::WireRequest& req, int& os_error) {
    os_error = 0;
    std::string ser;
    if (!req.SerializeToString(&ser) || ser.size() > kMaxFrameBytes) {
        return false;
    }
    const std::string frame = encodeFrame(ser);
    std::lock_guard<std::mutex> lk(write_mutex_);
    if (!stream_) {
        return false;
    }
    if (!stream_->write(frame.data(), frame.size(), -1)) {
        os_error = stream_->lastError();
        return false;
    }
    return true;
}

RpcResult DuplexConnection::call(proto::WireMethod method,
                                 const std::string& request_payload,
                                 bool expect_ancillary_fd) {
    RpcResult out;
    if (!attached_.load()) {
        out.error = makeIoError(proto::ERROR_DEVICE_DISCONNECTED, "not connected", 0);
        return out;
    }

    std::unique_lock<std::mutex> lk(rpc_mutex_);
    if (pending_) {
        out.error = makeIoError(proto::ERROR_UNKNOWN, "concurrent sync RPC not supported", 0);
        return out;
    }

    pending_ = std::make_unique<PendingRpc>();
    pending_->request_id = next_request_id_.fetch_add(1);

    proto::WireRequest req;
    req.set_method(method);
    req.set_request_id(pending_->request_id);
    req.set_payload(request_payload);
    lk.unlock();

    int err = 0;
    if (!writeRequest(req, err)) {
        lk.lock();
        pending_.reset();
        out.error = makeIoError(proto::ERROR_DEVICE_IO, "transport write failed", err);
        return out;
    }

    lk.lock();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    while (!pending_->complete && attached_.load()) {
        if (rpc_cv_.wait_until(lk, deadline) == std::cv_status::timeout) {
            break;
        }
    }

    if (!pending_->complete) {
        out.error = makeIoError(proto::ERROR_DEVICE_IO, "RPC timed out", 0);
        pending_.reset();
        return out;
    }

    out = std::move(pending_->result);
    pending_.reset();

    if (out.ok && expect_ancillary_fd && out.ancillary_fd < 0) {
        out.ok = false;
        out.error = makeIoError(proto::ERROR_UNKNOWN,
                                "expected ancillary fd but none received", 0);
    }
    return out;
}

void DuplexConnection::setMaxOutstandingAsync(const std::size_t depth) {
    max_outstanding_async_ = depth < 1 ? 1 : depth;
}

std::size_t DuplexConnection::maxOutstandingAsync() const {
    return max_outstanding_async_;
}

void DuplexConnection::submitAsync(proto::WireMethod method,
                                   const std::string& request_payload,
                                   AsyncCallback on_complete) {
    if (!on_complete) {
        return;
    }

    auto fail = [&](const proto::ErrorInfo& err) {
        RpcResult result;
        result.ok = false;
        result.error = err;
        on_complete(std::move(result));
    };

    if (!attached_.load()) {
        fail(makeIoError(proto::ERROR_DEVICE_DISCONNECTED, "not connected", 0));
        return;
    }

    std::unique_lock<std::mutex> alk(async_mutex_);
    async_cv_.wait(alk, [this] {
        return !attached_.load()
               || outstanding_async_.size() < max_outstanding_async_;
    });
    if (!attached_.load()) {
        fail(makeIoError(proto::ERROR_DEVICE_DISCONNECTED, "not connected", 0));
        return;
    }

    const std::uint64_t request_id = next_request_id_.fetch_add(1);
    outstanding_async_.emplace(request_id, std::move(on_complete));
    alk.unlock();

    proto::WireRequest req;
    req.set_method(method);
    req.set_request_id(request_id);
    req.set_payload(request_payload);

    int err = 0;
    if (!writeRequest(req, err)) {
        const proto::ErrorInfo io_err =
            makeIoError(proto::ERROR_DEVICE_IO, "transport write failed", err);
        alk.lock();
        auto it = outstanding_async_.find(request_id);
        if (it != outstanding_async_.end()) {
            AsyncCallback cb = std::move(it->second);
            outstanding_async_.erase(it);
            alk.unlock();
            async_cv_.notify_all();
            RpcResult result;
            result.ok = false;
            result.error = io_err;
            cb(std::move(result));
        }
        return;
    }
}

void DuplexConnection::handleServerPayload(const std::string& payload, int ancillary_fd) {
    proto::WireResponse response;
    proto::WireEvent event;
    bool is_event = false;
    if (!decodeServerMessage(payload, response, event, is_event)) {
        detach();
        return;
    }

    if (is_event) {
        EventCallback cb;
        {
            std::lock_guard<std::mutex> lk(event_mutex_);
            cb = event_cb_;
        }
        if (cb) {
            cb(event);
        }
        return;
    }

    AsyncCallback async_cb;
    bool have_async = false;

    {
        std::lock_guard<std::mutex> lk(rpc_mutex_);
        if (pending_ && pending_->request_id == response.request_id()) {
            pending_->result = rpcResultFromResponse(response, ancillary_fd);
            pending_->complete = true;
            rpc_cv_.notify_all();
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lk(async_mutex_);
        auto it = outstanding_async_.find(response.request_id());
        if (it != outstanding_async_.end()) {
            async_cb = std::move(it->second);
            outstanding_async_.erase(it);
            have_async = true;
        }
    }
    if (have_async) {
        async_cv_.notify_all();
        if (async_cb) {
            async_cb(rpcResultFromResponse(response, ancillary_fd));
        }
        return;
    }
}

void DuplexConnection::readerLoop() {
    char buf[8192];
    std::string payload;
    int pending_ancillary_fd = -1;

    while (!stop_.load() && attached_.load()) {
        bool oversize = false;
        if (acc_.next(payload, oversize)) {
            handleServerPayload(payload, pending_ancillary_fd);
            pending_ancillary_fd = -1;
            continue;
        }
        if (oversize) {
            detach();
            return;
        }

        const StreamChunk chunk = stream_->read(buf, sizeof(buf));
        if (!chunk.ok) {
            detach();
            return;
        }
        if (chunk.ancillary_fd >= 0) {
            pending_ancillary_fd = chunk.ancillary_fd;
        }
        acc_.append(buf, chunk.len);
    }
}

} // namespace rpi_imager::privileged::wire
