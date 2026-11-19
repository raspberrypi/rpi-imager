// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The Linux client backend and the real helper loop over a socketpair, so
// every message crosses the wire as in production, without pkexec or root.
// The helper opens only /dev/* paths, so the card is a file in /dev/shm.

#include <catch2/catch_test_macros.hpp>

#include "backends/linux_polkit.h"
#include "linux/service_impl.h"
#include "wire/duplex_connection.h"
#include "wire/frame.h"
#include "wire/linux_socket_stream.h"
#include "wire/protocol.h"
#include "wire/server_message.h"

#include <gnutls/crypto.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace wire = rpi_imager::privileged::wire;
namespace proto = rpi_imager::privileged::proto;
using rpi_imager::privileged::backends::LinuxPolkitBackend;

namespace {

constexpr std::size_t kCardBytes = 1024 * 1024;

class HelperPeer {
public:
    HelperPeer() {
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds_) == 0) {
            server_ = std::thread([fd = fds_[1]] { rpi_imager::writer::ServeClientLinux(fd); });
        }
    }
    ~HelperPeer() {
        hangUpClientEnd();
        if (server_.joinable()) {
            server_.join();
        }
        if (fds_[1] >= 0) {
            ::close(fds_[1]);
        }
    }

    bool ok() const { return server_.joinable(); }
    int clientFd() const { return fds_[0]; }
    int helperFd() const { return fds_[1]; }

    int releaseClientFd() {
        const int fd = fds_[0];
        fds_[0] = -1;
        return fd;
    }

    void hangUpClientEnd() {
        if (fds_[0] >= 0) {
            ::close(fds_[0]);
            fds_[0] = -1;
        }
    }

    void join() {
        if (server_.joinable()) {
            server_.join();
        }
    }

private:
    int fds_[2] = {-1, -1};
    std::thread server_;
};

std::unique_ptr<LinuxPolkitBackend> connectedBackend(HelperPeer& peer) {
    LinuxPolkitBackend::Options opts;
    opts.connected_socket = peer.releaseClientFd();
    auto backend = std::make_unique<LinuxPolkitBackend>(opts);
    REQUIRE(backend->installHelper().ok);
    return backend;
}

class ScratchCard {
public:
    explicit ScratchCard(std::size_t bytes = kCardBytes) : bytes_(bytes) {
        char tmpl[] = "/dev/shm/rpi-imager-wire-XXXXXX";
        const int fd = ::mkstemp(tmpl);
        if (fd < 0) {
            return;
        }
        if (::ftruncate(fd, static_cast<off_t>(bytes)) == 0) {
            path_ = tmpl;
        } else {
            ::unlink(tmpl);
        }
        ::close(fd);
    }
    ~ScratchCard() {
        if (!path_.empty()) {
            ::unlink(path_.c_str());
        }
    }
    const std::string& path() const { return path_; }

    std::string contents() const {
        std::string out(bytes_, '\0');
        const int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
        const ssize_t n = fd >= 0 ? ::pread(fd, out.data(), out.size(), 0) : -1;
        if (fd >= 0) {
            ::close(fd);
        }
        out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
        return out;
    }

private:
    std::size_t bytes_;
    std::string path_;
};

std::string sha256(const std::string& data) {
    unsigned char digest[32];
    REQUIRE(gnutls_hash_fast(GNUTLS_DIG_SHA256, data.data(), data.size(), digest) == 0);
    return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}

std::string pattern(std::size_t n, unsigned seed) {
    std::string out(n, '\0');
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = static_cast<char>((i * 131u + seed) & 0xFF);
    }
    return out;
}

std::string firstDifference(const std::string& got, const std::string& want) {
    if (got.size() != want.size()) {
        return "sizes " + std::to_string(got.size()) + " and " + std::to_string(want.size());
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i] != want[i]) {
            return "first difference at byte " + std::to_string(i);
        }
    }
    return "identical";
}

proto::SessionId bogusSession() {
    proto::SessionId sid;
    sid.set_v(987654);
    return sid;
}

std::string serialised(const google::protobuf::MessageLite& m) {
    std::string out;
    (void)m.SerializeToString(&out);
    return out;
}

} // namespace

TEST_CASE("The client and helper agree on the protocol", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    CHECK(backend->helperActive());
    const auto status = backend->queryHelperStatus();
    REQUIRE(status.ok);
    CHECK(status.value.state() == proto::HELPER_STATE_INSTALLED_READY);
    CHECK(status.value.installed_version() == std::to_string(wire::kProtocolVersion));
    CHECK(status.value.client_version() == std::to_string(wire::kProtocolVersion));
}

TEST_CASE("The drive list crosses the wire whole", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    const auto summaries = backend->listDrivesNow();
    REQUIRE(summaries.ok);
    const auto devices = backend->listDevicesNow();
    REQUIRE(devices.ok);
    CHECK(devices.value.size() == summaries.value.size());
}

TEST_CASE("Drive events reach the subscriber, and only drive events",
          "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    const int helper_fd = peer.helperFd();
    auto backend = connectedBackend(peer);

    CHECK_FALSE(backend->subscribeDrives(nullptr).ok);

    std::mutex m;
    std::condition_variable cv;
    std::vector<proto::DriveChange> seen;
    const auto record = [&](const proto::DriveChange& c) {
        std::lock_guard<std::mutex> lk(m);
        seen.push_back(c);
        cv.notify_all();
    };
    REQUIRE(backend->subscribeDrives(record).ok);
    REQUIRE(backend->subscribeDrives(record).ok);

    // Written on the helper's end while it idles.
    proto::WireEvent other;
    other.set_kind(static_cast<proto::WireEventKind>(99));
    proto::WireEvent unknown_change;
    unknown_change.set_kind(proto::WIRE_EVENT_DRIVE_CHANGED);
    proto::WireEvent added;
    added.set_kind(proto::WIRE_EVENT_DRIVE_CHANGED);
    added.mutable_change()->set_kind(proto::DriveChange::KIND_ADDED);
    const std::string frames = wire::encodeServerEvent(other)
                               + wire::encodeServerEvent(unknown_change)
                               + wire::encodeServerEvent(added);
    REQUIRE(::write(helper_fd, frames.data(), frames.size())
            == static_cast<ssize_t>(frames.size()));

    std::unique_lock<std::mutex> lk(m);
    REQUIRE(cv.wait_for(lk, std::chrono::seconds(10), [&] { return seen.size() >= 2; }));
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].kind() == proto::DriveChange::KIND_UPDATED);
    CHECK(seen[1].kind() == proto::DriveChange::KIND_ADDED);
    lk.unlock();

    CHECK(backend->unsubscribeDrives().ok);
    CHECK(backend->unsubscribeDrives().ok);
}

TEST_CASE("The helper refuses what it must not open", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    const auto outside = backend->openSession("/tmp/not-a-device", {});
    CHECK_FALSE(outside.ok);
    CHECK(outside.error.code() == proto::ERROR_DEVICE_PERMISSION);

    const auto missing = backend->openSession("/dev/rpi-imager-no-such-device", {});
    CHECK_FALSE(missing.ok);
    CHECK(missing.error.code() != proto::ERROR_NONE);

    CHECK_FALSE(backend->unmount("/dev/rpi-imager-no-such-device").ok);
    CHECK_FALSE(backend->eject("/dev/rpi-imager-no-such-device").ok);
    CHECK_FALSE(backend->openUsbDevice(999, 999).ok);
}

TEST_CASE("Every session request names a session the helper holds",
          "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);
    const proto::SessionId sid = bogusSession();
    std::byte buf[16] = {};

    auto notFound = [](const proto::ErrorInfo& e) {
        return e.code() == proto::ERROR_SESSION_NOT_FOUND;
    };
    CHECK(notFound(backend->queryDeviceLimits(sid).error));
    CHECK(notFound(backend->prepareDevice(sid, {}).error));
    CHECK(notFound(backend->readChunk(sid, 0, buf, sizeof(buf)).error));
    CHECK(notFound(backend->writeChunk(sid, 0, buf, sizeof(buf)).error));
    CHECK(notFound(backend->syncDevice(sid).error));
    CHECK(notFound(backend->hashDeviceSha256(sid, "", 0, 16).error));
    CHECK(notFound(backend->closeSession(sid).error));
    CHECK(notFound(backend->mapBulkBuffer(sid, 0).error));
    CHECK(notFound(backend->mapBulkBuffer(sid, 4096).error));
}

TEST_CASE("The ring-slot calls are not offered on Linux", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    CHECK(backend->acquireSlot(bogusSession()).error.code() == proto::ERROR_NOT_IMPLEMENTED);
    proto::WriteResult seen;
    backend->submitWrite(bogusSession(), 0, 0, 0,
                         [&](const proto::WriteResult& wr) { seen = wr; });
    CHECK(seen.error().code() == proto::ERROR_NOT_IMPLEMENTED);
    backend->submitWrite(bogusSession(), 0, 0, 0, nullptr);
}

TEST_CASE("A bulk write needs a mapped buffer and stays inside it",
          "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    CHECK_FALSE(backend->bulkWriteFromBuffer(bogusSession(), 0, 0, 512).ok);

    bool called = false;
    bool ok = true;
    backend->submitBulkWriteAsync(bogusSession(), 0, 0, 512,
                                  [&](rpi_imager::privileged::Result<std::size_t> r) {
                                      called = true;
                                      ok = r.ok;
                                  });
    CHECK(called);
    CHECK_FALSE(ok);
    backend->submitBulkWriteAsync(bogusSession(), 0, 0, 512, nullptr);
}

TEST_CASE("A card is written, read back, hashed and closed through the helper",
          "[privileged][linux][wire]")
{
    ScratchCard card;
    if (card.path().empty()) {
        SKIP("no writable /dev/shm");
    }
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    const auto opened = backend->openSession(card.path(), {});
    REQUIRE(opened.ok);
    const proto::SessionId sid = opened.value;

    const auto limits = backend->queryDeviceLimits(sid);
    REQUIRE(limits.ok);
    CHECK(limits.value.total_bytes() == kCardBytes);

    const std::string head = pattern(64 * 1024, 1);
    const auto wrote = backend->writeChunk(
        sid, 0, reinterpret_cast<const std::byte*>(head.data()), head.size());
    REQUIRE(wrote.ok);
    CHECK(wrote.value == head.size());

    std::string back(head.size(), '\0');
    const auto read = backend->readChunk(sid, 0, reinterpret_cast<std::byte*>(back.data()),
                                         back.size());
    REQUIRE(read.ok);
    CHECK(read.value == head.size());
    CHECK(back == head);

    const std::string huge(8u * 1024 * 1024 + 1, 'x');
    CHECK(backend->writeChunk(sid, 0, reinterpret_cast<const std::byte*>(huge.data()),
                              huge.size()).error.code() == proto::ERROR_WRITE_FAILED);
    std::vector<std::byte> big(huge.size());
    CHECK(backend->readChunk(sid, 0, big.data(), big.size()).error.code()
          == proto::ERROR_DEVICE_IO);

    const auto mapped = backend->mapBulkBuffer(sid, 256 * 1024, 2);
    INFO(mapped.error.detail());
    REQUIRE(mapped.ok);
    REQUIRE(backend->bulkBufferBase() != nullptr);
    REQUIRE(backend->bulkBufferSize() >= 256 * 1024);
    const std::string body = pattern(256 * 1024, 7);
    std::memcpy(backend->bulkBufferBase(), body.data(), body.size());

    const auto bulk = backend->bulkWriteFromBuffer(sid, 0, 64 * 1024, 128 * 1024);
    REQUIRE(bulk.ok);
    CHECK(bulk.value == 128 * 1024);
    CHECK_FALSE(backend->bulkWriteFromBuffer(sid, 0, 0, 0).ok);
    CHECK_FALSE(backend->bulkWriteFromBuffer(sid, 255 * 1024, 0, 2048).ok);

    std::mutex m;
    std::condition_variable cv;
    int done = 0;
    std::size_t async_bytes = 0;
    bool async_ok = true;
    for (int i = 0; i < 2; ++i) {
        backend->submitBulkWriteAsync(
            sid, 128 * 1024 + i * 64 * 1024, 192 * 1024 + i * 64 * 1024, 64 * 1024,
            [&](rpi_imager::privileged::Result<std::size_t> r) {
                std::lock_guard<std::mutex> lk(m);
                async_ok = async_ok && r.ok;
                async_bytes += r.ok ? r.value : 0;
                ++done;
                cv.notify_all();
            });
    }
    {
        std::unique_lock<std::mutex> lk(m);
        REQUIRE(cv.wait_for(lk, std::chrono::seconds(30), [&] { return done == 2; }));
    }
    CHECK(async_ok);
    CHECK(async_bytes == 128 * 1024);

    REQUIRE(backend->syncDevice(sid).ok);

    std::string expected(kCardBytes, '\0');
    expected.replace(0, head.size(), head);
    expected.replace(64 * 1024, 128 * 1024, body.substr(0, 128 * 1024));
    expected.replace(192 * 1024, 128 * 1024, body.substr(128 * 1024, 128 * 1024));
    const std::string landed = card.contents();
    INFO(firstDifference(landed, expected));
    CHECK(landed == expected);

    const auto whole = backend->hashDeviceSha256(sid, "prefix", 0, kCardBytes);
    REQUIRE(whole.ok);
    CHECK(whole.value == sha256("prefix" + expected));

    CHECK(backend->hashDeviceSha256Chunk(sid, "", 0, 4096, false, false).error.code()
          == proto::ERROR_UNKNOWN);
    REQUIRE(backend->hashDeviceSha256Chunk(sid, "prefix", 0, kCardBytes / 2, true, false).ok);
    const auto pieces =
        backend->hashDeviceSha256Chunk(sid, "", kCardBytes / 2, kCardBytes / 2, false, true);
    REQUIRE(pieces.ok);
    CHECK(pieces.value == whole.value);

    CHECK_FALSE(backend->hashDeviceSha256(sid, "", kCardBytes - 512, 4096).ok);

    REQUIRE(backend->mapBulkBuffer(sid, 0).ok);
    CHECK(backend->bulkBufferBase() == nullptr);

    const auto stats = backend->closeSession(sid);
    REQUIRE(stats.ok);
    CHECK(stats.value.bytes_written() >= 256 * 1024);
    CHECK(backend->closeSession(sid).error.code() == proto::ERROR_SESSION_NOT_FOUND);
}

TEST_CASE("A card is prepared through the helper", "[privileged][linux][wire]")
{
    constexpr std::size_t kBytes = 3 * 1024 * 1024;
    ScratchCard card(kBytes);
    if (card.path().empty()) {
        SKIP("no writable /dev/shm");
    }
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    const auto opened = backend->openSession(card.path(), {});
    REQUIRE(opened.ok);
    const std::string junk(4096, '\xAA');
    for (const std::uint64_t at : {std::uint64_t{0}, std::uint64_t{kBytes / 2},
                                   std::uint64_t{kBytes - junk.size()}}) {
        REQUIRE(backend->writeChunk(opened.value, at,
                                    reinterpret_cast<const std::byte*>(junk.data()),
                                    junk.size()).ok);
    }

    proto::PrepareOptions opts;
    opts.set_zero_last_mb(true);
    REQUIRE(backend->prepareDevice(opened.value, opts).ok);
    REQUIRE(backend->syncDevice(opened.value).ok);

    const std::string after = card.contents();
    REQUIRE(after.size() == kBytes);
    CHECK(after.substr(0, junk.size()) == std::string(junk.size(), '\0'));
    CHECK(after.substr(kBytes / 2, junk.size()) == junk);
    CHECK(after.substr(kBytes - junk.size()) == std::string(junk.size(), '\0'));
    CHECK(backend->closeSession(opened.value).ok);
}

TEST_CASE("A device error the helper reports leaves the connection up",
          "[privileged][linux][wire]")
{
    // Read as a lost helper, this started a second through pkexec.
    ScratchCard card;
    if (card.path().empty()) {
        SKIP("no writable /dev/shm");
    }
    HelperPeer peer;
    REQUIRE(peer.ok());
    auto backend = connectedBackend(peer);

    const auto opened = backend->openSession(card.path(), {});
    REQUIRE(opened.ok);
    std::vector<std::byte> big(8u * 1024 * 1024 + 1);
    REQUIRE(backend->readChunk(opened.value, 0, big.data(), big.size()).error.code()
            == proto::ERROR_DEVICE_IO);

    CHECK(backend->helperActive());
    CHECK(backend->queryDeviceLimits(opened.value).ok);
    CHECK(backend->closeSession(opened.value).ok);
}

TEST_CASE("A reply that does not parse fails the call", "[privileged][linux][wire]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);

    // Answers every request "successfully" with a payload no message parses:
    // a length-delimited field claiming more bytes than follow.
    std::thread helper([fd = fds[1]] {
        wire::FrameAccumulator acc;
        char buf[8192];
        std::string frame;
        for (;;) {
            const ssize_t got = ::read(fd, buf, sizeof(buf));
            if (got <= 0) {
                return;
            }
            acc.append(buf, static_cast<std::size_t>(got));
            bool oversize = false;
            while (acc.next(frame, oversize)) {
                proto::WireRequest req;
                if (!req.ParseFromString(frame)) {
                    return;
                }
                proto::WireResponse resp;
                resp.set_request_id(req.request_id());
                resp.set_payload(std::string("\x0a\x7f", 2));
                const std::string out = wire::encodeServerResponse(resp);
                if (::write(fd, out.data(), out.size()) != static_cast<ssize_t>(out.size())) {
                    return;
                }
            }
        }
    });

    {
        LinuxPolkitBackend::Options opts;
        opts.connected_socket = fds[0];
        LinuxPolkitBackend backend(opts);
        REQUIRE(backend.installHelper().ok);

        const proto::SessionId sid = bogusSession();
        std::byte buf[16] = {};
        auto malformed = [](const proto::ErrorInfo& e) {
            return e.code() == proto::ERROR_UNKNOWN
                   && e.detail().find("malformed") != std::string::npos;
        };
        CHECK(malformed(backend.queryHelperStatus().error));
        CHECK(malformed(backend.listDrivesNow().error));
        CHECK(malformed(backend.listDevicesNow().error));
        CHECK(malformed(backend.openSession("/dev/sdz", {}).error));
        CHECK(malformed(backend.queryDeviceLimits(sid).error));
        CHECK(malformed(backend.readChunk(sid, 0, buf, sizeof(buf)).error));
        CHECK(malformed(backend.writeChunk(sid, 0, buf, sizeof(buf)).error));
        CHECK(malformed(backend.hashDeviceSha256(sid, "", 0, 16).error));
        CHECK(malformed(backend.closeSession(sid).error));
        CHECK(backend.helperActive());
    }
    ::shutdown(fds[1], SHUT_RDWR);
    helper.join();
    ::close(fds[1]);
}

TEST_CASE("A helper that goes away fails every later call", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    const int helper_fd = peer.helperFd();
    auto backend = connectedBackend(peer);

    ::shutdown(helper_fd, SHUT_RDWR);
    peer.join();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (backend->helperActive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto first = backend->listDrivesNow();
    CHECK_FALSE(first.ok);
    CHECK(first.error.code() == proto::ERROR_DEVICE_DISCONNECTED);
    CHECK_FALSE(backend->helperActive());

    const auto again = backend->listDrivesNow();
    CHECK_FALSE(again.ok);
    CHECK(again.error.code() == proto::ERROR_DEVICE_DISCONNECTED);

    CHECK(backend->uninstallHelper().ok);
    CHECK_FALSE(backend->installHelper().ok);
}

TEST_CASE("Writing to a peer that has gone fails rather than killing the writer",
          "[privileged][linux][wire]")
{
    // Neither process ignores SIGPIPE.
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
    ::close(fds[1]);
    wire::LinuxSocketStream stream(fds[0]);

    CHECK_FALSE(stream.write("x", 1, -1));
    CHECK(stream.lastError() == EPIPE);
    CHECK_FALSE(stream.write("x", 1, fds[0]));
    CHECK(stream.lastError() == EPIPE);

    stream.cancel();
    char c;
    CHECK_FALSE(stream.read(&c, 1).ok);
    ::close(fds[0]);
}

TEST_CASE("The helper answers a request it cannot parse with an error",
          "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    wire::DuplexConnection conn;
    REQUIRE(conn.attach(std::make_unique<wire::LinuxSocketStream>(peer.clientFd())));

    const std::string garbage("\x0a\x7f", 2);
    for (const auto method :
         {proto::WIRE_HELLO, proto::WIRE_OPEN_SESSION, proto::WIRE_QUERY_DEVICE_LIMITS,
          proto::WIRE_PREPARE_DEVICE, proto::WIRE_WRITE_CHUNK, proto::WIRE_READ_CHUNK,
          proto::WIRE_SYNC_DEVICE, proto::WIRE_HASH_DEVICE_SHA256, proto::WIRE_CLOSE_SESSION,
          proto::WIRE_MAP_BULK_BUFFER, proto::WIRE_BULK_WRITE, proto::WIRE_UNMOUNT,
          proto::WIRE_EJECT, proto::WIRE_OPEN_USB_DEVICE}) {
        CAPTURE(method);
        const auto r = conn.call(method, garbage);
        CHECK_FALSE(r.ok);
        CHECK(r.error.code() == proto::ERROR_UNKNOWN);
    }

    const auto unknown = conn.call(static_cast<proto::WireMethod>(999), "");
    CHECK(unknown.error.code() == proto::ERROR_NOT_IMPLEMENTED);
    CHECK(conn.isAttached());
    conn.detach();
}

TEST_CASE("The helper tells a client on another version so", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    wire::DuplexConnection conn;
    REQUIRE(conn.attach(std::make_unique<wire::LinuxSocketStream>(peer.clientFd())));

    proto::ProtocolVersion pv;
    pv.set_version(wire::kProtocolVersion + 1);
    const auto r = conn.call(proto::WIRE_HELLO, serialised(pv));
    CHECK_FALSE(r.ok);
    CHECK(r.error.code() == proto::ERROR_HELPER_VERSION_MISMATCH);

    const auto status = conn.call(proto::WIRE_QUERY_HELPER_STATUS, "");
    REQUIRE(status.ok);
    proto::HelperStatus s;
    REQUIRE(s.ParseFromString(status.payload));
    CHECK(s.state() == proto::HELPER_STATE_INSTALLED_READY);
    conn.detach();
}

TEST_CASE("Pipelined requests are answered in any order", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    wire::DuplexConnection conn;
    conn.setMaxOutstandingAsync(0);
    CHECK(conn.maxOutstandingAsync() == 1);
    conn.setMaxOutstandingAsync(4);
    REQUIRE(conn.attach(std::make_unique<wire::LinuxSocketStream>(peer.clientFd())));

    std::mutex m;
    std::condition_variable cv;
    int done = 0;
    for (int i = 0; i < 8; ++i) {
        conn.submitAsync(proto::WIRE_QUERY_HELPER_STATUS, "", [&](wire::RpcResult r) {
            std::lock_guard<std::mutex> lk(m);
            done += r.ok ? 1 : 100;
            cv.notify_all();
        });
    }
    conn.submitAsync(proto::WIRE_QUERY_HELPER_STATUS, "", nullptr);
    std::unique_lock<std::mutex> lk(m);
    REQUIRE(cv.wait_for(lk, std::chrono::seconds(10), [&] { return done >= 8; }));
    CHECK(done == 8);
    lk.unlock();
    conn.detach();

    bool failed = false;
    conn.submitAsync(proto::WIRE_QUERY_HELPER_STATUS, "", [&](wire::RpcResult r) {
        failed = !r.ok && r.error.code() == proto::ERROR_DEVICE_DISCONNECTED;
    });
    CHECK(failed);
}

TEST_CASE("A reply without the descriptor it promised is a failure",
          "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    wire::DuplexConnection conn;
    REQUIRE(conn.attach(std::make_unique<wire::LinuxSocketStream>(peer.clientFd())));

    const auto r = conn.call(proto::WIRE_QUERY_HELPER_STATUS, "", true);
    CHECK_FALSE(r.ok);
    CHECK(r.error.code() == proto::ERROR_UNKNOWN);
    conn.detach();
}

TEST_CASE("The helper drops a client that breaks the framing", "[privileged][linux][wire]")
{
    HelperPeer peer;
    REQUIRE(peer.ok());
    const int fd = peer.clientFd();

    SECTION("a frame that is not a request") {
        const std::string frame = wire::encodeFrame(std::string("\x0a\x7f", 2));
        REQUIRE(::write(fd, frame.data(), frame.size()) == static_cast<ssize_t>(frame.size()));
    }
    SECTION("a length beyond the cap") {
        const std::uint32_t n = wire::kMaxFrameBytes + 1;
        const char header[4] = {static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF),
                                static_cast<char>((n >> 16) & 0xFF),
                                static_cast<char>((n >> 24) & 0xFF)};
        REQUIRE(::write(fd, header, sizeof(header)) == 4);
    }

    peer.join();
}

TEST_CASE("The client drops a helper that breaks the framing", "[privileged][linux][wire]")
{
    int fds[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
    wire::DuplexConnection conn;
    REQUIRE(conn.attach(std::make_unique<wire::LinuxSocketStream>(fds[0])));

    SECTION("a reply that is not a server message") {
        const std::string frame = wire::encodeFrame(std::string("\x0a\x7f", 2));
        REQUIRE(::write(fds[1], frame.data(), frame.size())
                == static_cast<ssize_t>(frame.size()));
    }
    SECTION("a length beyond the cap") {
        const std::uint32_t n = wire::kMaxFrameBytes + 1;
        const char header[4] = {static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF),
                                static_cast<char>((n >> 16) & 0xFF),
                                static_cast<char>((n >> 24) & 0xFF)};
        REQUIRE(::write(fds[1], header, sizeof(header)) == 4);
    }
    SECTION("a reply to nothing that was asked") {
        proto::WireResponse stray;
        stray.set_request_id(424242);
        const std::string frame = wire::encodeServerResponse(stray);
        REQUIRE(::write(fds[1], frame.data(), frame.size())
                == static_cast<ssize_t>(frame.size()));
        ::shutdown(fds[1], SHUT_WR);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (conn.isAttached() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_FALSE(conn.isAttached());
    conn.detach();
    ::close(fds[0]);
    ::close(fds[1]);
}
