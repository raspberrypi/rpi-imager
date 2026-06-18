// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Duplex wire transport: a background reader demuxes helper->client
// WireServerMessage frames (RPC responses and push events) while the caller
// thread issues synchronous WireRequest RPCs.

#pragma once

#include "frame.h"
#include "proto/imager.pb.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rpi_imager::privileged::wire {

namespace proto = rpi_imager::privileged::proto;

struct RpcResult {
    bool ok = false;
    proto::ErrorInfo error;
    std::string payload;
    int ancillary_fd = -1;
};

class DuplexConnection {
public:
    using EventCallback = std::function<void(const proto::WireEvent&)>;

    DuplexConnection() = default;
    ~DuplexConnection();

    DuplexConnection(const DuplexConnection&) = delete;
    DuplexConnection& operator=(const DuplexConnection&) = delete;

#if defined(_WIN32)
    bool attach(void* pipe_handle);
#else
    bool attach(int sock_fd);
#endif
    void detach();

    bool isAttached() const { return attached_.load(); }

    void setEventCallback(EventCallback cb);

    RpcResult call(proto::WireMethod method,
                   const std::string& request_payload,
                   bool expect_ancillary_fd = false);

private:
    void readerLoop();
    void handleServerPayload(const std::string& payload, int ancillary_fd);
    bool writeRequest(const proto::WireRequest& req);

#if defined(_WIN32)
    void* io_ = nullptr;
#else
    int io_ = -1;
#endif

    std::atomic<bool> attached_{false};
    std::atomic<bool> stop_{false};
    std::thread reader_;
    FrameAccumulator acc_;

    std::mutex rpc_mutex_;
    std::condition_variable rpc_cv_;
    struct PendingRpc {
        std::uint64_t request_id = 0;
        bool complete = false;
        RpcResult result;
    };
    std::unique_ptr<PendingRpc> pending_;

    std::mutex event_mutex_;
    EventCallback event_cb_;

    std::atomic<std::uint64_t> next_request_id_{1};
};

} // namespace rpi_imager::privileged::wire
