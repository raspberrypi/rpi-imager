// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// FaultInjectingBackend tests.
//
// These exist primarily to demonstrate that the imager's upper-half
// pipeline correctly surfaces a synthetic write failure - the same
// failure shape (mid-stream pwrite with EIO) that triggered the Tahoe
// authopen-FD investigation. The point isn't merely to test the backend
// wrapper; it's to ensure that in CI we can deterministically reproduce
// that failure mode without needing real hardware.

#include <catch2/catch_test_macros.hpp>

#include "privileged_io/privileged_writer.h"
#include "privileged_io/backends/in_process_test.h"
#include "privileged_io/backends/fault_injecting.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

using namespace rpi_imager::privileged;
using rpi_imager::privileged::backends::InProcessTestBackend;
using rpi_imager::privileged::backends::FaultInjectingBackend;
namespace proto = rpi_imager::privileged::proto;

namespace {

struct CompletionGather {
    std::mutex                       m;
    std::condition_variable          cv;
    std::vector<proto::WriteResult>  results;
    std::uint32_t                    expected = 0;

    void onComplete(const proto::WriteResult& r) {
        std::lock_guard<std::mutex> lk(m);
        results.push_back(r);
        if (results.size() == expected) cv.notify_all();
    }

    void waitFor(std::uint32_t n) {
        std::unique_lock<std::mutex> lk(m);
        expected = n;
        cv.wait(lk, [&] { return results.size() == n; });
    }
};

}  // namespace

TEST_CASE("FaultInjectingBackend: passthrough when no fault configured",
          "[privileged_io][fault]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 256ull * 1024;
    opts.slot_capacity = 32ull * 1024;
    opts.slot_count = 4;

    FaultInjectingBackend::Plan plan;   // all defaults: don't inject anything
    auto inner = std::make_unique<InProcessTestBackend>(opts);
    FaultInjectingBackend w(std::move(inner), plan);

    REQUIRE(w.backend() == BackendKind::FaultInjecting);

    proto::OpenOptions oopts;
    auto sid = w.openSession("", oopts);
    REQUIRE(sid.ok);

    constexpr std::uint32_t kWrites = 8;
    CompletionGather gather;

    for (std::uint32_t i = 0; i < kWrites; ++i) {
        auto slot_r = w.acquireSlot(sid.value);
        REQUIRE(slot_r.ok);
        // Fill with anything; we just want successful completions.
        std::memset(slot_r.value.data, static_cast<int>(i & 0xff),
                    opts.slot_capacity);
        w.submitWrite(sid.value, slot_r.value.index,
                      static_cast<std::uint64_t>(i) * opts.slot_capacity,
                      opts.slot_capacity,
                      [&gather](const proto::WriteResult& r) {
                          gather.onComplete(r);
                      });
    }
    gather.waitFor(kWrites);

    for (const auto& r : gather.results) {
        REQUIRE(r.error().code() == proto::ERROR_NONE);
    }
    (void)w.closeSession(sid.value);
}

TEST_CASE("FaultInjectingBackend: injected EIO at Nth write surfaces correctly",
          "[privileged_io][fault]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 256ull * 1024;
    opts.slot_capacity = 32ull * 1024;
    opts.slot_count = 4;

    FaultInjectingBackend::Plan plan;
    plan.fail_write_at = 3;                          // 3rd submitWrite fails
    plan.fail_write_code = proto::ERROR_DEVICE_IO;
    plan.fail_write_kernel_errno = 5;                // EIO
    plan.fail_write_detail = "synthetic EIO";

    auto inner = std::make_unique<InProcessTestBackend>(opts);
    FaultInjectingBackend w(std::move(inner), plan);

    proto::OpenOptions oopts;
    auto sid = w.openSession("", oopts);
    REQUIRE(sid.ok);

    constexpr std::uint32_t kWrites = 6;
    CompletionGather gather;

    for (std::uint32_t i = 0; i < kWrites; ++i) {
        auto slot_r = w.acquireSlot(sid.value);
        REQUIRE(slot_r.ok);
        std::memset(slot_r.value.data, static_cast<int>(i & 0xff),
                    opts.slot_capacity);
        w.submitWrite(sid.value, slot_r.value.index,
                      static_cast<std::uint64_t>(i) * opts.slot_capacity,
                      opts.slot_capacity,
                      [&gather](const proto::WriteResult& r) {
                          gather.onComplete(r);
                      });
    }
    gather.waitFor(kWrites);

    // 5 successes, exactly 1 EIO failure.
    std::uint32_t fail_count = 0;
    proto::WriteResult failed_one;
    for (const auto& r : gather.results) {
        if (r.error().code() != proto::ERROR_NONE) {
            ++fail_count;
            failed_one = r;
        }
    }
    REQUIRE(fail_count == 1);
    REQUIRE(failed_one.error().code() == proto::ERROR_DEVICE_IO);
    REQUIRE(failed_one.error().kernel_errno() == 5);
    REQUIRE(failed_one.error().detail() == "synthetic EIO");

    (void)w.closeSession(sid.value);
}

TEST_CASE("FaultInjectingBackend: failures recur every N after the first",
          "[privileged_io][fault]") {
    InProcessTestBackend::Options opts;
    opts.device_bytes = 1ull * 1024 * 1024;
    opts.slot_capacity = 16ull * 1024;
    opts.slot_count = 4;

    FaultInjectingBackend::Plan plan;
    plan.fail_write_at = 2;             // first failure at write #2
    plan.fail_every_n_after = 3;        // then writes 5, 8, 11, 14...
    plan.fail_write_code = proto::ERROR_DEVICE_IO;
    plan.fail_write_kernel_errno = 5;

    auto inner = std::make_unique<InProcessTestBackend>(opts);
    FaultInjectingBackend w(std::move(inner), plan);

    proto::OpenOptions oopts;
    auto sid = w.openSession("", oopts);
    REQUIRE(sid.ok);

    constexpr std::uint32_t kWrites = 14;
    CompletionGather gather;

    for (std::uint32_t i = 0; i < kWrites; ++i) {
        auto slot_r = w.acquireSlot(sid.value);
        REQUIRE(slot_r.ok);
        std::memset(slot_r.value.data, static_cast<int>(i & 0xff),
                    opts.slot_capacity);
        w.submitWrite(sid.value, slot_r.value.index,
                      static_cast<std::uint64_t>(i) * opts.slot_capacity,
                      opts.slot_capacity,
                      [&gather](const proto::WriteResult& r) {
                          gather.onComplete(r);
                      });
    }
    gather.waitFor(kWrites);

    // Submit numbering is 1-based: writes #2, #5, #8, #11, #14 should fail.
    std::uint32_t fail_count = 0;
    for (const auto& r : gather.results) {
        if (r.error().code() != proto::ERROR_NONE) ++fail_count;
    }
    REQUIRE(fail_count == 5);

    (void)w.closeSession(sid.value);
}

TEST_CASE("FaultInjectingBackend: openSession can be made to fail",
          "[privileged_io][fault]") {
    FaultInjectingBackend::Plan plan;
    plan.fail_open_with = proto::ERROR_DEVICE_PERMISSION;

    auto inner = std::make_unique<InProcessTestBackend>();
    FaultInjectingBackend w(std::move(inner), plan);

    proto::OpenOptions oopts;
    auto sid = w.openSession("/dev/null", oopts);
    REQUIRE(!sid.ok);
    REQUIRE(sid.error.code() == proto::ERROR_DEVICE_PERMISSION);
}
