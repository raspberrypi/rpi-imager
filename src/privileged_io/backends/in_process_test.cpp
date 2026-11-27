// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// In-process test backend implementation. See header for design rationale.

#include "in_process_test.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>

namespace rpi_imager::privileged::backends {

namespace fs = std::filesystem;

namespace {

std::string makeTempPath() {
    auto dir = fs::temp_directory_path();
    std::random_device rd;
    std::stringstream ss;
    ss << "rpi-imager-test-" << std::hex << rd();
    return (dir / ss.str()).string();
}

// Opening for append creates the file without discarding what a caller-supplied
// device file already holds; resize_file then sets the length either way.
bool truncateOrCreate(const std::string& path, std::uint64_t size) {
    {
        std::ofstream create(path, std::ios::binary | std::ios::app);
        if (!create) return false;
    }
    std::error_code ec;
    fs::resize_file(path, size, ec);
    return !ec;
}

// Callers hold the session's io_mutex. Every transfer seeks first, which is
// also what lets one fstream alternate between reading and writing.
bool writeAt(std::fstream& f, std::uint64_t offset, const void* buf, std::size_t len) {
    f.clear();
    f.seekp(static_cast<std::streamoff>(offset));
    f.write(static_cast<const char*>(buf), static_cast<std::streamsize>(len));
    return static_cast<bool>(f);
}

// Reading past the end of the file is a short read, as pread(2) reports it,
// so only a bad stream counts as failure.
bool readAt(std::fstream& f, std::uint64_t offset, void* buf, std::size_t len,
            std::size_t& out_read) {
    f.clear();
    f.seekg(static_cast<std::streamoff>(offset));
    f.read(static_cast<char*>(buf), static_cast<std::streamsize>(len));
    out_read = static_cast<std::size_t>(f.gcount());
    const bool ok = !f.bad();
    f.clear();
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

InProcessTestBackend::InProcessTestBackend()
    : InProcessTestBackend(Options{}) {}

InProcessTestBackend::InProcessTestBackend(Options opts)
    : options_(std::move(opts)) {}

InProcessTestBackend::~InProcessTestBackend() {
    // Tear down any sessions still open. Tests should always closeSession()
    // explicitly; this is a safety net.
    std::unique_lock<std::mutex> lk(sessions_mutex_);
    auto sessions = std::move(sessions_);
    lk.unlock();

    for (auto& [_, sess] : sessions) {
        sess->stop.store(true);
        sess->queue_cv.notify_all();
        if (sess->worker.joinable()) sess->worker.join();
        sess->file.close();
        if (sess->owns_tempfile && !sess->device_path.empty()) {
            std::error_code ec;
            fs::remove(sess->device_path, ec);
        }
    }
}

// ---------------------------------------------------------------------------
// Helper lifecycle (no-op)
// ---------------------------------------------------------------------------

Result<proto_ns::HelperStatus> InProcessTestBackend::queryHelperStatus() {
    proto_ns::HelperStatus s;
    s.set_state(proto_ns::HELPER_STATE_INSTALLED_READY);
    s.set_installed_version("in-process-test");
    s.set_client_version("in-process-test");
    return Result<proto_ns::HelperStatus>::success(std::move(s));
}

Result<void> InProcessTestBackend::installHelper() {
    return Result<void>::success();
}

Result<void> InProcessTestBackend::uninstallHelper() {
    return Result<void>::success();
}

std::string InProcessTestBackend::backendDescription() const {
    return "InProcessTestBackend (tempfile-backed pseudo-device)";
}

// ---------------------------------------------------------------------------
// Drive enumeration
// ---------------------------------------------------------------------------

Result<std::vector<proto_ns::DriveInfo>> InProcessTestBackend::listDrivesNow() {
    std::vector<proto_ns::DriveInfo> drives;
    proto_ns::DriveInfo d;
    d.set_device_path(options_.device_file_path.empty() ? "/tmp/rpi-imager-test-device"
                                                        : options_.device_file_path);
    d.set_display_name("InProcessTestBackend synthetic device");
    d.set_size_bytes(options_.device_bytes);
    d.set_is_removable(true);
    d.set_is_usb(false);
    d.set_is_system_drive(false);
    d.set_logical_block_size(512);
    drives.push_back(std::move(d));
    return Result<std::vector<proto_ns::DriveInfo>>::success(std::move(drives));
}

Result<void> InProcessTestBackend::subscribeDrives(DriveChangeCallback /*cb*/) {
    // No drive change events in the test backend.
    return Result<void>::success();
}

Result<void> InProcessTestBackend::unsubscribeDrives() {
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

Result<proto_ns::SessionId> InProcessTestBackend::openSession(
    const std::string& device_path, const proto_ns::OpenOptions& opts) {

    auto sess = std::make_unique<Session>();
    sess->options = opts;

    // Resolve device path. Caller may pass empty to mean "create a tempfile".
    if (!device_path.empty()) {
        sess->device_path = device_path;
    } else if (!options_.device_file_path.empty()) {
        sess->device_path = options_.device_file_path;
    } else {
        sess->device_path = makeTempPath();
        sess->owns_tempfile = true;
    }

    if (!truncateOrCreate(sess->device_path, options_.device_bytes)) {
        return Result<proto_ns::SessionId>::failure(makeError(
            proto_ns::ERROR_DEVICE_PERMISSION,
            "could not create or truncate test device file: " + sess->device_path,
            errno));
    }

    sess->file.open(sess->device_path,
                    std::ios::in | std::ios::out | std::ios::binary);
    if (!sess->file.is_open()) {
        return Result<proto_ns::SessionId>::failure(makeError(
            proto_ns::ERROR_DEVICE_PERMISSION,
            "could not open test device file: " + sess->device_path,
            errno));
    }

    // Allocate slots
    sess->slots.resize(options_.slot_count);
    for (auto& slot : sess->slots) {
        slot.buffer.resize(options_.slot_capacity);
        slot.in_use = false;
    }

    sess->id = next_session_id_.fetch_add(1);
    sess->started_at = std::chrono::steady_clock::now();

    Session* sess_raw = sess.get();
    sess->worker = std::thread([this, sess_raw]() { workerLoop(sess_raw); });

    proto_ns::SessionId sid;
    sid.set_v(sess->id);

    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        sessions_.emplace(sess->id, std::move(sess));
    }

    return Result<proto_ns::SessionId>::success(std::move(sid));
}

Result<proto_ns::DeviceLimits> InProcessTestBackend::queryDeviceLimits(
    const proto_ns::SessionId& sid) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<proto_ns::DeviceLimits>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    proto_ns::DeviceLimits limits;
    limits.set_max_transfer_bytes(static_cast<std::uint32_t>(options_.slot_capacity));
    limits.set_logical_block_size(512);
    limits.set_total_bytes(options_.device_bytes);
    return Result<proto_ns::DeviceLimits>::success(std::move(limits));
}

Result<void> InProcessTestBackend::prepareDevice(
    const proto_ns::SessionId& sid, const proto_ns::PrepareOptions& opts) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<void>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    // Optionally zero the first/last MB to mimic real prepareDevice() behaviour.
    constexpr std::size_t kMb = 1ull * 1024 * 1024;
    std::vector<std::uint8_t> zeros(kMb, 0);

    std::lock_guard<std::mutex> io(s->io_mutex);
    if (opts.zero_first_mb()) {
        if (!writeAt(s->file, 0, zeros.data(), kMb)) {
            return Result<void>::failure(makeError(
                proto_ns::ERROR_WRITE_FAILED, "zero first MB failed", errno));
        }
    }
    if (opts.zero_last_mb() && options_.device_bytes >= kMb) {
        if (!writeAt(s->file, options_.device_bytes - kMb, zeros.data(), kMb)) {
            return Result<void>::failure(makeError(
                proto_ns::ERROR_WRITE_FAILED, "zero last MB failed", errno));
        }
    }

    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Bulk write plane
// ---------------------------------------------------------------------------

Result<Slot> InProcessTestBackend::acquireSlot(const proto_ns::SessionId& sid) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<Slot>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    std::unique_lock<std::mutex> lk(s->slot_mutex);
    s->slot_cv.wait(lk, [s] {
        for (const auto& slot : s->slots) if (!slot.in_use) return true;
        return false;
    });

    for (std::uint32_t i = 0; i < s->slots.size(); ++i) {
        if (!s->slots[i].in_use) {
            s->slots[i].in_use = true;
            Slot out;
            out.data = s->slots[i].buffer.data();
            out.capacity = s->slots[i].buffer.size();
            out.index = i;
            return Result<Slot>::success(out);
        }
    }
    // Shouldn't reach here given the wait condition above, but be defensive.
    return Result<Slot>::failure(
        makeError(proto_ns::ERROR_SLOT_EXHAUSTED, "no slot available after wait"));
}

void InProcessTestBackend::submitWrite(const proto_ns::SessionId& sid,
                                        std::uint32_t slot_index,
                                        std::uint64_t offset,
                                        std::uint64_t length,
                                        WriteCompleteCallback on_complete) {
    auto* s = sessionOrNull(sid);
    if (!s) {
        if (on_complete) {
            proto_ns::WriteResult wr;
            wr.set_slot_index(slot_index);
            *wr.mutable_error() = makeError(proto_ns::ERROR_SESSION_NOT_FOUND,
                                            "no such session");
            on_complete(wr);
        }
        return;
    }

    PendingWrite pw;
    pw.slot_index = slot_index;
    pw.offset = offset;
    pw.length = length;
    pw.callback = std::move(on_complete);

    {
        std::lock_guard<std::mutex> lk(s->queue_mutex);
        s->queue.push_back(std::move(pw));
    }
    s->queue_cv.notify_one();
}

Result<std::size_t> InProcessTestBackend::readChunk(const proto_ns::SessionId& sid,
                                                      std::uint64_t offset,
                                                      std::byte* buf,
                                                      std::size_t len) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<std::size_t>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    std::size_t n = 0;
    std::lock_guard<std::mutex> io(s->io_mutex);
    if (!readAt(s->file, offset, buf, len, n)) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_DEVICE_IO, "read failed", errno));
    }
    return Result<std::size_t>::success(n);
}

Result<std::size_t> InProcessTestBackend::writeChunk(const proto_ns::SessionId& sid,
                                                       std::uint64_t offset,
                                                       const std::byte* buf,
                                                       std::size_t len) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<std::size_t>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    std::lock_guard<std::mutex> io(s->io_mutex);
    if (!writeAt(s->file, offset, buf, len)) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_WRITE_FAILED, "write failed", errno));
    }
    return Result<std::size_t>::success(len);
}

Result<void> InProcessTestBackend::syncDevice(const proto_ns::SessionId& sid) {
    auto* s = sessionOrNull(sid);
    if (!s) return Result<void>::failure(
        makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));

    // A tempfile has no device cache to drain; flushing the stream is what
    // makes earlier writes visible to anything else reading the file.
    std::lock_guard<std::mutex> io(s->io_mutex);
    if (!s->file.flush()) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SYNC_FAILED, "flush failed", errno));
    }
    return Result<void>::success();
}

Result<proto_ns::SessionStats> InProcessTestBackend::closeSession(
    const proto_ns::SessionId& sid) {
    std::unique_ptr<Session> sess;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(sid.v());
        if (it == sessions_.end()) {
            return Result<proto_ns::SessionStats>::failure(
                makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
        }
        sess = std::move(it->second);
        sessions_.erase(it);
    }

    // Stop the worker, drain anything queued (callbacks fire before we exit).
    sess->stop.store(true);
    sess->queue_cv.notify_all();
    if (sess->worker.joinable()) sess->worker.join();

    sess->file.close();
    if (sess->owns_tempfile && !sess->device_path.empty()) {
        std::error_code ec;
        fs::remove(sess->device_path, ec);
    }

    auto duration = std::chrono::steady_clock::now() - sess->started_at;
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

    proto_ns::SessionStats stats;
    stats.set_state(sess->writes_failed.load() == 0
                        ? proto_ns::SESSION_STATE_SUCCEEDED
                        : proto_ns::SESSION_STATE_FAILED);
    stats.set_bytes_written(sess->bytes_written.load());
    stats.set_duration_ms(static_cast<std::uint64_t>(duration_ms));

    return Result<proto_ns::SessionStats>::success(std::move(stats));
}

// ---------------------------------------------------------------------------
// Maintenance (no-op)
// ---------------------------------------------------------------------------

Result<void> InProcessTestBackend::unmount(const std::string& /*device_path*/) {
    return Result<void>::success();
}

Result<void> InProcessTestBackend::eject(const std::string& /*device_path*/) {
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

InProcessTestBackend::Session* InProcessTestBackend::sessionOrNull(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    auto it = sessions_.find(sid.v());
    return it == sessions_.end() ? nullptr : it->second.get();
}

void InProcessTestBackend::workerLoop(Session* s) {
    while (true) {
        PendingWrite pw;
        {
            std::unique_lock<std::mutex> lk(s->queue_mutex);
            s->queue_cv.wait(lk, [s] { return !s->queue.empty() || s->stop.load(); });
            if (s->queue.empty()) return;        // stop with empty queue → exit
            pw = std::move(s->queue.front());
            s->queue.pop_front();
        }

        // Optional simulated latency
        if (options_.simulated_write_latency.count() > 0) {
            std::this_thread::sleep_for(options_.simulated_write_latency);
        }

        const auto submit_time = std::chrono::steady_clock::now();
        const auto& slot_buf = s->slots[pw.slot_index].buffer;

        bool wrote = false;
        {
            std::lock_guard<std::mutex> io(s->io_mutex);
            wrote = pw.length <= slot_buf.size() &&
                    writeAt(s->file, pw.offset, slot_buf.data(),
                            static_cast<std::size_t>(pw.length));
        }

        proto_ns::WriteResult wr;
        wr.set_slot_index(pw.slot_index);

        const auto latency = std::chrono::steady_clock::now() - submit_time;
        wr.set_latency_us(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(latency).count()));

        if (!wrote) {
            *wr.mutable_error() = makeError(proto_ns::ERROR_WRITE_FAILED,
                                            "slot write failed",
                                            errno);
            s->writes_failed.fetch_add(1);
        } else {
            wr.set_bytes_written(pw.length);
            s->bytes_written.fetch_add(pw.length);
            s->writes_completed.fetch_add(1);
        }

        // Release slot back to the pool BEFORE invoking the callback; the
        // callback may turn around and acquire another slot, and we want
        // it to see this one as available.
        releaseSlot(s, pw.slot_index);

        if (pw.callback) pw.callback(wr);
    }
}

void InProcessTestBackend::releaseSlot(Session* s, std::uint32_t idx) {
    std::lock_guard<std::mutex> lk(s->slot_mutex);
    s->slots[idx].in_use = false;
    s->slot_cv.notify_one();
}

proto_ns::ErrorInfo InProcessTestBackend::makeError(proto_ns::ErrorCode code,
                                                     const std::string& detail,
                                                     int kernel_errno) {
    proto_ns::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    e.set_kernel_errno(kernel_errno);
    return e;
}

} // namespace rpi_imager::privileged::backends
