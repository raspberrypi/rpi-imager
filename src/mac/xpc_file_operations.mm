// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "xpc_file_operations.h"

#include "../privileged_io/privileged_writer.h"
#include "../privileged_io/backends/macos_xpc.h"
#include "../privileged_io_glue.h"

#import <ServiceManagement/ServiceManagement.h>
#import <AppKit/AppKit.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <vector>

namespace rpi_imager {

namespace priv = rpi_imager::privileged;

namespace {

// Get the underlying MacOSXpcBackend so we can use its bulk-write
// API. The PAL interface deliberately doesn't expose mapBulkBuffer /
// submitBulkWriteAsync (those are backend-specific to macOS for now),
// so XpcFileOperations downcasts here.
priv::backends::MacOSXpcBackend* xpcBackend() {
    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    return dynamic_cast<priv::backends::MacOSXpcBackend*>(&w);
}

}  // namespace

struct XpcFileOperations::State {
    bool open = false;
    priv::proto::SessionId session_id;   // set when open == true
    std::uint64_t total_size = 0;
    std::uint64_t write_offset = 0;
    std::uint64_t read_offset = 0;
    mutable std::mutex mutex;

    // ---- Async write path (shared-memory ring via the helper) ----
    //
    // SetAsyncQueueDepth(N) maps an N×kSlotBytes shared-memory region
    // via the helper's mapBulkBuffer. AsyncWriteSequential copies the
    // caller's buffer into a slot and submits via the helper's
    // submitBulkWriteAsync. The completion callback frees the slot.
    //
    // Slot size = max chunk per XPC round-trip. Each round-trip has a
    // fixed per-call cost (~1 ms on macOS NSXPC), so larger slots mean
    // fewer trips per GB. The effective size (slot_bytes) is set at
    // SetAsyncQueueDepth time from the caller's SetMaxWriteSizeHint, so a
    // depth-N ring reserves N × the actual write size rather than N × the
    // 32 MB worst-case cap. The imager's write buffers are ≤8 MB, so this
    // is a ~4× reduction in mapped shared memory. kMaxSlotBytes caps the
    // hint; kMinSlotBytes floors it so a tiny hint still amortises IPC.
    static constexpr std::size_t kMaxSlotBytes = 32ull * 1024 * 1024;
    static constexpr std::size_t kMinSlotBytes =  1ull * 1024 * 1024;
    std::size_t max_write_hint = 0;          // set by SetMaxWriteSizeHint
    std::size_t slot_bytes = kMaxSlotBytes;  // finalised at SetAsyncQueueDepth
    int  queue_depth = 1;
    bool async_ready = false;
    void* ring_base = nullptr;
    std::size_t ring_size = 0;
    std::vector<bool> slot_in_use;
    mutable std::mutex slot_mutex;
    std::condition_variable slot_cv;
    std::atomic<int>       pending_writes{0};
    mutable std::condition_variable pending_cv;
    mutable std::mutex pending_mutex;
    std::atomic<bool>      cancelled{false};
    std::atomic<bool>      had_error{false};
    std::string            first_error_detail;
    std::int32_t           first_error_kerrno = 0;
    std::mutex             first_error_mutex;

    // Stats
    std::chrono::steady_clock::time_point async_first_submit;
    std::chrono::steady_clock::time_point async_last_complete;
    std::atomic<std::uint32_t> async_writes_completed{0};
    std::atomic<std::uint64_t> async_total_latency_us{0};
    std::atomic<std::uint32_t> async_min_latency_us{UINT32_MAX};
    std::atomic<std::uint32_t> async_max_latency_us{0};

    // §7a helper-side SessionStats captured in Close(); read via
    // GetLastSessionStats(). Reset to default-constructed on OpenDevice.
    FileOperations::HelperSessionStats last_session_stats;
};

XpcFileOperations::XpcFileOperations()
    : state_(std::make_unique<State>()) {}

XpcFileOperations::~XpcFileOperations() {
    if (state_->open) {
        (void)::rpi_imager::getProcessPrivilegedWriter().closeSession(state_->session_id);
        state_->open = false;
    }
}

FileError XpcFileOperations::OpenDevice(const std::string& path) {
    auto& w = ::rpi_imager::getProcessPrivilegedWriter();

    // Pre-flight: query SMAppService state precisely so we can react
    // before attempting an open against a non-existent service.
    auto status = w.queryHelperStatus();
    if (!status.ok) {
        FileOperationsLog("XpcFileOperations: helper status query failed: "
                          + status.error.detail());
        return FileError::kOpenError;
    }

    using HS = priv::proto::HelperState;
    if (status.value.state() == HS::HELPER_STATE_NOT_INSTALLED) {
        // First-time install: trigger SMAppService.register() so macOS
        // surfaces its "Allow in Background" prompt. The call returns
        // quickly; the user interacts with the system prompt out-of-band.
        // After it returns we re-query state to learn the outcome.
        FileOperationsLog("XpcFileOperations: helper not registered; "
                          "calling installHelper() to trigger SMAppService prompt");
        auto install = w.installHelper();
        if (!install.ok) {
            FileOperationsLog("XpcFileOperations: installHelper failed: "
                              + install.error.detail());
            return FileError::kOpenError;
        }
        status = w.queryHelperStatus();
        if (!status.ok) {
            FileOperationsLog("XpcFileOperations: post-install status query failed: "
                              + status.error.detail());
            return FileError::kOpenError;
        }
    }

    if (status.value.state() == HS::HELPER_STATE_INSTALLED_DISABLED) {
        // SMAppService reports the helper is registered but the user
        // hasn't approved it (or has disabled it from Settings). No
        // amount of retrying from here will help - the user needs to
        // flip the toggle in System Settings > Login Items. The QML
        // layer surfaces this via the privilegedHelperState property
        // and offers a re-enable affordance.
        FileOperationsLog(
            "XpcFileOperations: helper requires user approval in System Settings");
        return FileError::kOpenError;
    }

    priv::proto::OpenOptions opts;
    opts.set_direct_io(true);
    auto open_r = w.openSession(path, opts);
    if (!open_r.ok) {
        FileOperationsLog("XpcFileOperations: openSession failed: "
                          + open_r.error.detail());
        return FileError::kOpenError;
    }

    auto limits = w.queryDeviceLimits(open_r.value);
    if (limits.ok) {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->total_size = limits.value.total_bytes();
        device_io_limits_.max_transfer_bytes = limits.value.max_transfer_bytes();
        device_io_limits_.suggested_queue_depth = 0;  // helper handles it
    } else {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->total_size = 0;
    }

    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->session_id = open_r.value;
        state_->write_offset = 0;
        state_->read_offset = 0;
        state_->open = true;
        // Each new session gets a fresh stats slot; the previous
        // session's stats are no longer addressable from the helper
        // side either (it tore down the session on closeSession).
        state_->last_session_stats = FileOperations::HelperSessionStats{};
    }

    return FileError::kSuccess;
}

FileError XpcFileOperations::CreateTestFile(const std::string& /*path*/,
                                              std::uint64_t /*size*/) {
    // The helper deliberately refuses non-/dev/disk* paths; creating
    // a regular test file goes through the unprivileged side. Tests
    // that need this should use InProcessTestBackend or the legacy
    // MacOSFileOperations.
    return FileError::kOpenError;
}

FileError XpcFileOperations::WriteAtOffset(std::uint64_t offset,
                                              const std::uint8_t* data,
                                              std::size_t size) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;

    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto r = w.writeChunk(state_->session_id, offset,
                            reinterpret_cast<const std::byte*>(data), size);
    if (!r.ok) return FileError::kWriteError;
    if (r.value != size) return FileError::kWriteError;
    return FileError::kSuccess;
}

FileError XpcFileOperations::GetSize(std::uint64_t& size) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    size = state_->total_size;
    return state_->total_size > 0 ? FileError::kSuccess : FileError::kSizeError;
}

FileError XpcFileOperations::Close() {
    // Drain any in-flight async writes BEFORE we tell the helper to
    // close the session, otherwise the helper's bulk-write dispatch
    // queue can still be invoking pwrite against an fd that's about to
    // disappear. WaitForPendingWrites is a no-op when there are no
    // pending writes (sync mode).
    (void)WaitForPendingWrites();

    bool was_open;
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        was_open = state_->open;
        sid = state_->session_id;
        state_->open = false;
    }
    if (!was_open) return FileError::kSuccess;

    // Release the bulk ring (if any) before closing the session - the
    // helper's session teardown unmaps shm too, but releasing here
    // gets the client-side mmap dropped cleanly via mapBulkBuffer(0).
    if (state_->ring_base) {
        if (auto* xpc = xpcBackend()) {
            (void)xpc->mapBulkBuffer(sid, 0);
        }
        state_->ring_base = nullptr;
        state_->ring_size = 0;
        state_->slot_in_use.clear();
    }

    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto stats = w.closeSession(sid);
    if (!stats.ok) {
        FileOperationsLog("XpcFileOperations: closeSession failed: "
                          + stats.error.detail());
        return FileError::kCloseError;
    }

    // §7a cache the helper-reported per-session telemetry so callers
    // (e.g. DownloadThread) can pull it via GetLastSessionStats()
    // after a successful close and feed it into PerformanceStats.
    {
        const auto& s = stats.value;
        FileOperations::HelperSessionStats hss;
        hss.valid = true;
        hss.bytes_written = s.bytes_written();
        hss.duration_ms = s.duration_ms();
        if (s.has_write_latency()) {
            const auto& h = s.write_latency();
            hss.write_count = h.sample_count();
            hss.min_write_latency_us = (std::uint32_t)h.min_us();
            hss.avg_write_latency_us = (std::uint32_t)h.avg_us();
            hss.max_write_latency_us = (std::uint32_t)h.max_us();
        }
        // The backend already translated discrete events; pull
        // helper.fsync / helper.prepareDevice / helper.hashDevice out
        // so the client struct has them addressable.
        for (const auto& ev : s.events()) {
            if (ev.type() == "helper.fsync") {
                hss.total_fsync_us = ev.duration_ms() * 1000ull;
            } else if (ev.type() == "helper.prepareDevice") {
                hss.prepare_device_us = ev.duration_ms() * 1000ull;
            } else if (ev.type() == "helper.hashDevice") {
                hss.hash_device_us = ev.duration_ms() * 1000ull;
            }
        }
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->last_session_stats = hss;
    }
    return FileError::kSuccess;
}

FileOperations::HelperSessionStats
XpcFileOperations::GetLastSessionStats() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->last_session_stats;
}

bool XpcFileOperations::IsOpen() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->open;
}

FileError XpcFileOperations::WriteSequential(const std::uint8_t* data,
                                                std::size_t size) {
    // The helper's writeChunk is bounded at 4 MB per call (set by the
    // protocol). Split larger writes into chunks.
    constexpr std::size_t kMaxChunk = 4ull * 1024 * 1024;
    std::size_t written_total = 0;
    while (written_total < size) {
        const std::size_t chunk = std::min(kMaxChunk, size - written_total);

        std::uint64_t offset;
        priv::proto::SessionId sid;
        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            if (!state_->open) return FileError::kOpenError;
            offset = state_->write_offset;
            sid = state_->session_id;
        }

        auto& w = ::rpi_imager::getProcessPrivilegedWriter();
        auto r = w.writeChunk(sid, offset,
                                reinterpret_cast<const std::byte*>(data + written_total),
                                chunk);
        if (!r.ok) return FileError::kWriteError;
        if (r.value == 0) return FileError::kWriteError;

        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            state_->write_offset += r.value;
        }
        written_total += r.value;
    }
    return FileError::kSuccess;
}

FileError XpcFileOperations::ReadSequential(std::uint8_t* data,
                                               std::size_t size,
                                               std::size_t& bytes_read) {
    bytes_read = 0;
    constexpr std::size_t kMaxChunk = 4ull * 1024 * 1024;
    std::size_t got_total = 0;
    while (got_total < size) {
        const std::size_t chunk = std::min(kMaxChunk, size - got_total);

        std::uint64_t offset;
        priv::proto::SessionId sid;
        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            if (!state_->open) return FileError::kOpenError;
            offset = state_->read_offset;
            sid = state_->session_id;
        }

        auto& w = ::rpi_imager::getProcessPrivilegedWriter();
        auto r = w.readChunk(sid, offset,
                                reinterpret_cast<std::byte*>(data + got_total),
                                chunk);
        if (!r.ok) return FileError::kReadError;

        {
            std::lock_guard<std::mutex> lk(state_->mutex);
            state_->read_offset += r.value;
        }
        got_total += r.value;
        bytes_read += r.value;

        // Short read at end of device - that's OK, just return.
        if (r.value < chunk) break;
    }
    return FileError::kSuccess;
}

void XpcFileOperations::SetMaxWriteSizeHint(std::size_t bytes) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->max_write_hint = bytes;
}

bool XpcFileOperations::SetAsyncQueueDepth(int depth) {
    if (depth < 1) depth = 1;

    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return false;

    // Size each ring slot to the caller's largest expected write (rounded
    // up to a page), clamped to [kMinSlotBytes, kMaxSlotBytes]. Without a
    // hint we fall back to the worst-case cap so behaviour is unchanged.
    {
        std::size_t slot = State::kMaxSlotBytes;
        if (state_->max_write_hint > 0) {
            constexpr std::size_t kPage = 4096;
            slot = ((state_->max_write_hint + kPage - 1) / kPage) * kPage;
            slot = std::max(slot, State::kMinSlotBytes);
            slot = std::min(slot, State::kMaxSlotBytes);
        }
        state_->slot_bytes = slot;
    }

    // depth == 1 means synchronous mode; nothing to set up.
    if (depth == 1) {
        if (state_->ring_base) {
            // Tear down any prior ring.
            auto* xpc = xpcBackend();
            if (xpc) (void)xpc->mapBulkBuffer(state_->session_id, 0);
            state_->ring_base = nullptr;
            state_->ring_size = 0;
            state_->slot_in_use.clear();
        }
        state_->queue_depth = 1;
        state_->async_ready = false;
        return false;
    }

    auto* xpc = xpcBackend();
    if (!xpc) {
        // Not the XPC backend - shouldn't happen on macOS, but be safe.
        return false;
    }

    const std::size_t ring_bytes =
        static_cast<std::size_t>(depth) * state_->slot_bytes;
    auto map_r = xpc->mapBulkBuffer(state_->session_id, ring_bytes);
    if (!map_r.ok) {
        FileOperationsLog("XpcFileOperations: mapBulkBuffer failed: "
                          + map_r.error.detail());
        return false;
    }

    state_->ring_base = xpc->bulkBufferBase();
    state_->ring_size = xpc->bulkBufferSize();
    state_->slot_in_use.assign(static_cast<std::size_t>(depth), false);
    state_->queue_depth = depth;
    state_->async_ready = true;
    state_->cancelled.store(false);
    state_->had_error.store(false);
    state_->pending_writes.store(0);
    return true;
}

int XpcFileOperations::GetAsyncQueueDepth() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->queue_depth;
}

bool XpcFileOperations::IsAsyncIOSupported() const {
    // Capability check: the helper-routed path always supports async via
    // the shared-memory bulk ring. The base-class contract is "is this
    // platform capable", not "has it been configured" - callers check
    // capability first, then call SetAsyncQueueDepth to enable it.
    return true;
}

FileError XpcFileOperations::AsyncWriteSequential(const std::uint8_t* data,
                                                    std::size_t size,
                                                    AsyncWriteCallback callback) {
    // Fast paths: not configured for async, oversized chunk, or
    // cancelled -> fall through to synchronous WriteSequential.
    bool use_sync = false;
    std::uint64_t offset_for_sync = 0;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) {
            if (callback) callback(FileError::kOpenError, 0);
            return FileError::kOpenError;
        }
        if (!state_->async_ready || size > state_->slot_bytes) {
            use_sync = true;
        }
        if (state_->cancelled.load()) {
            if (callback) callback(FileError::kCancelled, 0);
            return FileError::kCancelled;
        }
        offset_for_sync = state_->write_offset;
    }
    if (use_sync) {
        // Run the synchronous path with the existing write_offset
        // tracking handled inside WriteSequential. Then invoke the
        // callback with the result.
        FileError r = WriteSequential(data, size);
        if (callback) callback(r, r == FileError::kSuccess ? size : 0);
        return r;
    }

    // Acquire a free slot (blocks if all are in use).
    std::size_t slot_index = 0;
    {
        std::unique_lock<std::mutex> slot_lk(state_->slot_mutex);
        state_->slot_cv.wait(slot_lk, [this] {
            if (state_->cancelled.load()) return true;
            for (bool b : state_->slot_in_use) if (!b) return true;
            return false;
        });
        if (state_->cancelled.load()) {
            if (callback) callback(FileError::kCancelled, 0);
            return FileError::kCancelled;
        }
        for (std::size_t i = 0; i < state_->slot_in_use.size(); ++i) {
            if (!state_->slot_in_use[i]) { slot_index = i; state_->slot_in_use[i] = true; break; }
        }
    }

    // Copy caller's buffer into the slot, snapshot offset, advance
    // the write cursor under the main mutex.
    const std::uint64_t buffer_offset =
        static_cast<std::uint64_t>(slot_index) * state_->slot_bytes;
    std::uint8_t* slot_base =
        static_cast<std::uint8_t*>(state_->ring_base) + buffer_offset;
    std::memcpy(slot_base, data, size);

    std::uint64_t device_offset;
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        device_offset = state_->write_offset;
        state_->write_offset += size;
        sid = state_->session_id;
    }

    state_->pending_writes.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(state_->first_error_mutex);
        if (state_->async_writes_completed.load() == 0 &&
            state_->async_first_submit.time_since_epoch().count() == 0) {
            state_->async_first_submit = std::chrono::steady_clock::now();
        }
    }
    const auto submit_time = std::chrono::steady_clock::now();

    auto* xpc = xpcBackend();
    if (!xpc) {
        // Should never happen given async_ready check above.
        state_->pending_writes.fetch_sub(1);
        if (callback) callback(FileError::kWriteError, 0);
        return FileError::kWriteError;
    }

    // Capture what we need into the completion closure (no `this`
    // capture lifetime concerns because XpcFileOperations outlives
    // its in-flight writes - WaitForPendingWrites is called before
    // close).
    State* st = state_.get();
    std::size_t slot_for_release = slot_index;
    AsyncWriteCallback user_cb = std::move(callback);

    xpc->submitBulkWriteAsync(
        sid, buffer_offset, device_offset, size,
        [st, slot_for_release, size, user_cb = std::move(user_cb), submit_time]
        (priv::Result<std::size_t> result) mutable {
            // Latency tracking
            auto now = std::chrono::steady_clock::now();
            auto latency_us = static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now - submit_time).count());
            std::uint32_t cur_min = st->async_min_latency_us.load();
            while (latency_us < cur_min &&
                   !st->async_min_latency_us.compare_exchange_weak(cur_min, latency_us)) {}
            std::uint32_t cur_max = st->async_max_latency_us.load();
            while (latency_us > cur_max &&
                   !st->async_max_latency_us.compare_exchange_weak(cur_max, latency_us)) {}
            st->async_total_latency_us.fetch_add(latency_us);
            st->async_writes_completed.fetch_add(1);
            st->async_last_complete = now;

            // Free the slot.
            {
                std::lock_guard<std::mutex> lk(st->slot_mutex);
                st->slot_in_use[slot_for_release] = false;
            }
            st->slot_cv.notify_one();

            FileError fe = FileError::kSuccess;
            std::size_t bytes = 0;
            if (!result.ok) {
                fe = FileError::kWriteError;
                if (!st->had_error.exchange(true)) {
                    std::lock_guard<std::mutex> lk(st->first_error_mutex);
                    st->first_error_detail = result.error.detail();
                    st->first_error_kerrno = result.error.kernel_errno();
                }
            } else if (result.value != size) {
                fe = FileError::kWriteError;
                bytes = result.value;
                st->had_error.store(true);
            } else {
                bytes = result.value;
            }

            if (user_cb) user_cb(fe, bytes);

            // Decrement AFTER invoking the user callback so that
            // WaitForPendingWrites doesn't fire before the user gets
            // notified.
            if (st->pending_writes.fetch_sub(1) == 1) {
                std::lock_guard<std::mutex> lk(st->pending_mutex);
                st->pending_cv.notify_all();
            }
        });
    return FileError::kSuccess;
}

int XpcFileOperations::GetPendingWriteCount() const {
    return state_->pending_writes.load();
}

FileError XpcFileOperations::WaitForPendingWrites() {
    std::unique_lock<std::mutex> lk(state_->pending_mutex);
    state_->pending_cv.wait(lk, [this] {
        return state_->pending_writes.load() == 0 ||
               state_->cancelled.load();
    });
    if (state_->had_error.load()) {
        return FileError::kWriteError;
    }
    if (state_->cancelled.load() && state_->pending_writes.load() > 0) {
        return FileError::kCancelled;
    }
    return FileError::kSuccess;
}

void XpcFileOperations::CancelAsyncIO() {
    state_->cancelled.store(true);
    state_->slot_cv.notify_all();
    {
        std::lock_guard<std::mutex> lk(state_->pending_mutex);
        state_->pending_cv.notify_all();
    }
}

void XpcFileOperations::GetAsyncIOStats(uint32_t& wallClockMs,
                                          uint32_t& writeCount,
                                          uint32_t& minLatencyUs,
                                          uint32_t& maxLatencyUs,
                                          uint32_t& avgLatencyUs) const {
    writeCount = state_->async_writes_completed.load();
    if (writeCount == 0) {
        wallClockMs = minLatencyUs = maxLatencyUs = avgLatencyUs = 0;
        return;
    }
    auto first = state_->async_first_submit;
    auto last = state_->async_last_complete;
    wallClockMs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(last - first).count());
    std::uint32_t mn = state_->async_min_latency_us.load();
    minLatencyUs = (mn == UINT32_MAX) ? 0 : mn;
    maxLatencyUs = state_->async_max_latency_us.load();
    avgLatencyUs = static_cast<uint32_t>(state_->async_total_latency_us.load() / writeCount);
}

FileError XpcFileOperations::Seek(std::uint64_t position) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    state_->write_offset = position;
    state_->read_offset = position;
    return FileError::kSuccess;
}

std::uint64_t XpcFileOperations::Tell() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->write_offset;
}

FileError XpcFileOperations::ForceSync() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    auto sid = state_->session_id;
    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto r = w.syncDevice(sid);
    if (!r.ok) return FileError::kSyncError;
    return FileError::kSuccess;
}

FileError XpcFileOperations::Flush() {
    // No client-side buffering above the XPC layer, so flush is a
    // no-op (all writes are submitted by the time WriteSequential
    // returns).
    return FileError::kSuccess;
}

void XpcFileOperations::PrepareForSequentialRead(std::uint64_t offset,
                                                    std::uint64_t /*length*/) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->read_offset = offset;
    // F_RDAHEAD / F_NOCACHE hints don't cross the XPC boundary. The
    // helper opens with O_RDWR and the kernel's read-ahead heuristics
    // apply on its side.
}

FileOperations::FastVerifyResult XpcFileOperations::FastVerifySha256(
    const std::uint8_t* prefix,
    std::size_t prefix_len,
    std::uint64_t device_offset,
    std::uint64_t length) {
    FastVerifyResult out;
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) {
            out.supported = true;
            out.error = FileError::kOpenError;
            return out;
        }
        sid = state_->session_id;
    }

    std::string prefix_bytes;
    if (prefix && prefix_len > 0) {
        prefix_bytes.assign(reinterpret_cast<const char*>(prefix), prefix_len);
    }

    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto r = w.hashDeviceSha256(sid, prefix_bytes, device_offset, length);
    out.supported = true;
    if (!r.ok) {
        FileOperationsLog("XpcFileOperations: hashDeviceSha256 failed: "
                          + r.error.detail());
        out.error = FileError::kReadError;
        return out;
    }
    out.error = FileError::kSuccess;
    out.digest = std::move(r.value);
    return out;
}

FileError XpcFileOperations::PrepareDevice(std::uint64_t /*device_size*/,
                                              bool zero_last_mb) {
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        sid = state_->session_id;
    }

    priv::proto::PrepareOptions opts;
    opts.set_zero_first_mb(true);
    opts.set_zero_last_mb(zero_last_mb);

    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto r = w.prepareDevice(sid, opts);
    if (!r.ok) {
        FileOperationsLog("XpcFileOperations: prepareDevice failed: "
                          + r.error.detail());
        return FileError::kWriteError;
    }

    // Reset the local write/read cursors so subsequent WriteSequential
    // starts at offset 0, matching the FileOperations contract.
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->write_offset = 0;
        state_->read_offset = 0;
    }
    return FileError::kSuccess;
}

bool XpcFileOperations::IsDirectIOEnabled() const {
    // The helper opens the device with the direct-I/O hint set on its
    // own FD. From the unprivileged side we don't have a meaningful
    // way to query that without an additional XPC round-trip; report
    // true to match the configured open-time behaviour.
    return true;
}

FileError XpcFileOperations::SetDirectIOEnabled(bool /*enabled*/) {
    // Direct-I/O is fixed at openSession time on the helper side.
    return FileError::kSuccess;
}

FileOperations::DirectIOInfo XpcFileOperations::GetDirectIOInfo() const {
    DirectIOInfo info;
    info.attempted = true;
    info.succeeded = true;
    info.currently_enabled = true;
    info.error_code = 0;
    info.error_message = "(via helper)";
    return info;
}

// Factory entry point referenced from file_operations_macos.cpp's
// CreatePlatformFileOperations() factory.
std::unique_ptr<FileOperations> CreateXpcFileOperations() {
    return std::make_unique<XpcFileOperations>();
}

// Deep-link to the Login Items pane. Used by the QML "Open System
// Settings" affordance when the privileged helper needs user approval.
//
// We tried the documented x-apple.systempreferences URL first but
// Apple's been renaming pane identifiers between macOS releases and
// it doesn't work reliably on Tahoe. SMAppService.openSystemSettingsLoginItems
// is the Apple-supported entry point introduced in macOS 13.0 and is
// the form Apple actually maintains across releases.
void OpenLoginItemsSettings() {
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            [SMAppService openSystemSettingsLoginItems];
            return;
        }
        // macOS 12 fallback: best-effort URL scheme. Won't deep-link
        // to Login Items but at least opens System Preferences.
        NSURL* url = [NSURL URLWithString:
            @"x-apple.systempreferences:com.apple.preference.security?LoginItems"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

} // namespace rpi_imager
