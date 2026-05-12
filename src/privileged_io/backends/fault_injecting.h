// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// FaultInjectingBackend - wraps another IPrivilegedWriter and injects
// configurable failures or latency. Used to:
//
//   - Reproduce the Tahoe authopen-FD intermittent failure deterministically
//     in tests ("fail every Nth pwrite with EIO until further notice").
//   - Stress-test the imager pipeline against device-disconnect mid-stream,
//     short-write, EBUSY, etc.
//   - Validate error propagation in unit tests without needing a real disk.
//
// Configure via the public Plan{} struct before any operations are issued.
// All operations forward to the wrapped backend except where the plan
// dictates a synthetic failure or delay.

#pragma once

#include "../privileged_writer.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>

namespace rpi_imager::privileged::backends {

class FaultInjectingBackend final : public IPrivilegedWriter {
public:
    struct Plan {
        // Fail the Nth submitWrite (1-based) with the given error.
        // 0 means "don't inject" - the default.
        std::uint32_t        fail_write_at = 0;
        proto_ns::ErrorCode  fail_write_code = proto_ns::ERROR_DEVICE_IO;
        int                  fail_write_kernel_errno = 5;          // EIO
        std::string          fail_write_detail = "injected fault: EIO";

        // After fail_write_at fires, also fail every Nth subsequent write
        // (0 = only fail the one configured above).
        std::uint32_t        fail_every_n_after = 0;

        // Add this much wall-clock latency to each pwrite() in the wrapped
        // backend. Useful for testing back-pressure handling.
        std::chrono::microseconds added_write_latency{0};

        // Reject openSession() with the given error. ERROR_NONE = succeed.
        proto_ns::ErrorCode  fail_open_with = proto_ns::ERROR_NONE;
    };

    // Wraps `inner`, taking ownership.
    FaultInjectingBackend(std::unique_ptr<IPrivilegedWriter> inner, Plan plan);
    ~FaultInjectingBackend() override = default;

    FaultInjectingBackend(const FaultInjectingBackend&) = delete;
    FaultInjectingBackend& operator=(const FaultInjectingBackend&) = delete;

    // Update the plan mid-test. Thread-safe with respect to in-flight ops.
    void setPlan(Plan plan);

    // ---- Helper lifecycle (forwarded)
    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::FaultInjecting; }
    std::string  backendDescription() const override;

    // ---- Drive enumeration (forwarded)
    Result<std::vector<proto_ns::DriveInfo>> listDrivesNow() override;
    Result<void> subscribeDrives(DriveChangeCallback cb) override;
    Result<void> unsubscribeDrives() override;

    // ---- Sessions
    Result<proto_ns::SessionId> openSession(const std::string& device_path,
                                              const proto_ns::OpenOptions& opts) override;
    Result<proto_ns::DeviceLimits> queryDeviceLimits(const proto_ns::SessionId&) override;
    Result<void> prepareDevice(const proto_ns::SessionId&,
                                const proto_ns::PrepareOptions&) override;
    Result<Slot> acquireSlot(const proto_ns::SessionId&) override;
    void submitWrite(const proto_ns::SessionId&,
                     std::uint32_t slot_index,
                     std::uint64_t offset,
                     std::uint64_t length,
                     WriteCompleteCallback on_complete) override;
    Result<std::size_t> readChunk(const proto_ns::SessionId&,
                                    std::uint64_t offset,
                                    std::byte* buf,
                                    std::size_t len) override;
    Result<std::size_t> writeChunk(const proto_ns::SessionId&,
                                     std::uint64_t offset,
                                     const std::byte* buf,
                                     std::size_t len) override;
    Result<void> syncDevice(const proto_ns::SessionId&) override;
    Result<proto_ns::SessionStats> closeSession(const proto_ns::SessionId&) override;

    // ---- Maintenance (forwarded)
    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;

private:
    std::unique_ptr<IPrivilegedWriter> inner_;
    mutable std::mutex                 plan_mutex_;
    Plan                                plan_;
    std::atomic<std::uint32_t>         submit_count_{0};
};

} // namespace rpi_imager::privileged::backends
