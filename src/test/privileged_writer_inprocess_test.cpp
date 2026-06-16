// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Round-trip and basic-API tests for InProcessTestBackend.
//
// These exercise the IPrivilegedWriter interface end-to-end against a
// tempfile-backed pseudo-device. They run in milliseconds, with no
// privilege and no real disk.

#include <catch2/catch_test_macros.hpp>

#include "privileged_io/privileged_writer.h"
#include "privileged_io/backends/in_process_test.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

using namespace rpi_imager::privileged;
using rpi_imager::privileged::backends::InProcessTestBackend;

namespace proto = rpi_imager::privileged::proto;

namespace {

// Helper: open a session on a backend with sensible defaults.
proto::SessionId openTestSession(IPrivilegedWriter& w,
                                  const std::string& device_path = "") {
    proto::OpenOptions opts;
    opts.set_direct_io(false);
    opts.set_async_queue_depth(0);
    auto r = w.openSession(device_path, opts);
    REQUIRE(r.ok);
    return r.value;
}

// Helper: synchronous wrapper around the async submitWrite, used in tests
// where we just want to push N writes and wait for all to complete.
struct SubmitFuture {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    proto::WriteResult result;

    void wait() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return done; });
    }

    void set(const proto::WriteResult& r) {
        std::unique_lock<std::mutex> lk(m);
        result = r;
        done = true;
        cv.notify_all();
    }
};

}  // namespace

TEST_CASE("InProcessTestBackend: open + close session", "[privileged_io]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 1ull * 1024 * 1024;   // 1 MB
    opts.slot_count = 2;
    opts.slot_capacity = 64ull * 1024;        // 64 KB
    InProcessTestBackend w(opts);

    auto sid = openTestSession(w);
    auto stats = w.closeSession(sid);
    REQUIRE(stats.ok);
    REQUIRE(stats.value.state() == proto::SESSION_STATE_SUCCEEDED);
    REQUIRE(stats.value.bytes_written() == 0);
}

TEST_CASE("InProcessTestBackend: list drives returns synthetic entry",
          "[privileged_io]") {
    InProcessTestBackend w;
    auto drives = w.listDrivesNow();
    REQUIRE(drives.ok);
    REQUIRE(drives.value.size() == 1);
    REQUIRE(drives.value[0].is_removable());
}

TEST_CASE("InProcessTestBackend: device limits reflect Options",
          "[privileged_io]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 4ull * 1024 * 1024;
    opts.slot_capacity = 256ull * 1024;
    InProcessTestBackend w(opts);

    auto sid = openTestSession(w);
    auto limits = w.queryDeviceLimits(sid);
    REQUIRE(limits.ok);
    REQUIRE(limits.value.total_bytes() == opts.device_bytes);
    REQUIRE(limits.value.max_transfer_bytes() == opts.slot_capacity);
    REQUIRE(limits.value.logical_block_size() == 512);
    (void)w.closeSession(sid);
}

TEST_CASE("InProcessTestBackend: round-trip write + read", "[privileged_io]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 1ull * 1024 * 1024;   // 1 MB device
    opts.slot_count = 4;
    opts.slot_capacity = 64ull * 1024;        // 64 KB slots
    InProcessTestBackend w(opts);

    auto sid = openTestSession(w);

    // Write 16 distinct slots (1 MB total, fills the synthetic device exactly).
    constexpr std::uint32_t kNumWrites = 16;
    std::vector<SubmitFuture> futures(kNumWrites);
    std::vector<std::vector<std::uint8_t>> expected(kNumWrites);

    for (std::uint32_t i = 0; i < kNumWrites; ++i) {
        auto slot_r = w.acquireSlot(sid);
        REQUIRE(slot_r.ok);
        Slot slot = slot_r.value;

        // Fill the slot with a recognisable pattern keyed off the slot's
        // device offset so we can verify positionally on read-back.
        const std::uint64_t offset = static_cast<std::uint64_t>(i) * opts.slot_capacity;
        for (std::size_t j = 0; j < opts.slot_capacity; ++j) {
            slot.data[j] = static_cast<std::uint8_t>((offset + j) & 0xff);
        }
        expected[i].assign(slot.data, slot.data + opts.slot_capacity);

        w.submitWrite(sid, slot.index, offset, opts.slot_capacity,
                      [&futures, i](const proto::WriteResult& r) {
                          futures[i].set(r);
                      });
    }

    for (auto& f : futures) f.wait();

    // All writes succeeded?
    for (std::uint32_t i = 0; i < kNumWrites; ++i) {
        INFO("write " << i);
        REQUIRE(futures[i].result.error().code() == proto::ERROR_NONE);
        REQUIRE(futures[i].result.bytes_written() == opts.slot_capacity);
    }

    // Read back via readChunk and assert content matches what we wrote.
    std::vector<std::byte> readback(opts.slot_capacity);
    for (std::uint32_t i = 0; i < kNumWrites; ++i) {
        const std::uint64_t offset = static_cast<std::uint64_t>(i) * opts.slot_capacity;
        auto rr = w.readChunk(sid, offset, readback.data(), readback.size());
        REQUIRE(rr.ok);
        REQUIRE(rr.value == opts.slot_capacity);
        REQUIRE(std::memcmp(readback.data(), expected[i].data(),
                             opts.slot_capacity) == 0);
    }

    auto stats = w.closeSession(sid);
    REQUIRE(stats.ok);
    REQUIRE(stats.value.state() == proto::SESSION_STATE_SUCCEEDED);
    REQUIRE(stats.value.bytes_written() == opts.device_bytes);
}

TEST_CASE("InProcessTestBackend: prepareDevice zeros first/last MB",
          "[privileged_io]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 4ull * 1024 * 1024;   // 4 MB so first/last MB don't overlap
    InProcessTestBackend w(opts);

    auto sid = openTestSession(w);

    proto::PrepareOptions popts;
    popts.set_zero_first_mb(true);
    popts.set_zero_last_mb(true);
    auto pr = w.prepareDevice(sid, popts);
    REQUIRE(pr.ok);

    constexpr std::size_t kMb = 1ull * 1024 * 1024;
    std::vector<std::byte> buf(kMb);

    // First MB is zero
    auto rr1 = w.readChunk(sid, 0, buf.data(), buf.size());
    REQUIRE(rr1.ok);
    for (auto b : buf) REQUIRE(static_cast<unsigned char>(b) == 0);

    // Last MB is zero
    const std::uint64_t last_off = opts.device_bytes - kMb;
    auto rr2 = w.readChunk(sid, last_off, buf.data(), buf.size());
    REQUIRE(rr2.ok);
    for (auto b : buf) REQUIRE(static_cast<unsigned char>(b) == 0);

    (void)w.closeSession(sid);
}

TEST_CASE("InProcessTestBackend: factory falls back to test backend "
          "when no helper preferred and no local constructor supplied",
          "[privileged_io][factory]") {
    PrivilegedWriterFactory::Config config;
    config.prefer_helper = false;   // skip the platform helper backend
    // local_backend_constructor left null - factory should fall back
    // to the in-process test backend.
    auto w = PrivilegedWriterFactory::create(std::move(config));
    REQUIRE(w != nullptr);
    REQUIRE(w->backend() == BackendKind::InProcessTest);
}

TEST_CASE("InProcessTestBackend: factory honours testing_override",
          "[privileged_io][factory]") {
    PrivilegedWriterFactory::Config config;
    InProcessTestBackend::Options opts;
    opts.device_bytes = 128ull * 1024;
    config.testing_override = std::make_unique<InProcessTestBackend>(opts);
    auto w = PrivilegedWriterFactory::create(std::move(config));
    REQUIRE(w != nullptr);

    // The override carried opts.device_bytes = 128 KB; verify by querying
    // device limits inside an opened session.
    auto sid = openTestSession(*w);
    auto limits = w->queryDeviceLimits(sid);
    REQUIRE(limits.ok);
    REQUIRE(limits.value.total_bytes() == 128ull * 1024);
    (void)w->closeSession(sid);
}
