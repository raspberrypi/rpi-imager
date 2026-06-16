// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// In-process test backend for IPrivilegedWriter.
//
// Backs a session with a regular tempfile rather than a real block device,
// performs writes via pwrite(2) on a worker thread, and accumulates the
// SessionStats payload that real backends would. No privilege escalation,
// no IPC, no shared memory — but a faithful enough implementation that
// the upper-half pipeline (decompression, hashing, ring back-pressure,
// verify) can be exercised end-to-end in unit tests in milliseconds.
//
// FaultInjectingBackend wraps this to add controlled error injection.

#pragma once

#include "../privileged_writer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rpi_imager::privileged::backends {

class InProcessTestBackend final : public IPrivilegedWriter {
public:
    struct Options {
        // Path to a regular file to be used as the synthetic device. If
        // empty, the backend creates one in the platform's temp directory
        // and removes it on destruction.
        std::string device_file_path;

        // Size of the synthetic device. Used for DeviceLimits and to
        // truncate the underlying file on session open.
        std::uint64_t device_bytes = 64ull * 1024 * 1024;   // 64 MB default

        // Number of slots in the bulk-write ring.
        std::uint32_t slot_count = 8;

        // Slot capacity, matches typical write buffer sizes (8 MB default
        // mirrors production), but tests usually shrink this for speed.
        std::size_t slot_capacity = 1ull * 1024 * 1024;     // 1 MB default

        // If non-zero, sleep this long inside each pwrite() to simulate
        // backend latency. Useful for tests that want to verify pacing
        // behaviour without involving a real disk.
        std::chrono::microseconds simulated_write_latency{0};
    };

    InProcessTestBackend();
    explicit InProcessTestBackend(Options opts);
    ~InProcessTestBackend() override;

    InProcessTestBackend(const InProcessTestBackend&) = delete;
    InProcessTestBackend& operator=(const InProcessTestBackend&) = delete;

    // ---- Helper lifecycle (no-op for an in-process backend)
    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::InProcessTest; }
    std::string  backendDescription() const override;

    // ---- Drive enumeration
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

    // ---- Maintenance (no-op for tempfiles)
    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;

private:
    struct SlotEntry {
        std::vector<std::uint8_t> buffer;
        bool in_use = false;
    };

    struct PendingWrite {
        std::uint32_t        slot_index;
        std::uint64_t        offset;
        std::uint64_t        length;
        WriteCompleteCallback callback;
    };

    struct Session {
        std::uint64_t id = 0;
        std::string   device_path;
        int           fd = -1;
        bool          owns_tempfile = false;
        proto_ns::OpenOptions options;

        std::vector<SlotEntry> slots;
        std::mutex             slot_mutex;
        std::condition_variable slot_cv;

        // Worker thread + queue
        std::thread             worker;
        std::mutex              queue_mutex;
        std::condition_variable queue_cv;
        std::deque<PendingWrite> queue;
        std::atomic<bool>       stop{false};

        // Stats accumulators. Only the basics are populated here; this
        // test backend doesn't fill in the full SessionStats histograms.
        std::chrono::steady_clock::time_point started_at;
        std::atomic<std::uint64_t> bytes_written{0};
        std::atomic<std::uint32_t> writes_completed{0};
        std::atomic<std::uint32_t> writes_failed{0};
    };

    Session* sessionOrNull(const proto_ns::SessionId&);
    void workerLoop(Session* s);
    void releaseSlot(Session* s, std::uint32_t idx);
    static proto_ns::ErrorInfo makeError(proto_ns::ErrorCode code,
                                          const std::string& detail,
                                          int kernel_errno = 0);

    Options                                          options_;
    std::mutex                                       sessions_mutex_;
    std::unordered_map<std::uint64_t,
                        std::unique_ptr<Session>>     sessions_;
    std::atomic<std::uint64_t>                       next_session_id_{1};
};

} // namespace rpi_imager::privileged::backends
