// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The named pipe between the Windows client and its privileged helper. The
// transport keeps a reader blocked on the pipe at all times, so a write has
// to complete while that read is pending; a stream that serialises the two
// leaves every RPC waiting forever. A hang here is the failure, so each wait
// is bounded and unblocks the stream before reporting it.

#include <catch2/catch_test_macros.hpp>

#include "wire/duplex_connection.h"
#include "wire/frame.h"
#include "wire/server_message.h"
#include "wire/win_pipe_stream.h"

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace std::chrono_literals;
namespace wire = rpi_imager::privileged::wire;
namespace proto = rpi_imager::privileged::proto;

namespace {

constexpr auto kHangLimit = 10s;

std::wstring uniquePipeName() {
    static std::atomic<int> counter{0};
    return L"\\\\.\\pipe\\rpi-imager-test-" + std::to_wstring(GetCurrentProcessId())
           + L"-" + std::to_wstring(counter.fetch_add(1));
}

// Both ends of one pipe, connected; closed on destruction.
struct PipePair {
    HANDLE server = INVALID_HANDLE_VALUE;
    HANDLE client = INVALID_HANDLE_VALUE;

    PipePair() {
        const std::wstring name = uniquePipeName();
        server = wire::createPipeServer(name, nullptr);
        if (server == INVALID_HANDLE_VALUE) return;
        client = wire::openPipeClient(name);
        if (client == INVALID_HANDLE_VALUE) return;
        if (!wire::connectPipeServer(server)) {
            CloseHandle(client);
            client = INVALID_HANDLE_VALUE;
        }
    }
    ~PipePair() {
        if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
        if (server != INVALID_HANDLE_VALUE) CloseHandle(server);
    }
    bool ok() const {
        return server != INVALID_HANDLE_VALUE && client != INVALID_HANDLE_VALUE;
    }
};

std::string readExactly(wire::DuplexStream& s, std::size_t n) {
    std::string out;
    char buf[256];
    while (out.size() < n) {
        const wire::StreamChunk c = s.read(buf, sizeof(buf));
        if (!c.ok) break;
        out.append(buf, c.len);
    }
    return out;
}

} // namespace

TEST_CASE("A write goes out while a read on the same end is waiting", "[win_pipe_stream]") {
    PipePair pipe;
    REQUIRE(pipe.ok());
    wire::WinPipeStream client(pipe.client);
    wire::WinPipeStream server(pipe.server);

    // The read cannot finish until the server speaks, and the server only
    // speaks once it has the write.
    auto reply = std::async(std::launch::async, [&] { return readExactly(client, 4); });
    std::this_thread::sleep_for(100ms);   // let the read reach the pipe

    auto wrote = std::async(std::launch::async,
                            [&] { return client.write("ping", 4, -1); });
    if (wrote.wait_for(kHangLimit) != std::future_status::ready) {
        client.cancel();
        server.cancel();
        FAIL("the write waited behind the pending read");
    }
    CHECK(wrote.get());

    CHECK(readExactly(server, 4) == "ping");
    REQUIRE(server.write("pong", 4, -1));
    REQUIRE(reply.wait_for(kHangLimit) == std::future_status::ready);
    CHECK(reply.get() == "pong");
}

TEST_CASE("Cancelling wakes a read that is waiting, and refuses later ones", "[win_pipe_stream]") {
    PipePair pipe;
    REQUIRE(pipe.ok());
    wire::WinPipeStream client(pipe.client);

    auto blocked = std::async(std::launch::async, [&] {
        char buf[16];
        return client.read(buf, sizeof(buf)).ok;
    });
    std::this_thread::sleep_for(100ms);
    client.cancel();

    REQUIRE(blocked.wait_for(kHangLimit) == std::future_status::ready);
    CHECK_FALSE(blocked.get());

    char buf[16];
    CHECK_FALSE(client.read(buf, sizeof(buf)).ok);
}

TEST_CASE("A descriptor cannot be attached to a pipe write", "[win_pipe_stream]") {
    PipePair pipe;
    REQUIRE(pipe.ok());
    wire::WinPipeStream client(pipe.client);

    CHECK_FALSE(client.write("x", 1, 3));
    CHECK(client.lastError() == static_cast<int>(ERROR_NOT_SUPPORTED));
}

TEST_CASE("A call over the pipe gets its answer", "[win_pipe_stream]") {
    PipePair pipe;
    REQUIRE(pipe.ok());
    wire::WinPipeStream server(pipe.server);

    // A helper that answers one request, echoing its id.
    std::thread helper([&] {
        wire::FrameAccumulator acc;
        std::string payload;
        char buf[1024];
        bool oversize = false;
        while (!acc.next(payload, oversize)) {
            const wire::StreamChunk c = server.read(buf, sizeof(buf));
            if (!c.ok) return;
            acc.append(buf, c.len);
        }
        proto::WireRequest req;
        if (!req.ParseFromString(payload)) return;
        proto::WireResponse resp;
        resp.set_request_id(req.request_id());
        resp.set_payload("hello back");
        const std::string frame = wire::encodeServerResponse(resp);
        (void)server.write(frame.data(), frame.size(), -1);
    });

    wire::DuplexConnection duplex;
    REQUIRE(duplex.attach(std::make_unique<wire::WinPipeStream>(pipe.client)));

    auto result = std::async(std::launch::async,
                             [&] { return duplex.call(proto::WIRE_HELLO, "hello"); });
    const bool answered = result.wait_for(kHangLimit) == std::future_status::ready;
    if (!answered) {
        duplex.detach();
        server.cancel();
    }
    helper.join();
    REQUIRE(answered);

    const wire::RpcResult r = result.get();
    CHECK(r.ok);
    CHECK(r.payload == "hello back");
    duplex.detach();
}
