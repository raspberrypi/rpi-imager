// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "duplex_connection.h"

#include "frame.h"
#include "server_message.h"

#include <chrono>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rpi_imager::privileged::wire {

namespace {

#if defined(_WIN32)
bool writeAllHandle(void* handle, const char* data, std::size_t len) {
    auto* pipe = static_cast<HANDLE>(handle);
    std::size_t off = 0;
    while (off < len) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(
            (len - off) > 0x7fffffffu ? 0x7fffffffu : (len - off));
        if (!WriteFile(pipe, data + off, chunk, &wrote, nullptr) || wrote == 0) {
            return false;
        }
        off += wrote;
    }
    return true;
}

bool readSomeHandle(void* handle, char* buf, std::size_t cap, std::size_t& out_len) {
    auto* pipe = static_cast<HANDLE>(handle);
    DWORD got = 0;
    const DWORD chunk = static_cast<DWORD>(cap > 0x7fffffffu ? 0x7fffffffu : cap);
    if (!ReadFile(pipe, buf, chunk, &got, nullptr) || got == 0) {
        return false;
    }
    out_len = got;
    return true;
}
#else
bool writeAllFd(int fd, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t n = ::write(fd, data + off, len - off);
        if (n <= 0) {
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

struct SocketReadChunk {
    std::size_t len = 0;
    int ancillary_fd = -1;
    bool ok = false;
};

SocketReadChunk readSomeSocket(int fd, char* buf, std::size_t cap) {
    SocketReadChunk out;
    char cmsg_buf[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov {};
    iov.iov_base = buf;
    iov.iov_len = cap;
    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    const ssize_t n = ::recvmsg(fd, &msg, 0);
    if (n <= 0) {
        return out;
    }
    out.len = static_cast<std::size_t>(n);
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            std::memcpy(&out.ancillary_fd, CMSG_DATA(cmsg), sizeof(out.ancillary_fd));
        }
    }
    out.ok = true;
    return out;
}
#endif

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

#if defined(_WIN32)
bool DuplexConnection::attach(void* pipe_handle) {
    if (pipe_handle == nullptr || pipe_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    detach();
    io_ = pipe_handle;
    stop_.store(false);
    attached_.store(true);
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}
#else
bool DuplexConnection::attach(int sock_fd) {
    if (sock_fd < 0) {
        return false;
    }
    detach();
    io_ = sock_fd;
    stop_.store(false);
    attached_.store(true);
    reader_ = std::thread([this] { readerLoop(); });
    return true;
}
#endif

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
    if (reader_.joinable()) {
        reader_.join();
    }
#if defined(_WIN32)
    io_ = nullptr;
#else
    io_ = -1;
#endif
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

bool DuplexConnection::writeRequest(const proto::WireRequest& req) {
    std::string ser;
    if (!req.SerializeToString(&ser) || ser.size() > kMaxFrameBytes) {
        return false;
    }
    const std::string frame = encodeFrame(ser);
    std::lock_guard<std::mutex> lk(write_mutex_);
#if defined(_WIN32)
    return writeAllHandle(io_, frame.data(), frame.size());
#else
    return writeAllFd(io_, frame.data(), frame.size());
#endif
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

    if (!writeRequest(req)) {
        lk.lock();
        pending_.reset();
#if defined(_WIN32)
        const int err = static_cast<int>(GetLastError());
#else
        const int err = errno;
#endif
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

    if (!writeRequest(req)) {
#if defined(_WIN32)
        const int err = static_cast<int>(GetLastError());
#else
        const int err = errno;
#endif
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

#if defined(_WIN32)
        std::size_t got = 0;
        if (!readSomeHandle(io_, buf, sizeof(buf), got)) {
            detach();
            return;
        }
        acc_.append(buf, got);
#else
        const SocketReadChunk chunk = readSomeSocket(io_, buf, sizeof(buf));
        if (!chunk.ok) {
            detach();
            return;
        }
        if (chunk.ancillary_fd >= 0) {
            pending_ancillary_fd = chunk.ancillary_fd;
        }
        acc_.append(buf, chunk.len);
#endif
    }
}

} // namespace rpi_imager::privileged::wire
