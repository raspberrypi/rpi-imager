// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
// Windows PAL adapter — routes I/O through WindowsUacBackend (§14.6).

#include "windows_helper_file_operations.h"

#include "../privileged_io/privileged_writer.h"
#include "../privileged_io/backends/windows_uac.h"
#include "../privileged_io/helper_session_stats.h"
#include "../privileged_io_glue.h"
#include "../write_buffer_provider.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rpi_imager {

namespace priv = rpi_imager::privileged;

namespace {

priv::backends::WindowsUacBackend* uacBackend() {
    auto& w = getProcessPrivilegedWriter();
    return dynamic_cast<priv::backends::WindowsUacBackend*>(&w);
}

std::size_t maxZeroCopyRingBytes() {
    constexpr std::uint64_t kFloor = 256ull * 1024 * 1024;
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st) || st.ullTotalPhys == 0) {
        return static_cast<std::size_t>(kFloor);
    }
    const std::uint64_t third = st.ullTotalPhys / 3;
    return static_cast<std::size_t>(third > kFloor ? third : kFloor);
}

}  // namespace

struct WindowsHelperFileOperations::State {
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

    // ---- Zero-copy ring (slots are the producer's RingBuffer memory) ----
    //
    // When a zero-copy WriteBufferProvider maps the helper bulk ring as the
    // producer's write ring buffer, the producer fills these slots directly
    // and AsyncWriteSequential submits the slot's offset with no copy. The
    // mapping reuses ring_base/ring_size (so Close() tears it down via the
    // same path); zerocopy_active flags the in-range submit path and
    // zc_slot_bytes bounds an individual write. Back-pressure is provided by
    // the producer's RingBuffer, so the copy-path slot_in_use pool is unused
    // (and cleared) in this mode.
    bool zerocopy_active = false;
    std::size_t zc_slot_bytes = 0;

    // Observability: whether a zero-copy ring was ever adopted this session
    // (survives Close so post-write stats can report it; reset on OpenDevice),
    // and per-path async submit counts.
    bool zerocopy_engaged = false;
    std::atomic<std::uint64_t> zc_submits{0};
    std::atomic<std::uint64_t> copy_submits{0};

    // Sentinel slot index meaning "no copy-ring slot to free" (zero-copy
    // submit, where the producer's RingBuffer owns slot lifetime).
    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);
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

WindowsHelperFileOperations::WindowsHelperFileOperations()
    : state_(std::make_unique<State>()) {}

WindowsHelperFileOperations::~WindowsHelperFileOperations() {
    if (state_->open) {
        (void)::rpi_imager::getProcessPrivilegedWriter().closeSession(state_->session_id);
        state_->open = false;
    }
}

FileError WindowsHelperFileOperations::OpenDevice(const std::string& path) {
    auto& w = ::rpi_imager::getProcessPrivilegedWriter();

    // Pre-flight: query SMAppService state precisely so we can react
    // before attempting an open against a non-existent service.
    auto status = w.queryHelperStatus();
    if (!status.ok) {
        FileOperationsLog("WindowsHelperFileOperations: helper status query failed: "
                          + status.error.detail());
        return FileError::kOpenError;
    }

    using HS = priv::proto::HelperState;
    if (status.value.state() == HS::HELPER_STATE_VERSION_MISMATCH) {
        FileOperationsLog("WindowsHelperFileOperations: helper protocol version mismatch");
        return FileError::kOpenError;
    }
    if (status.value.state() == HS::HELPER_STATE_NOT_INSTALLED) {
        FileOperationsLog("WindowsHelperFileOperations: helper not reachable; calling installHelper()");
        auto install = w.installHelper();
        if (!install.ok) {
            FileOperationsLog("WindowsHelperFileOperations: installHelper failed: "
                              + install.error.detail());
            return FileError::kOpenError;
        }
        status = w.queryHelperStatus();
        if (!status.ok) {
            FileOperationsLog("WindowsHelperFileOperations: post-install status query failed: "
                              + status.error.detail());
            return FileError::kOpenError;
        }
    }

    priv::proto::OpenOptions opts;
    opts.set_direct_io(true);
    auto open_r = w.openSession(path, opts);
    if (!open_r.ok) {
        FileOperationsLog("WindowsHelperFileOperations: openSession failed: "
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
        state_->zerocopy_engaged = false;
        state_->zc_submits.store(0, std::memory_order_relaxed);
        state_->copy_submits.store(0, std::memory_order_relaxed);
    }

    return FileError::kSuccess;
}

FileError WindowsHelperFileOperations::CreateTestFile(const std::string& /*path*/,
                                              std::uint64_t /*size*/) {
    // The helper deliberately refuses non-/dev/disk* paths; creating
    // a regular test file goes through the unprivileged side. Tests
    // that need this should use InProcessTestBackend or the legacy
    // MacOSFileOperations.
    return FileError::kOpenError;
}

FileError WindowsHelperFileOperations::WriteAtOffset(std::uint64_t offset,
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

FileError WindowsHelperFileOperations::GetSize(std::uint64_t& size) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    size = state_->total_size;
    return state_->total_size > 0 ? FileError::kSuccess : FileError::kSizeError;
}

FileError WindowsHelperFileOperations::Close() {
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
        if (auto* uac = uacBackend()) {
            (void)uac->mapBulkBuffer(sid, 0);
        }
        state_->ring_base = nullptr;
        state_->ring_size = 0;
        state_->slot_in_use.clear();
        state_->zerocopy_active = false;
        state_->zc_slot_bytes = 0;
    }

    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto stats = w.closeSession(sid);
    if (!stats.ok) {
        FileOperationsLog("WindowsHelperFileOperations: closeSession failed: "
                          + stats.error.detail());
        return FileError::kCloseError;
    }

    // §7a cache the helper-reported per-session telemetry so callers
    // (e.g. DownloadThread) can pull it via GetLastSessionStats()
    // after a successful close and feed it into PerformanceStats.
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->last_session_stats = priv::helperSessionStatsFromProto(stats.value);
    }
    return FileError::kSuccess;
}

FileOperations::HelperSessionStats
WindowsHelperFileOperations::GetLastSessionStats() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->last_session_stats;
}

bool WindowsHelperFileOperations::IsOpen() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->open;
}

FileError WindowsHelperFileOperations::WriteSequential(const std::uint8_t* data,
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

FileError WindowsHelperFileOperations::ReadSequential(std::uint8_t* data,
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

void WindowsHelperFileOperations::SetMaxWriteSizeHint(std::size_t bytes) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->max_write_hint = bytes;
}

bool WindowsHelperFileOperations::SetAsyncQueueDepth(int depth) {
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
            if (auto* uac = uacBackend()) {
                (void)uac->mapBulkBuffer(state_->session_id, 0);
            }
            state_->ring_base = nullptr;
            state_->ring_size = 0;
            state_->slot_in_use.clear();
        }
        state_->queue_depth = 1;
        state_->async_ready = false;
        return false;
    }

    auto* uac = uacBackend();
    if (!uac) {
        return false;
    }

    const std::size_t ring_bytes =
        static_cast<std::size_t>(depth) * state_->slot_bytes;
    auto map_r = uac->mapBulkBuffer(state_->session_id, ring_bytes);
    if (!map_r.ok) {
        FileOperationsLog("WindowsHelperFileOperations: mapBulkBuffer failed: "
                          + map_r.error.detail());
        return false;
    }

    state_->ring_base = uac->bulkBufferBase();
    state_->ring_size = uac->bulkBufferSize();
    state_->slot_in_use.assign(static_cast<std::size_t>(depth), false);
    state_->queue_depth = depth;
    state_->async_ready = true;
    state_->cancelled.store(false);
    state_->had_error.store(false);
    state_->pending_writes.store(0);
    return true;
}

bool WindowsHelperFileOperations::adoptZeroCopyRing(std::size_t count, std::size_t slotSize,
                                          std::vector<void*>& outSlots) {
    outSlots.clear();
    if (count == 0 || slotSize == 0) return false;

    // Bound the ring to the helper's RAM-proportional bulk-buffer cap (see
    // maxZeroCopyRingBytes / service_impl.mm). Larger write rings can't be
    // mapped zero-copy, so the caller falls back to heap slots.
    const std::size_t kMaxZeroCopyRingBytes = maxZeroCopyRingBytes();
    if (count > kMaxZeroCopyRingBytes / slotSize) {
        return false;
    }
    const std::size_t ring_bytes = count * slotSize;

    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return false;
    auto* uac = uacBackend();
    if (!uac) return false;

    auto map_r = uac->mapBulkBuffer(state_->session_id, ring_bytes);
    void* base = map_r.ok ? uac->bulkBufferBase() : nullptr;
    if (!base || uac->bulkBufferSize() < ring_bytes) {
        // Map failed and the copy ring is already gone. Degrade to the
        // synchronous write path (always correct) rather than leaving a
        // dangling copy-ring pointer behind; the provider also falls back to
        // heap slots, so writes still complete.
        if (!map_r.ok) {
            FileOperationsLog("WindowsHelperFileOperations: zero-copy mapBulkBuffer failed: "
                              + map_r.error.detail());
        }
        state_->ring_base = nullptr;
        state_->ring_size = 0;
        state_->slot_in_use.clear();
        state_->zerocopy_active = false;
        state_->zc_slot_bytes = 0;
        state_->async_ready = false;
        return false;
    }

    // The zero-copy ring replaces any copy-ring mapping established by
    // SetAsyncQueueDepth. Back-pressure now comes from the producer's
    // RingBuffer, so the copy-path slot pool is unused (cleared here).
    state_->ring_base = base;
    state_->ring_size = ring_bytes;
    state_->slot_in_use.clear();
    state_->zerocopy_active = true;
    state_->zerocopy_engaged = true;
    state_->zc_slot_bytes = slotSize;
    state_->async_ready = true;
    if (state_->queue_depth < 2) {
        // count is bounded by the 256 MB cap / slotSize, so it fits an int.
        state_->queue_depth = static_cast<int>(count);
    }
    state_->cancelled.store(false);
    state_->had_error.store(false);
    state_->pending_writes.store(0);

    outSlots.reserve(count);
    auto* p = static_cast<std::uint8_t*>(base);
    for (std::size_t i = 0; i < count; ++i) {
        outSlots.push_back(p + i * slotSize);
    }
    return true;
}

void WindowsHelperFileOperations::releaseZeroCopyRing() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->zerocopy_active) return;
    if (state_->open) {
        if (auto* uac = uacBackend()) {
            (void)uac->mapBulkBuffer(state_->session_id, 0);
        }
    }
    state_->ring_base = nullptr;
    state_->ring_size = 0;
    state_->zerocopy_active = false;
    state_->zc_slot_bytes = 0;
}

// Provider that backs RingBuffer slots with the helper's shared-memory bulk
// ring (zero-copy), falling back to aligned heap if the ring can't be mapped.
class WindowsWriteBufferProvider : public WriteBufferProvider {
public:
    explicit WindowsWriteBufferProvider(WindowsHelperFileOperations* xpc) : xpc_(xpc) {}
    ~WindowsWriteBufferProvider() override { releaseSlots(); }

    bool allocateSlots(std::size_t count, std::size_t slotSize,
                       std::size_t alignment,
                       std::vector<void*>& outSlots) override {
        releaseSlots();
        if (xpc_ && xpc_->adoptZeroCopyRing(count, slotSize, outSlots)) {
            zero_copy_ = true;
            return true;
        }
        zero_copy_ = false;
        return heap_.allocateSlots(count, slotSize, alignment, outSlots);
    }

    void releaseSlots() override {
        if (zero_copy_) {
            if (xpc_) xpc_->releaseZeroCopyRing();
            zero_copy_ = false;
        } else {
            heap_.releaseSlots();
        }
    }

    bool isZeroCopy() const override { return zero_copy_; }

private:
    WindowsHelperFileOperations* xpc_ = nullptr;  // outlives this provider (see header)
    HeapWriteBufferProvider heap_;
    bool zero_copy_ = false;
};

std::shared_ptr<WriteBufferProvider> WindowsHelperFileOperations::CreateWriteBufferProvider() {
    // A zero-copy ring needs an established write session; before OpenDevice
    // there's nothing to map, so the caller uses a heap provider.
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return nullptr;
    }
    return std::make_shared<WindowsWriteBufferProvider>(this);
}

int WindowsHelperFileOperations::GetAsyncQueueDepth() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->queue_depth;
}

bool WindowsHelperFileOperations::IsAsyncIOSupported() const {
    // Capability check: the helper-routed path always supports async via
    // the shared-memory bulk ring. The base-class contract is "is this
    // platform capable", not "has it been configured" - callers check
    // capability first, then call SetAsyncQueueDepth to enable it.
    return true;
}

FileError WindowsHelperFileOperations::AsyncWriteSequential(const std::uint8_t* data,
                                                    std::size_t size,
                                                    AsyncWriteCallback callback) {
    // Fast paths: not configured for async, oversized chunk, or
    // cancelled -> fall through to synchronous WriteSequential.
    bool use_sync = false;
    bool zero_copy = false;
    std::uint64_t zc_buffer_offset = 0;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) {
            if (callback) callback(FileError::kOpenError, 0);
            return FileError::kOpenError;
        }
        if (state_->cancelled.load()) {
            if (callback) callback(FileError::kCancelled, 0);
            return FileError::kCancelled;
        }
        if (state_->zerocopy_active && state_->ring_base) {
            // The producer filled a slot inside the shared ring; submit its
            // offset directly with no copy. A pointer outside the ring (not
            // expected on this path) falls back to a synchronous write rather
            // than misusing the copy-ring slot pool.
            const auto* base = static_cast<const std::uint8_t*>(state_->ring_base);
            if (data >= base && data + size <= base + state_->ring_size &&
                size <= state_->zc_slot_bytes) {
                zero_copy = true;
                zc_buffer_offset =
                    static_cast<std::uint64_t>(data - base);
            } else {
                use_sync = true;
            }
        } else if (!state_->async_ready || size > state_->slot_bytes) {
            use_sync = true;
        }
    }
    if (use_sync) {
        // Run the synchronous path with the existing write_offset
        // tracking handled inside WriteSequential. Then invoke the
        // callback with the result.
        FileError r = WriteSequential(data, size);
        if (callback) callback(r, r == FileError::kSuccess ? size : 0);
        return r;
    }

    // Determine the ring offset to submit. Zero-copy: the data already lives
    // in the ring, so submit its offset directly. Copy path: acquire a free
    // slot (blocking on back-pressure) and copy the caller's buffer in.
    std::size_t slot_index = State::kNoSlot;
    std::uint64_t buffer_offset = 0;
    if (zero_copy) {
        state_->zc_submits.fetch_add(1, std::memory_order_relaxed);
        buffer_offset = zc_buffer_offset;
    } else {
        state_->copy_submits.fetch_add(1, std::memory_order_relaxed);
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
        slot_index = 0;
        for (std::size_t i = 0; i < state_->slot_in_use.size(); ++i) {
            if (!state_->slot_in_use[i]) { slot_index = i; state_->slot_in_use[i] = true; break; }
        }
        buffer_offset = static_cast<std::uint64_t>(slot_index) * state_->slot_bytes;
        std::uint8_t* slot_base =
            static_cast<std::uint8_t*>(state_->ring_base) + buffer_offset;
        std::memcpy(slot_base, data, size);
    }

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

    auto* uac = uacBackend();
    if (!uac) {
        state_->pending_writes.fetch_sub(1);
        if (callback) callback(FileError::kWriteError, 0);
        return FileError::kWriteError;
    }

    State* st = state_.get();
    const std::size_t slot_for_release = slot_index;
    AsyncWriteCallback user_cb = std::move(callback);

    uac->submitBulkWriteAsync(
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

            // Free the copy-ring slot (zero-copy submits have none; the
            // producer's RingBuffer owns slot lifetime there).
            if (slot_for_release != State::kNoSlot) {
                {
                    std::lock_guard<std::mutex> lk(st->slot_mutex);
                    st->slot_in_use[slot_for_release] = false;
                }
                st->slot_cv.notify_one();
            }

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

int WindowsHelperFileOperations::GetPendingWriteCount() const {
    return state_->pending_writes.load();
}

FileError WindowsHelperFileOperations::WaitForPendingWrites() {
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

void WindowsHelperFileOperations::CancelAsyncIO() {
    state_->cancelled.store(true);
    state_->slot_cv.notify_all();
    {
        std::lock_guard<std::mutex> lk(state_->pending_mutex);
        state_->pending_cv.notify_all();
    }
}

void WindowsHelperFileOperations::GetAsyncIOStats(uint32_t& wallClockMs,
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

void WindowsHelperFileOperations::GetZeroCopyWriteStats(bool& engaged,
                                                std::uint64_t& zeroCopySubmits,
                                                std::uint64_t& copySubmits) const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    engaged = state_->zerocopy_engaged;
    zeroCopySubmits = state_->zc_submits.load(std::memory_order_relaxed);
    copySubmits = state_->copy_submits.load(std::memory_order_relaxed);
}

FileError WindowsHelperFileOperations::Seek(std::uint64_t position) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    state_->write_offset = position;
    state_->read_offset = position;
    return FileError::kSuccess;
}

std::uint64_t WindowsHelperFileOperations::Tell() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->write_offset;
}

FileError WindowsHelperFileOperations::ForceSync() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    auto sid = state_->session_id;
    auto& w = ::rpi_imager::getProcessPrivilegedWriter();
    auto r = w.syncDevice(sid);
    if (!r.ok) return FileError::kSyncError;
    return FileError::kSuccess;
}

FileError WindowsHelperFileOperations::Flush() {
    // No client-side buffering above the XPC layer, so flush is a
    // no-op (all writes are submitted by the time WriteSequential
    // returns).
    return FileError::kSuccess;
}

void WindowsHelperFileOperations::PrepareForSequentialRead(std::uint64_t offset,
                                                    std::uint64_t /*length*/) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->read_offset = offset;
    // F_RDAHEAD / F_NOCACHE hints don't cross the XPC boundary. The
    // helper opens with O_RDWR and the kernel's read-ahead heuristics
    // apply on its side.
}

FileOperations::FastVerifyResult WindowsHelperFileOperations::FastVerifySha256(
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
        FileOperationsLog("WindowsHelperFileOperations: hashDeviceSha256 failed: "
                          + r.error.detail());
        out.error = FileError::kReadError;
        return out;
    }
    out.error = FileError::kSuccess;
    out.digest = std::move(r.value);
    return out;
}

FileError WindowsHelperFileOperations::PrepareDevice(std::uint64_t /*device_size*/,
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
        FileOperationsLog("WindowsHelperFileOperations: prepareDevice failed: "
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

bool WindowsHelperFileOperations::IsDirectIOEnabled() const {
    // The helper opens the device with the direct-I/O hint set on its
    // own FD. From the unprivileged side we don't have a meaningful
    // way to query that without an additional XPC round-trip; report
    // true to match the configured open-time behaviour.
    return true;
}

FileError WindowsHelperFileOperations::SetDirectIOEnabled(bool /*enabled*/) {
    // Direct-I/O is fixed at openSession time on the helper side.
    return FileError::kSuccess;
}

FileOperations::DirectIOInfo WindowsHelperFileOperations::GetDirectIOInfo() const {
    DirectIOInfo info;
    info.attempted = true;
    info.succeeded = true;
    info.currently_enabled = true;
    info.error_code = 0;
    info.error_message = "(via helper)";
    return info;
}

std::unique_ptr<FileOperations> CreateWindowsHelperFileOperations() {
    return std::make_unique<WindowsHelperFileOperations>();
}

} // namespace rpi_imager
