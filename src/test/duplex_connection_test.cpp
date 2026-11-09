// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The client's side of the helper connection when the helper goes away. A
// helper that refused the client, crashed or was killed hung up while the
// client waited on a reply, and the client crashed instead of reporting it.

#include <catch2/catch_test_macros.hpp>

#include "wire/duplex_connection.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

namespace wire = rpi_imager::privileged::wire;
namespace proto = rpi_imager::privileged::proto;

namespace {

// Takes the request, then reports end of stream, as a helper does when peer
// authentication turns the client away.
class HangsUpAfterRequest final : public wire::DuplexStream {
public:
    bool write(const char*, std::size_t, int) override {
        std::lock_guard<std::mutex> lk(m_);
        written_ = true;
        cv_.notify_all();
        return true;
    }

    wire::StreamChunk read(char*, std::size_t) override {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::seconds(10), [this] { return written_ || cancelled_; });
        return {};
    }

    void cancel() override {
        std::lock_guard<std::mutex> lk(m_);
        cancelled_ = true;
        cv_.notify_all();
    }

    int lastError() const override { return 0; }

private:
    std::mutex m_;
    std::condition_variable cv_;
    bool written_ = false;
    bool cancelled_ = false;
};

} // namespace

TEST_CASE("A helper that hangs up mid-request fails the request, not the client",
          "[privileged][wire]")
{
    wire::DuplexConnection connection;
    REQUIRE(connection.attach(std::make_unique<HangsUpAfterRequest>()));

    const wire::RpcResult result = connection.call(proto::WIRE_UNMOUNT, "/dev/sdz");

    CHECK_FALSE(result.ok);
    CHECK(result.error.code() == proto::ERROR_DEVICE_DISCONNECTED);
    CHECK_FALSE(connection.isAttached());
}

TEST_CASE("A request after the helper has gone fails straight away", "[privileged][wire]")
{
    wire::DuplexConnection connection;
    REQUIRE(connection.attach(std::make_unique<HangsUpAfterRequest>()));
    (void)connection.call(proto::WIRE_UNMOUNT, "/dev/sdz");

    const wire::RpcResult again = connection.call(proto::WIRE_EJECT, "/dev/sdz");

    CHECK_FALSE(again.ok);
    CHECK(again.error.code() == proto::ERROR_DEVICE_DISCONNECTED);
}
