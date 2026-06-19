// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Duplex wire transport: a background reader demuxes helper->client
// WireServerMessage frames (RPC responses and push events) while caller
// threads issue synchronous or pipelined async WireRequest RPCs.

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
#include <unordered_map>

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
    using AsyncCallback = std::function<void(RpcResult)>;

    // Max in-flight async RPC responses; configured via setMaxOutstandingAsync().
    static constexpr std::size_t kDefaultMaxOutstandingAsync = 8;

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

    // Blocking control-plane RPC (one at a time).
    RpcResult call(proto::WireMethod method,
                   const std::string& request_payload,
                   bool expect_ancillary_fd = false);

    // Pipelined RPC: returns after the request is on the wire. Completion runs
    // on the reader thread. Blocks only when kMaxOutstandingAsync responses
    // are already awaiting demux (back-pressure matching the helper queue).
    void submitAsync(proto::WireMethod method,
                     const std::string& request_payload,
                     AsyncCallback on_complete);

    // Match the client/helper async pipeline depth (ring slot count).
    void setMaxOutstandingAsync(std::size_t depth);
    std::size_t maxOutstandingAsync() const;

private:
    void readerLoop();
    void handleServerPayload(const std::string& payload, int ancillary_fd);
    bool writeRequest(const proto::WireRequest& req);
    void failAllOutstandingAsyncLocked(const proto::ErrorInfo& err);
    RpcResult rpcResultFromResponse(const proto::WireResponse& response, int ancillary_fd);

#if defined(_WIN32)
    void* io_ = nullptr;
#else
    int io_ = -1;
#endif

    std::atomic<bool> attached_{false};
    std::atomic<bool> stop_{false};
    std::thread reader_;
    FrameAccumulator acc_;

    std::mutex write_mutex_;

    std::mutex rpc_mutex_;
    std::condition_variable rpc_cv_;
    struct PendingRpc {
        std::uint64_t request_id = 0;
        bool complete = false;
        RpcResult result;
    };
    std::unique_ptr<PendingRpc> pending_;

    std::mutex async_mutex_;
    std::condition_variable async_cv_;
    std::unordered_map<std::uint64_t, AsyncCallback> outstanding_async_;

    std::mutex event_mutex_;
    EventCallback event_cb_;

    std::atomic<std::uint64_t> next_request_id_{1};
    std::size_t max_outstanding_async_ = kDefaultMaxOutstandingAsync;
};

} // namespace rpi_imager::privileged::wire
