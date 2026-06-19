// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "linux_embedded.h"

#include "../wire/linux_shared_memory.h"

#include "../../linux/helper_maintenance.h"

#include "../../dependencies/yescrypt/sha256.h"

#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace rpi_imager::privileged::backends {

namespace {

constexpr std::uint64_t kMaxSessionBytes = 64ull * 1024 * 1024 * 1024;

proto_ns::ErrorInfo makeError(proto_ns::ErrorCode code,
                              const std::string& detail,
                              int kernel_errno = 0) {
    proto_ns::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    if (kernel_errno != 0) {
        e.set_kernel_errno(kernel_errno);
    }
    return e;
}

bool isBlockDevicePath(const std::string& p) {
    return p.size() > 5 && p.compare(0, 5, "/dev/") == 0;
}

std::uint64_t deviceSizeBytes(int fd) {
    std::uint64_t size = 0;
    if (::ioctl(fd, BLKGETSIZE64, &size) == 0 && size > 0) {
        return size;
    }
    struct stat st {};
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
        return static_cast<std::uint64_t>(st.st_size);
    }
    return 0;
}

} // namespace

struct LinuxEmbeddedBackend::State {
    struct Session {
        std::uint64_t id = 0;
        std::string   path;
        int           fd = -1;
        wire::LinuxSharedMemory bulk;
        std::uint64_t bytes_written = 0;
        std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    };

    std::mutex mutex;
    std::unordered_map<std::uint64_t, Session> sessions;
    std::uint64_t next_id = 1;

    wire::LinuxSharedMemory client_bulk;
    proto_ns::SessionId bulk_session;

    std::thread worker;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    struct AsyncJob {
        proto_ns::SessionId sid;
        std::uint64_t buffer_offset;
        std::uint64_t device_offset;
        std::size_t length;
        BulkWriteCallback cb;
    };
    std::deque<AsyncJob> queue;
    bool worker_stop = false;
    bool worker_started = false;

    ~State() {
        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            worker_stop = true;
        }
        queue_cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        for (auto& [_, s] : sessions) {
            s.bulk.release();
            if (s.fd >= 0) {
                ::close(s.fd);
            }
        }
        client_bulk.release();
    }
};

LinuxEmbeddedBackend::LinuxEmbeddedBackend()
    : state_(std::make_unique<State>()) {}

LinuxEmbeddedBackend::~LinuxEmbeddedBackend() = default;

Result<proto_ns::HelperStatus> LinuxEmbeddedBackend::queryHelperStatus() {
    proto_ns::HelperStatus s;
    s.set_state(proto_ns::HELPER_STATE_INSTALLED_READY);
    s.set_installed_version("linux-embedded");
    s.set_client_version("linux-embedded");
    return Result<proto_ns::HelperStatus>::success(std::move(s));
}

Result<void> LinuxEmbeddedBackend::installHelper() {
    return Result<void>::success();
}

Result<void> LinuxEmbeddedBackend::uninstallHelper() {
    return Result<void>::success();
}

std::string LinuxEmbeddedBackend::backendDescription() const {
    return "LinuxEmbeddedBackend (in-process, running as root)";
}

Result<std::vector<proto_ns::DriveInfo>> LinuxEmbeddedBackend::listDrivesNow() {
    return Result<std::vector<proto_ns::DriveInfo>>::failure(
        makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                  "listDrivesNow: client poll loop handles enumeration"));
}

Result<void> LinuxEmbeddedBackend::subscribeDrives(DriveChangeCallback) {
    return Result<void>::success();
}

Result<void> LinuxEmbeddedBackend::unsubscribeDrives() {
    return Result<void>::success();
}

Result<proto_ns::SessionId> LinuxEmbeddedBackend::openSession(
    const std::string& device_path, const proto_ns::OpenOptions& opts) {
    if (!isBlockDevicePath(device_path)) {
        return Result<proto_ns::SessionId>::failure(
            makeError(proto_ns::ERROR_DEVICE_PERMISSION,
                      "only /dev/* block device paths are accepted"));
    }

    int flags = O_RDWR;
    if (opts.direct_io()) {
        flags |= O_DIRECT;
    }
    const int fd = ::open(device_path.c_str(), flags);
    if (fd < 0) {
        return Result<proto_ns::SessionId>::failure(
            makeError(proto_ns::ERROR_DEVICE_PERMISSION,
                      "open failed: " + device_path, errno));
    }

    State::Session s;
    s.id = state_->next_id++;
    s.path = device_path;
    s.fd = fd;

    proto_ns::SessionId sid;
    sid.set_v(s.id);
    {
        std::lock_guard<std::mutex> lk(state_->mutex);
        state_->sessions.emplace(s.id, std::move(s));
    }
    return Result<proto_ns::SessionId>::success(std::move(sid));
}

Result<proto_ns::DeviceLimits> LinuxEmbeddedBackend::queryDeviceLimits(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<proto_ns::DeviceLimits>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    const std::uint64_t size = deviceSizeBytes(it->second.fd);
    proto_ns::DeviceLimits lim;
    lim.set_total_bytes(size);
    lim.set_max_transfer_bytes(32u * 1024u * 1024u);
    lim.set_logical_block_size(512);
    return Result<proto_ns::DeviceLimits>::success(std::move(lim));
}

Result<void> LinuxEmbeddedBackend::prepareDevice(
    const proto_ns::SessionId& sid, const proto_ns::PrepareOptions& opts) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    constexpr std::size_t kMb = 1ull * 1024 * 1024;
    std::vector<std::uint8_t> zeros(kMb, 0);
    Session& s = it->second;
    const std::uint64_t dev_size = deviceSizeBytes(s.fd);

    if (opts.zero_first_mb()) {
        if (::pwrite(s.fd, zeros.data(), kMb, 0) != static_cast<ssize_t>(kMb)) {
            return Result<void>::failure(
                makeError(proto_ns::ERROR_WRITE_FAILED, "zero first MB failed", errno));
        }
    }
    if (opts.zero_last_mb() && dev_size >= kMb) {
        const off_t off = static_cast<off_t>(dev_size - kMb);
        if (::pwrite(s.fd, zeros.data(), kMb, off) != static_cast<ssize_t>(kMb)) {
            return Result<void>::failure(
                makeError(proto_ns::ERROR_WRITE_FAILED, "zero last MB failed", errno));
        }
    }
    if (::fsync(s.fd) != 0) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SYNC_FAILED, "fsync after prepare failed", errno));
    }
    return Result<void>::success();
}

Result<Slot> LinuxEmbeddedBackend::acquireSlot(const proto_ns::SessionId&) {
    return Result<Slot>::failure(
        makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                  "acquireSlot: use mapBulkBuffer/bulkWriteFromBuffer"));
}

void LinuxEmbeddedBackend::submitWrite(const proto_ns::SessionId&,
                                       std::uint32_t, std::uint64_t, std::uint64_t,
                                       WriteCompleteCallback on_complete) {
    if (on_complete) {
        proto_ns::WriteResult wr;
        *wr.mutable_error() = makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                                        "submitWrite: use submitBulkWriteAsync");
        on_complete(wr);
    }
}

Result<std::size_t> LinuxEmbeddedBackend::readChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    std::byte* buf, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    const ssize_t n = ::pread(it->second.fd, buf, len, static_cast<off_t>(offset));
    if (n < 0) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_DEVICE_IO, "pread failed", errno));
    }
    return Result<std::size_t>::success(static_cast<std::size_t>(n));
}

Result<std::size_t> LinuxEmbeddedBackend::writeChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    const std::byte* in, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    const ssize_t n = ::pwrite(it->second.fd, in, len, static_cast<off_t>(offset));
    if (n < 0) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_WRITE_FAILED, "pwrite failed", errno));
    }
    it->second.bytes_written += static_cast<std::uint64_t>(n);
    return Result<std::size_t>::success(static_cast<std::size_t>(n));
}

Result<std::string> LinuxEmbeddedBackend::hashDeviceSha256(
    const proto_ns::SessionId& sid, const std::string& prefix,
    std::uint64_t device_offset, std::uint64_t length) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<std::string>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    if (!prefix.empty()) {
        SHA256_Update(&ctx, prefix.data(), prefix.size());
    }

    constexpr std::size_t kChunk = 1u * 1024 * 1024;
    std::vector<std::uint8_t> buf(kChunk);
    std::uint64_t remaining = length;
    const int fd = it->second.fd;
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(
            remaining < kChunk ? remaining : kChunk);
        const ssize_t got = ::pread(fd, buf.data(), want,
                                    static_cast<off_t>(device_offset));
        if (got <= 0) {
            return Result<std::string>::failure(
                makeError(proto_ns::ERROR_DEVICE_IO, "hash read failed", errno));
        }
        SHA256_Update(&ctx, buf.data(), static_cast<std::size_t>(got));
        device_offset += static_cast<std::uint64_t>(got);
        remaining -= static_cast<std::uint64_t>(got);
    }

    uint8_t digest[32] = {0};
    SHA256_Final(digest, &ctx);
    return Result<std::string>::success(
        std::string(reinterpret_cast<char*>(digest), sizeof(digest)));
}

Result<void> LinuxEmbeddedBackend::syncDevice(const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    if (::fsync(it->second.fd) != 0) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SYNC_FAILED, "fsync failed", errno));
    }
    return Result<void>::success();
}

Result<proto_ns::SessionStats> LinuxEmbeddedBackend::closeSession(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->client_bulk.release();
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<proto_ns::SessionStats>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    Session s = std::move(it->second);
    state_->sessions.erase(it);

    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - s.started).count();
    if (s.fd >= 0) {
        ::close(s.fd);
    }
    s.bulk.release();

    proto_ns::SessionStats stats;
    stats.set_bytes_written(s.bytes_written);
    stats.set_duration_ms(static_cast<std::uint64_t>(dur));
    stats.set_state(proto_ns::SESSION_STATE_SUCCEEDED);
    return Result<proto_ns::SessionStats>::success(std::move(stats));
}

Result<void> LinuxEmbeddedBackend::unmount(const std::string& device_path) {
    const auto r = linux_maint::unmountDisk(device_path);
    switch (r) {
        case linux_maint::Result::Success:
            return Result<void>::success();
        case linux_maint::Result::InvalidDrive:
            return Result<void>::failure(
                makeError(proto_ns::ERROR_DEVICE_NOT_FOUND, linux_maint::lastDetail()));
        case linux_maint::Result::AccessDenied:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_DEVICE_PERMISSION, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
        case linux_maint::Result::Busy:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_DEVICE_BUSY, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
        default:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_UNMOUNT_FAILED, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
    }
}

Result<void> LinuxEmbeddedBackend::eject(const std::string& device_path) {
    const auto r = linux_maint::ejectDisk(device_path);
    switch (r) {
        case linux_maint::Result::Success:
            return Result<void>::success();
        case linux_maint::Result::InvalidDrive:
            return Result<void>::failure(
                makeError(proto_ns::ERROR_DEVICE_NOT_FOUND, linux_maint::lastDetail()));
        case linux_maint::Result::AccessDenied:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_DEVICE_PERMISSION, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
        case linux_maint::Result::Busy:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_DEVICE_BUSY, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
        default:
            return Result<void>::failure(makeError(
                proto_ns::ERROR_EJECT_FAILED, linux_maint::lastDetail(),
                linux_maint::lastKernelErrno()));
    }
}

Result<void> LinuxEmbeddedBackend::mapBulkBuffer(const proto_ns::SessionId& sid,
                                                 std::size_t size_bytes,
                                                 std::uint32_t /*async_queue_depth*/) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (size_bytes == 0) {
        state_->client_bulk.release();
        return Result<void>::success();
    }
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    state_->client_bulk.release();
    if (!state_->client_bulk.createOwned(size_bytes)) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_UNKNOWN, "memfd_create/mmap failed", errno));
    }
    state_->bulk_session = sid;
    return Result<void>::success();
}

void* LinuxEmbeddedBackend::bulkBufferBase() const {
    return state_->client_bulk.base();
}

std::size_t LinuxEmbeddedBackend::bulkBufferSize() const {
    return state_->client_bulk.size();
}

Result<std::size_t> LinuxEmbeddedBackend::bulkWriteFromBuffer(
    const proto_ns::SessionId& sid, std::uint64_t buffer_offset,
    std::uint64_t device_offset, std::size_t length) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->client_bulk.valid() || length == 0
        || buffer_offset > state_->client_bulk.size()
        || length > state_->client_bulk.size() - buffer_offset) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_UNKNOWN, "bulkWrite: buffer not mapped or OOB"));
    }
    auto it = state_->sessions.find(sid.v());
    if (it == state_->sessions.end()) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_SESSION_NOT_FOUND, "no such session"));
    }
    if (it->second.bytes_written + length > kMaxSessionBytes) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_WRITE_FAILED, "session bytes-written cap reached"));
    }
    const auto* src = static_cast<const std::uint8_t*>(state_->client_bulk.base())
                        + buffer_offset;
    const ssize_t n = ::pwrite(it->second.fd, src, length,
                               static_cast<off_t>(device_offset));
    if (n < 0 || static_cast<std::size_t>(n) != length) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_WRITE_FAILED, "bulk pwrite failed", errno));
    }
    it->second.bytes_written += length;
    return Result<std::size_t>::success(length);
}

void LinuxEmbeddedBackend::submitBulkWriteAsync(const proto_ns::SessionId& sid,
                                                std::uint64_t buffer_offset,
                                                std::uint64_t device_offset,
                                                std::size_t length,
                                                BulkWriteCallback on_complete) {
    {
        std::lock_guard<std::mutex> lk(state_->queue_mutex);
        if (!state_->worker_started) {
            state_->worker_started = true;
            State* st = state_.get();
            LinuxEmbeddedBackend* self = this;
            state_->worker = std::thread([st, self]() {
                for (;;) {
                    State::AsyncJob job;
                    {
                        std::unique_lock<std::mutex> qlk(st->queue_mutex);
                        st->queue_cv.wait(qlk, [st] {
                            return st->worker_stop || !st->queue.empty();
                        });
                        if (st->worker_stop && st->queue.empty()) {
                            return;
                        }
                        job = std::move(st->queue.front());
                        st->queue.pop_front();
                    }
                    auto result = self->bulkWriteFromBuffer(
                        job.sid, job.buffer_offset, job.device_offset, job.length);
                    if (job.cb) {
                        job.cb(std::move(result));
                    }
                }
            });
        }
        state_->queue.push_back(State::AsyncJob{
            sid, buffer_offset, device_offset, length, std::move(on_complete)});
    }
    state_->queue_cv.notify_one();
}

} // namespace rpi_imager::privileged::backends
