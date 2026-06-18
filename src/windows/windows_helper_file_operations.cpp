// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "windows_helper_file_operations.h"

#include "../privileged_io/backends/windows_uac.h"
#include "../privileged_io_glue.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <vector>

namespace rpi_imager {

namespace priv = rpi_imager::privileged;

namespace {

priv::backends::WindowsUacBackend* uacBackend() {
    auto& w = getProcessPrivilegedWriter();
    return dynamic_cast<priv::backends::WindowsUacBackend*>(&w);
}

}  // namespace

struct WindowsHelperFileOperations::State {
    bool open = false;
    priv::proto::SessionId session_id;
    std::uint64_t total_size = 0;
    std::uint64_t write_offset = 0;
    std::uint64_t read_offset = 0;
    mutable std::mutex mutex;

    static constexpr std::size_t kMaxSlotBytes = 32ull * 1024 * 1024;
    static constexpr std::size_t kMinSlotBytes =  1ull * 1024 * 1024;
    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

    std::size_t max_write_hint = 0;
    std::size_t slot_bytes = kMaxSlotBytes;
    int queue_depth = 1;
    bool async_ready = false;
    void* ring_base = nullptr;
    std::size_t ring_size = 0;
    std::vector<bool> slot_in_use;

    mutable std::mutex slot_mutex;
    std::condition_variable slot_cv;
    std::atomic<int> pending_writes{0};
    mutable std::condition_variable pending_cv;
    mutable std::mutex pending_mutex;
    std::atomic<bool> cancelled{false};
};

WindowsHelperFileOperations::WindowsHelperFileOperations()
    : state_(std::make_unique<State>()) {}

WindowsHelperFileOperations::~WindowsHelperFileOperations() {
    (void)Close();
}

FileError WindowsHelperFileOperations::OpenDevice(const std::string& path) {
    auto& w = getProcessPrivilegedWriter();

    auto status = w.queryHelperStatus();
    if (!status.ok) {
        FileOperationsLog("WindowsHelperFileOperations: helper status failed: "
                          + status.error.detail());
        return FileError::kOpenError;
    }

    using HS = priv::proto::HelperState;
    if (status.value.state() == HS::HELPER_STATE_NOT_INSTALLED) {
        auto install = w.installHelper();
        if (!install.ok) {
            FileOperationsLog("WindowsHelperFileOperations: installHelper failed: "
                              + install.error.detail());
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
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->session_id = open_r.value;
        state_->write_offset = 0;
        state_->read_offset = 0;
        state_->open = true;
        state_->total_size = limits.ok ? limits.value.total_bytes() : 0;
        if (limits.ok) {
            device_io_limits_.max_transfer_bytes = limits.value.max_transfer_bytes();
        }
    }
    return FileError::kSuccess;
}

FileError WindowsHelperFileOperations::CreateTestFile(const std::string&,
                                                      std::uint64_t) {
    return FileError::kOpenError;
}

FileError WindowsHelperFileOperations::WriteAtOffset(std::uint64_t offset,
                                                     const std::uint8_t* data,
                                                     std::size_t size) {
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        sid = state_->session_id;
    }

    auto r = getProcessPrivilegedWriter().writeChunk(
        sid, offset, reinterpret_cast<const std::byte*>(data), size);
    if (!r.ok || r.value != size) return FileError::kWriteError;
    return FileError::kSuccess;
}

FileError WindowsHelperFileOperations::GetSize(std::uint64_t& size) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->open) return FileError::kOpenError;
    size = state_->total_size;
    return state_->total_size > 0 ? FileError::kSuccess : FileError::kSizeError;
}

FileError WindowsHelperFileOperations::Close() {
    (void)WaitForPendingWrites();

    priv::proto::SessionId sid;
    bool was_open = false;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        was_open = state_->open;
        sid = state_->session_id;
        state_->open = false;
    }
    if (!was_open) return FileError::kSuccess;

    if (auto* uac = uacBackend()) {
        (void)uac->mapBulkBuffer(sid, 0);
    }
    (void)getProcessPrivilegedWriter().closeSession(sid);

    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->ring_base = nullptr;
    state_->ring_size = 0;
    state_->slot_in_use.clear();
    state_->async_ready = false;
    state_->queue_depth = 1;
    return FileError::kSuccess;
}

bool WindowsHelperFileOperations::IsOpen() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->open;
}

FileError WindowsHelperFileOperations::WriteSequential(const std::uint8_t* data,
                                                       std::size_t size) {
    std::uint64_t offset = 0;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        offset = state_->write_offset;
        state_->write_offset += size;
    }
    return WriteAtOffset(offset, data, size);
}

FileError WindowsHelperFileOperations::ReadSequential(std::uint8_t* data,
                                                      std::size_t size,
                                                      std::size_t& bytes_read) {
    bytes_read = 0;
    if (size == 0) return FileError::kSuccess;

    priv::proto::SessionId sid;
    std::uint64_t offset = 0;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        sid = state_->session_id;
        offset = state_->read_offset;
    }

    auto r = getProcessPrivilegedWriter().readChunk(
        sid, offset, reinterpret_cast<std::byte*>(data), size);
    if (!r.ok) return FileError::kReadError;

    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->read_offset += r.value;
    }
    bytes_read = r.value;
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
    if (!uac) return false;

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
    state_->pending_writes.store(0);
    return true;
}

int WindowsHelperFileOperations::GetAsyncQueueDepth() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->queue_depth;
}

bool WindowsHelperFileOperations::IsAsyncIOSupported() const {
    return true;
}

FileError WindowsHelperFileOperations::AsyncWriteSequential(
    const std::uint8_t* data, std::size_t size, AsyncWriteCallback callback) {
    bool use_sync = false;
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
        if (!state_->async_ready || size > state_->slot_bytes) {
            use_sync = true;
        }
    }
    if (use_sync) {
        FileError r = WriteSequential(data, size);
        if (callback) callback(r, r == FileError::kSuccess ? size : 0);
        return r;
    }

    std::size_t slot_index = State::kNoSlot;
    std::uint64_t buffer_offset = 0;
    {
        std::unique_lock<std::mutex> slot_lk(state_->slot_mutex);
        state_->slot_cv.wait(slot_lk, [this] {
            if (state_->cancelled.load()) return true;
            for (bool b : state_->slot_in_use) {
                if (!b) return true;
            }
            return false;
        });
        if (state_->cancelled.load()) {
            if (callback) callback(FileError::kCancelled, 0);
            return FileError::kCancelled;
        }
        for (std::size_t i = 0; i < state_->slot_in_use.size(); ++i) {
            if (!state_->slot_in_use[i]) {
                slot_index = i;
                state_->slot_in_use[i] = true;
                break;
            }
        }
        buffer_offset = static_cast<std::uint64_t>(slot_index) * state_->slot_bytes;
        std::memcpy(static_cast<std::uint8_t*>(state_->ring_base) + buffer_offset,
                    data, size);
    }

    std::uint64_t device_offset = 0;
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        device_offset = state_->write_offset;
        state_->write_offset += size;
        sid = state_->session_id;
    }

    state_->pending_writes.fetch_add(1);

    auto* uac = uacBackend();
    if (!uac) {
        state_->pending_writes.fetch_sub(1);
        if (slot_index != State::kNoSlot) {
            std::lock_guard<std::mutex> slot_lk(state_->slot_mutex);
            state_->slot_in_use[slot_index] = false;
            state_->slot_cv.notify_one();
        }
        if (callback) callback(FileError::kWriteError, 0);
        return FileError::kWriteError;
    }

    State* st = state_.get();
    const std::size_t slot_for_release = slot_index;
    AsyncWriteCallback user_cb = std::move(callback);

    uac->submitBulkWriteAsync(
        sid, buffer_offset, device_offset, size,
        [st, slot_for_release, size, user_cb = std::move(user_cb)](
            priv::Result<std::size_t> result) mutable {
            if (slot_for_release != State::kNoSlot) {
                std::lock_guard<std::mutex> slot_lk(st->slot_mutex);
                st->slot_in_use[slot_for_release] = false;
                st->slot_cv.notify_one();
            }
            st->pending_writes.fetch_sub(1);
            {
                std::lock_guard<std::mutex> plk(st->pending_mutex);
                st->pending_cv.notify_all();
            }
            FileError fe = FileError::kSuccess;
            std::size_t wrote = 0;
            if (!result.ok || result.value != size) {
                fe = FileError::kWriteError;
            } else {
                wrote = result.value;
            }
            if (user_cb) user_cb(fe, wrote);
        });

    return FileError::kSuccess;
}

int WindowsHelperFileOperations::GetPendingWriteCount() const {
    return state_->pending_writes.load();
}

FileError WindowsHelperFileOperations::WaitForPendingWrites() {
    std::unique_lock<std::mutex> lk(state_->pending_mutex);
    state_->pending_cv.wait(lk, [this] {
        return state_->pending_writes.load() == 0;
    });
    return FileError::kSuccess;
}

void WindowsHelperFileOperations::CancelAsyncIO() {
    state_->cancelled.store(true);
    state_->slot_cv.notify_all();
}

FileError WindowsHelperFileOperations::Seek(std::uint64_t position) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->write_offset = position;
    state_->read_offset = position;
    return FileError::kSuccess;
}

std::uint64_t WindowsHelperFileOperations::Tell() const {
    std::lock_guard<std::mutex> lk(state_->mutex);
    return state_->write_offset;
}

FileError WindowsHelperFileOperations::ForceSync() {
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        sid = state_->session_id;
    }
    auto r = getProcessPrivilegedWriter().syncDevice(sid);
    return r.ok ? FileError::kSuccess : FileError::kSyncError;
}

FileError WindowsHelperFileOperations::Flush() {
    return ForceSync();
}

void WindowsHelperFileOperations::PrepareForSequentialRead(std::uint64_t offset,
                                                         std::uint64_t /*length*/) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->read_offset = offset;
}

FileError WindowsHelperFileOperations::PrepareDevice(std::uint64_t device_size,
                                                     bool zero_last_mb) {
    priv::proto::SessionId sid;
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        if (!state_->open) return FileError::kOpenError;
        sid = state_->session_id;
    }

    priv::proto::PrepareOptions opts;
    opts.set_zero_last_mb(zero_last_mb);
    auto r = getProcessPrivilegedWriter().prepareDevice(sid, opts);
    if (!r.ok) return FileError::kWriteError;

    std::lock_guard<std::mutex> lk(state_->mutex);
    if (device_size > 0) {
        state_->total_size = device_size;
    }
    return FileError::kSuccess;
}

int WindowsHelperFileOperations::GetLastErrorCode() const {
    return 0;
}

bool WindowsHelperFileOperations::IsDirectIOEnabled() const {
    return true;
}

FileError WindowsHelperFileOperations::SetDirectIOEnabled(bool /*enabled*/) {
    return FileError::kSuccess;
}

FileOperations::DirectIOInfo WindowsHelperFileOperations::GetDirectIOInfo() const {
    DirectIOInfo info;
    info.attempted = true;
    info.succeeded = true;
    info.currently_enabled = true;
    return info;
}

std::unique_ptr<FileOperations> CreateWindowsHelperFileOperations() {
    return std::make_unique<WindowsHelperFileOperations>();
}

} // namespace rpi_imager
