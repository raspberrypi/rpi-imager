// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the Linux helper's request loop: the input is a sequence of calls,
// each a method byte, a length byte and that many payload bytes.
//
// The helper writes whatever block device it is named, so this refuses to
// run outside the sandbox run_fuzzers.sh provides.
//
// Every reply must decode and answer a request that was sent, once.
#include "linux/service_impl.h"
#include "wire/frame.h"
#include "wire/server_message.h"
#include "proto/imager.pb.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace proto = rpi_imager::privileged::proto;
namespace wire = rpi_imager::privileged::wire;

namespace {

constexpr proto::WireMethod kMethods[] = {
    proto::WIRE_HELLO,           proto::WIRE_QUERY_HELPER_STATUS,
    proto::WIRE_LIST_DRIVES,     proto::WIRE_SUBSCRIBE_DRIVES,
    proto::WIRE_UNSUBSCRIBE_DRIVES, proto::WIRE_OPEN_SESSION,
    proto::WIRE_QUERY_DEVICE_LIMITS, proto::WIRE_PREPARE_DEVICE,
    proto::WIRE_MAP_BULK_BUFFER, proto::WIRE_BULK_WRITE,
    proto::WIRE_READ_CHUNK,      proto::WIRE_WRITE_CHUNK,
    proto::WIRE_SYNC_DEVICE,     proto::WIRE_HASH_DEVICE_SHA256,
    proto::WIRE_CLOSE_SESSION,   proto::WIRE_UNMOUNT,
    proto::WIRE_EJECT,           proto::WIRE_OPEN_USB_DEVICE,
};
constexpr std::size_t kMethodCount = sizeof(kMethods) / sizeof(kMethods[0]);
constexpr std::size_t kMaxCalls = 24;

void writeAll(int fd, const std::string& s) {
    std::size_t done = 0;
    while (done < s.size()) {
        const ssize_t n = ::send(fd, s.data() + done, s.size() - done, MSG_NOSIGNAL);
        if (n <= 0) return;
        done += static_cast<std::size_t>(n);
    }
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int*, char***) {
    const char* sandboxed = std::getenv("RPI_FUZZ_SANDBOXED");
    if (::geteuid() == 0 || !sandboxed || std::string(sandboxed) != "1") {
        std::fprintf(stderr, "fuzz_helper_wire: run it through run_fuzzers.sh, "
                             "which sandboxes it, and never as root\n");
        std::abort();
    }
    std::signal(SIGPIPE, SIG_IGN);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    std::string stream;
    std::set<std::uint64_t> sent;
    std::uint64_t id = 1;
    std::size_t i = 0;
    while (i + 2 <= size && sent.size() < kMaxCalls) {
        const std::uint8_t m = data[i];
        const std::size_t len = std::min<std::size_t>(data[i + 1], size - i - 2);
        i += 2;
        proto::WireRequest req;
        req.set_request_id(id);
        // Past the table, the byte is the raw method number.
        req.set_method(m < kMethodCount ? kMethods[m]
                                        : static_cast<proto::WireMethod>(m - kMethodCount));
        req.set_payload(reinterpret_cast<const char*>(data + i), len);
        i += len;
        std::string bytes;
        (void)req.SerializeToString(&bytes);
        stream += wire::encodeFrame(bytes);
        sent.insert(id++);
    }

    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) return 0;

    std::thread writer([&] {
        writeAll(sv[0], stream);
        ::shutdown(sv[0], SHUT_WR);
    });
    std::thread reader([&] {
        wire::FrameAccumulator acc;
        std::set<std::uint64_t> answered;
        char buf[8192];
        for (;;) {
            std::string frame;
            bool oversize = false;
            while (acc.next(frame, oversize)) {
                proto::WireResponse resp;
                proto::WireEvent ev;
                bool is_event = false;
                if (!wire::decodeServerMessage(frame, resp, ev, is_event)) __builtin_trap();
                if (is_event) continue;
                if (!sent.count(resp.request_id())) __builtin_trap();
                if (!answered.insert(resp.request_id()).second) __builtin_trap();
            }
            if (oversize) __builtin_trap();
            const ssize_t n = ::read(sv[0], buf, sizeof(buf));
            if (n <= 0) break;
            acc.append(buf, static_cast<std::size_t>(n));
        }
    });

    rpi_imager::writer::ServeClientLinux(sv[1]);
    ::close(sv[1]);
    writer.join();
    reader.join();
    ::close(sv[0]);
    return 0;
}
