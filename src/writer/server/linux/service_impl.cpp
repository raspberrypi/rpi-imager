// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "service_impl.h"
#include "peer_auth_linux.h"
#include "helper_log.h"
#include "drivelist_linux_helper.h"
#include "../session_telemetry.h"
#include "../drive_list_rpc.h"
#include "../wire_outbound.h"
#include "../drive_watch_service.h"
#include "../bulk_write_async.h"
#include "../../../drivelist/drivelist.h"
#include "../../../linux/helper_maintenance.h"

#include "wire/frame.h"
#include "wire/handshake.h"
#include "wire/protocol.h"
#include "wire/linux_shared_memory.h"
#include "wire/linux_socket_io.h"
#include "proto/imager.pb.h"

#include "linux/file_operations_linux.h"

#include <gnutls/crypto.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace proto = rpi_imager::privileged::proto;
namespace wire = rpi_imager::privileged::wire;

namespace rpi_imager::writer {

namespace {

constexpr std::uint32_t kProtocolVersion = wire::kProtocolVersion;
constexpr std::size_t kMaxSyncChunk = 8u * 1024 * 1024;
constexpr std::uint64_t kMaxSessionBytesWritten = 64ull * 1024 * 1024 * 1024;

std::uint64_t physicalMemoryBytes() {
    long pages = ::sysconf(_SC_PHYS_PAGES);
    long page_size = ::sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page_size <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page_size);
}

std::uint64_t maxBulkBufferBytes() {
    constexpr std::uint64_t kFloor = 256ull * 1024 * 1024;
    const std::uint64_t phys = physicalMemoryBytes();
    if (phys == 0) return kFloor;
    const std::uint64_t third = phys / 3;
    return third > kFloor ? third : kFloor;
}

proto::ErrorInfo mapMaintenanceError(linux_maint::Result r, const char* op) {
    switch (r) {
        case linux_maint::Result::Success:
            return ok();
        case linux_maint::Result::InvalidDrive:
            return fail(proto::ERROR_DEVICE_NOT_FOUND,
                        std::string(op) + ": invalid device path");
        case linux_maint::Result::AccessDenied:
            return fail(proto::ERROR_DEVICE_PERMISSION,
                        std::string(op) + ": access denied");
        case linux_maint::Result::Busy:
            return fail(proto::ERROR_DEVICE_BUSY, std::string(op) + ": device busy");
        default:
            return fail(proto::ERROR_UNMOUNT_FAILED,
                        linux_maint::lastDetail().empty()
                            ? std::string(op) + " failed"
                            : linux_maint::lastDetail());
    }
}

proto::ErrorInfo ok() { return proto::ErrorInfo(); }

proto::ErrorInfo fail(proto::ErrorCode code, const std::string& detail) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    return e;
}

proto::ErrorCode mapFileError(FileError e) {
    switch (e) {
        case FileError::kSuccess: return proto::ERROR_NONE;
        case FileError::kOpenError: return proto::ERROR_DEVICE_NOT_FOUND;
        case FileError::kWriteError: return proto::ERROR_WRITE_FAILED;
        case FileError::kReadError: return proto::ERROR_DEVICE_IO;
        case FileError::kSeekError: return proto::ERROR_DEVICE_IO;
        case FileError::kSizeError: return proto::ERROR_DEVICE_IO;
        case FileError::kCloseError: return proto::ERROR_DEVICE_IO;
        case FileError::kLockError: return proto::ERROR_DEVICE_BUSY;
        case FileError::kSyncError: return proto::ERROR_SYNC_FAILED;
        case FileError::kFlushError: return proto::ERROR_SYNC_FAILED;
        case FileError::kCancelled: return proto::ERROR_CANCELLED;
        case FileError::kTimeout: return proto::ERROR_DEVICE_IO;
        default: return proto::ERROR_UNKNOWN;
    }
}

bool isBlockDevicePath(const std::string& p) {
    return p.size() > 5 && p.compare(0, 5, "/dev/") == 0;
}

class Sha256 {
public:
    bool init() {
        return gnutls_hash_init(&hd_, GNUTLS_DIG_SHA256) == 0;
    }
    bool update(const void* data, std::size_t len) {
        return gnutls_hash(hd_, data, len) == 0;
    }
    bool finish(std::string& out32) {
        unsigned char digest[32] = {0};
        if (gnutls_hash_output(hd_, digest) != 0) {
            return false;
        }
        out32.assign(reinterpret_cast<char*>(digest), sizeof(digest));
        return true;
    }
    ~Sha256() {
        if (hd_) {
            gnutls_hash_deinit(hd_, nullptr);
        }
    }

private:
    gnutls_hash_hd_t hd_ = nullptr;
};

struct Session {
    std::unique_ptr<rpi_imager::LinuxFileOperations> fops;
    wire::LinuxSharedMemory bulk;
    std::atomic<std::uint64_t> bytes_written{0};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    SessionTelemetry telemetry;
    SessionBulkWriter bulk_writer;
};

class Helper {
public:
    void setPushContext(WireOutbound* outbound, DriveWatchService* drive_watch) {
        outbound_ = outbound;
        drive_watch_ = drive_watch;
    }

    proto::ErrorInfo dispatch(const proto::WireRequest& req, std::string& out_payload,
                              bool& response_deferred);
    int pending_fd() const { return pending_fd_; }
    void clearPendingFd() { pending_fd_ = -1; }

private:
    Session* find(const proto::SessionId& sid) {
        auto it = sessions_.find(sid.v());
        return it == sessions_.end() ? nullptr : &it->second;
    }

    void sendDriveChangedEvent();
    proto::ErrorInfo handleSubscribeDrives();
    proto::ErrorInfo handleUnsubscribeDrives();

    proto::ErrorInfo handleOpenSession(const std::string& payload, std::string& out);
    proto::ErrorInfo handleQueryDeviceLimits(const std::string& payload, std::string& out);
    proto::ErrorInfo handlePrepareDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleMapBulkBuffer(const std::string& payload, std::string& out);
    proto::ErrorInfo handleBulkWrite(std::uint64_t request_id, const std::string& payload,
                                     std::string& out, bool& response_deferred);
    proto::ErrorInfo handleWriteChunk(const std::string& payload, std::string& out);
    proto::ErrorInfo handleReadChunk(const std::string& payload, std::string& out);
    proto::ErrorInfo handleSyncDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleHashDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleCloseSession(const std::string& payload, std::string& out);
    proto::ErrorInfo handleListDrives(std::string& out);
    proto::ErrorInfo handleUnmount(const std::string& payload);
    proto::ErrorInfo handleEject(const std::string& payload);

    std::unordered_map<std::uint64_t, Session> sessions_;
    std::uint64_t next_id_ = 1;
    int pending_fd_ = -1;

    WireOutbound* outbound_ = nullptr;
    DriveWatchService* drive_watch_ = nullptr;
    bool drive_subscribed_ = false;
};

void Helper::sendDriveChangedEvent() {
    if (!outbound_) {
        return;
    }
    proto::WireEvent ev;
    ev.set_kind(proto::WIRE_EVENT_DRIVE_CHANGED);
    ev.mutable_change()->set_kind(proto::DriveChange::KIND_UPDATED);
    (void)outbound_->sendEvent(ev);
}

proto::ErrorInfo Helper::handleSubscribeDrives() {
    if (drive_subscribed_) {
        return ok();
    }
    if (!drive_watch_ || !outbound_) {
        return fail(proto::ERROR_UNKNOWN, "push context not configured");
    }
    drive_subscribed_ = true;
    drive_watch_->subscribe([this] { sendDriveChangedEvent(); });
    helperLog("subscribeDrives ok");
    return ok();
}

proto::ErrorInfo Helper::handleUnsubscribeDrives() {
    if (!drive_subscribed_) {
        return ok();
    }
    drive_subscribed_ = false;
    if (drive_watch_) {
        drive_watch_->unsubscribe();
    }
    helperLog("unsubscribeDrives ok");
    return ok();
}

proto::ErrorInfo Helper::handleOpenSession(const std::string& payload, std::string& out) {
    proto::OpenSessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad OpenSessionRequest");
    if (!isBlockDevicePath(req.device_path())) {
        return fail(proto::ERROR_DEVICE_PERMISSION, "helper only opens /dev/* paths");
    }

    Session s;
    s.fops = std::make_unique<rpi_imager::LinuxFileOperations>();
    const FileError e = s.fops->OpenDevice(req.device_path());
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "OpenDevice failed for " + req.device_path());
    }

    const std::uint64_t id = next_id_++;
    auto [it, inserted] = sessions_.emplace(id, std::move(s));
    (void)inserted;
    it->second.bulk_writer.start(it->second.fops.get(), outbound_);

    proto::SessionId sid;
    sid.set_v(id);
    sid.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleQueryDeviceLimits(const std::string& payload, std::string& out) {
    proto::SessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad SessionRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "queryDeviceLimits: no session");

    std::uint64_t size = 0;
    if (s->fops->GetSize(size) != FileError::kSuccess) {
        return fail(proto::ERROR_DEVICE_IO, "GetSize failed");
    }
    const auto& lim = s->fops->GetDeviceIOLimits();

    proto::DeviceLimits out_limits;
    out_limits.set_total_bytes(size);
    out_limits.set_max_transfer_bytes(static_cast<std::uint32_t>(lim.max_transfer_bytes));
    out_limits.set_logical_block_size(512);
    if (lim.suggested_queue_depth > 0) {
        out_limits.set_suggested_queue_depth(
            static_cast<std::uint32_t>(lim.suggested_queue_depth));
    }
    out_limits.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handlePrepareDevice(const std::string& payload, std::string&) {
    proto::PrepareDeviceRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad PrepareDeviceRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "prepareDevice: no session");

    std::uint64_t size = 0;
    if (s->fops->GetSize(size) != FileError::kSuccess) {
        return fail(proto::ERROR_DEVICE_IO, "prepareDevice: GetSize failed");
    }
    const auto t0 = std::chrono::steady_clock::now();
    const FileError e = s->fops->PrepareDevice(size, req.options().zero_last_mb());
    const auto t1 = std::chrono::steady_clock::now();
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "PrepareDevice failed");
    }
    s->telemetry.recordPrepareDevice(t1 - t0);
    return ok();
}

proto::ErrorInfo Helper::handleMapBulkBuffer(const std::string& payload, std::string& out) {
    proto::MapBulkBufferRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad MapBulkBufferRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "mapBulkBuffer: no session");

    if (req.size_bytes() == 0) {
        s->bulk.release();
        return ok();
    }

    const std::uint64_t want = req.size_bytes();
    if (want > maxBulkBufferBytes()) {
        return fail(proto::ERROR_UNKNOWN, "mapBulkBuffer: size exceeds RAM-proportional cap");
    }
    if (s->bytes_written.load() >= kMaxSessionBytesWritten) {
        return fail(proto::ERROR_WRITE_FAILED, "mapBulkBuffer: session cap exhausted");
    }

    const auto& io_lim = s->fops->GetDeviceIOLimits();
    int async_depth = static_cast<int>(req.async_queue_depth());
    if (async_depth <= 0) {
        async_depth = kDefaultBulkInFlightCap;
    }
    async_depth = io_lim.capAsyncQueueDepth(async_depth);
    if (async_depth > 1) {
        s->bulk_writer.configureAsyncQueueDepth(async_depth);
    }

    s->bulk.release();
    if (!s->bulk.createOwned(static_cast<std::size_t>(want))) {
        return fail(proto::ERROR_UNKNOWN, "mapBulkBuffer: memfd_create failed");
    }

    pending_fd_ = s->bulk.ownedFd();

    proto::MapBulkBufferReply reply;
    reply.set_section_handle(static_cast<std::uint64_t>(pending_fd_));
    reply.set_size_bytes(want);
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleBulkWrite(const std::uint64_t request_id,
                                         const std::string& payload,
                                         std::string& out,
                                         bool& response_deferred) {
    response_deferred = false;
    proto::WriteRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad WriteRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "bulkWrite: no session");
    if (!s->bulk.valid()) {
        return fail(proto::ERROR_INVALID_SLOT, "bulkWrite: no bulk buffer mapped");
    }

    const std::uint64_t buf_off = req.buffer_offset();
    const std::uint64_t len = req.length();
    const std::uint64_t shm_size = static_cast<std::uint64_t>(s->bulk.size());
    if (len == 0 || buf_off > shm_size || len > shm_size - buf_off) {
        return fail(proto::ERROR_WRITE_FAILED, "bulkWrite: bounds check failed");
    }

    const auto* src = static_cast<const std::uint8_t*>(s->bulk.base()) + buf_off;
    proto::ErrorInfo sync_err;
    if (s->bulk_writer.submit(request_id, req, src, kMaxSessionBytesWritten,
                              &s->bytes_written, &s->telemetry, sync_err)) {
        response_deferred = true;
        (void)out;
        return ok();
    }
    return sync_err;
}

proto::ErrorInfo Helper::handleWriteChunk(const std::string& payload, std::string& out) {
    proto::WriteChunkRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad WriteChunkRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "writeChunk: no session");
    if (req.data().size() > kMaxSyncChunk) {
        return fail(proto::ERROR_WRITE_FAILED, "writeChunk: chunk exceeds sync cap");
    }

    const auto t0 = std::chrono::steady_clock::now();
    const FileError e = s->fops->WriteAtOffset(
        req.offset(),
        reinterpret_cast<const std::uint8_t*>(req.data().data()),
        req.data().size());
    const auto t1 = std::chrono::steady_clock::now();
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "WriteAtOffset failed");
    }
    s->bytes_written.fetch_add(req.data().size());
    const auto lat_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    s->telemetry.recordWriteLatency(static_cast<std::uint64_t>(lat_us));

    proto::WriteChunkReply reply;
    reply.set_bytes_written(req.data().size());
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleReadChunk(const std::string& payload, std::string& out) {
    proto::ReadRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad ReadRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "readChunk: no session");
    if (req.length() > kMaxSyncChunk) {
        return fail(proto::ERROR_DEVICE_IO, "readChunk: length exceeds sync cap");
    }

    if (s->fops->Seek(req.offset()) != FileError::kSuccess) {
        return fail(proto::ERROR_DEVICE_IO, "readChunk: seek failed");
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(req.length()));
    std::size_t got = 0;
    const FileError e = s->fops->ReadSequential(buf.data(), buf.size(), got);
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "ReadSequential failed");
    }

    proto::ReadReply reply;
    reply.set_data(reinterpret_cast<const char*>(buf.data()), got);
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleSyncDevice(const std::string& payload, std::string&) {
    proto::SessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad SessionRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "syncDevice: no session");
    s->bulk_writer.drainBeforeSync(s->fops.get());
    const auto t0 = std::chrono::steady_clock::now();
    const FileError e = s->fops->ForceSync();
    const auto t1 = std::chrono::steady_clock::now();
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "ForceSync failed");
    }
    s->telemetry.recordFsync(t1 - t0);
    return ok();
}

proto::ErrorInfo Helper::handleHashDevice(const std::string& payload, std::string& out) {
    proto::HashDeviceRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad HashDeviceRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "hashDeviceSha256: no session");

    s->bulk_writer.drainBeforeSync(s->fops.get());

    const auto t0 = std::chrono::steady_clock::now();

    Sha256 sha;
    if (!sha.init()) return fail(proto::ERROR_UNKNOWN, "SHA-256 init failed");
    if (!req.prefix().empty() && !sha.update(req.prefix().data(), req.prefix().size())) {
        return fail(proto::ERROR_UNKNOWN, "hash update (prefix) failed");
    }

    if (s->fops->Seek(req.device_offset()) != FileError::kSuccess) {
        return fail(proto::ERROR_DEVICE_IO, "hashDeviceSha256: seek failed");
    }

    constexpr std::size_t kChunk = 1u * 1024 * 1024;
    std::vector<std::uint8_t> buf(kChunk);
    std::uint64_t remaining = req.length();
    while (remaining > 0) {
        const std::size_t want = static_cast<std::size_t>(
            remaining < kChunk ? remaining : kChunk);
        std::size_t got = 0;
        if (s->fops->ReadSequential(buf.data(), want, got) != FileError::kSuccess || got == 0) {
            return fail(proto::ERROR_DEVICE_IO, "hashDeviceSha256: read failed");
        }
        if (!sha.update(buf.data(), got)) {
            return fail(proto::ERROR_UNKNOWN, "hash update (device) failed");
        }
        remaining -= got;
    }

    std::string digest;
    if (!sha.finish(digest)) return fail(proto::ERROR_UNKNOWN, "hash finalize failed");

    const auto t1 = std::chrono::steady_clock::now();
    s->telemetry.recordHashDevice(t1 - t0);

    proto::HashDeviceReply reply;
    reply.set_digest(digest);
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleListDrives(std::string& out) {
    const std::vector<Drivelist::DeviceDescriptor> devices =
        linux_drivelist::listStorageDevices();
    const proto::DriveList list = buildDriveList(devices);
    list.SerializeToString(&out);
    helperLog("listDrives count=" + std::to_string(list.drives_size()));
    return ok();
}

proto::ErrorInfo Helper::handleUnmount(const std::string& payload) {
    proto::PathRequest req;
    if (!req.ParseFromString(payload)) {
        return fail(proto::ERROR_UNKNOWN, "bad PathRequest");
    }
    const auto r = linux_maint::unmountDisk(req.device_path());
    if (r == linux_maint::Result::Success) {
        helperLog("unmount ok path=" + req.device_path());
        return ok();
    }
    proto::ErrorInfo err = mapMaintenanceError(r, "unmount");
    err.set_kernel_errno(linux_maint::lastKernelErrno());
    return err;
}

proto::ErrorInfo Helper::handleEject(const std::string& payload) {
    proto::PathRequest req;
    if (!req.ParseFromString(payload)) {
        return fail(proto::ERROR_UNKNOWN, "bad PathRequest");
    }
    const auto r = linux_maint::ejectDisk(req.device_path());
    if (r == linux_maint::Result::Success) {
        return ok();
    }
    proto::ErrorInfo err;
    switch (r) {
        case linux_maint::Result::InvalidDrive:
            err = fail(proto::ERROR_DEVICE_NOT_FOUND, "eject: invalid device path");
            break;
        case linux_maint::Result::Busy:
            err = fail(proto::ERROR_DEVICE_BUSY, "eject: device busy");
            break;
        default:
            err = fail(proto::ERROR_EJECT_FAILED,
                       linux_maint::lastDetail().empty() ? "eject failed"
                                                         : linux_maint::lastDetail());
            break;
    }
    err.set_kernel_errno(linux_maint::lastKernelErrno());
    return err;
}

proto::ErrorInfo Helper::handleCloseSession(const std::string& payload, std::string& out) {
    proto::SessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad SessionRequest");
    auto it = sessions_.find(req.session_id().v());
    if (it == sessions_.end()) return fail(proto::ERROR_SESSION_NOT_FOUND, "closeSession: no session");

    Session& s = it->second;
    s.bulk_writer.shutdown();
    (void)s.fops->WaitForPendingWrites();
    s.bulk.release();
    const FileError e = s.fops->Close();
    const bool success = (e == FileError::kSuccess);
    proto::ErrorInfo terminal;
    if (!success) {
        terminal = fail(mapFileError(e), "Close failed");
    }

    proto::SessionStats stats;
    s.telemetry.fillSessionStats(stats, s.bytes_written.load(), s.started, success,
                                 success ? nullptr : &terminal);
    stats.SerializeToString(&out);

    sessions_.erase(it);
    return ok();
}

proto::ErrorInfo Helper::dispatch(const proto::WireRequest& req, std::string& out_payload,
                                  bool& response_deferred) {
    response_deferred = false;
    pending_fd_ = -1;
    switch (req.method()) {
        case proto::WIRE_HELLO: {
            const auto ver_err = wire::checkProtocolVersion(req.payload(), kProtocolVersion);
            if (ver_err.code() != proto::ERROR_NONE) {
                return ver_err;
            }
            [[fallthrough]];
        }
        case proto::WIRE_QUERY_HELPER_STATUS: {
            proto::HelperStatus status;
            wire::fillReadyHelperStatus(status, kProtocolVersion);
            if (req.method() == proto::WIRE_HELLO) {
                proto::ProtocolVersion pv;
                if (pv.ParseFromString(req.payload())) {
                    wire::applyClientProtocolVersion(status, pv.version());
                }
            }
            status.SerializeToString(&out_payload);
            return ok();
        }
        case proto::WIRE_OPEN_SESSION: return handleOpenSession(req.payload(), out_payload);
        case proto::WIRE_QUERY_DEVICE_LIMITS: return handleQueryDeviceLimits(req.payload(), out_payload);
        case proto::WIRE_PREPARE_DEVICE: return handlePrepareDevice(req.payload(), out_payload);
        case proto::WIRE_WRITE_CHUNK: return handleWriteChunk(req.payload(), out_payload);
        case proto::WIRE_READ_CHUNK: return handleReadChunk(req.payload(), out_payload);
        case proto::WIRE_SYNC_DEVICE: return handleSyncDevice(req.payload(), out_payload);
        case proto::WIRE_HASH_DEVICE_SHA256: return handleHashDevice(req.payload(), out_payload);
        case proto::WIRE_CLOSE_SESSION: return handleCloseSession(req.payload(), out_payload);
        case proto::WIRE_MAP_BULK_BUFFER: return handleMapBulkBuffer(req.payload(), out_payload);
        case proto::WIRE_BULK_WRITE:
            return handleBulkWrite(req.request_id(), req.payload(), out_payload,
                                   response_deferred);
        case proto::WIRE_LIST_DRIVES: return handleListDrives(out_payload);
        case proto::WIRE_SUBSCRIBE_DRIVES: return handleSubscribeDrives();
        case proto::WIRE_UNSUBSCRIBE_DRIVES: return handleUnsubscribeDrives();
        case proto::WIRE_UNMOUNT: return handleUnmount(req.payload());
        case proto::WIRE_EJECT: return handleEject(req.payload());
        default:
            return fail(proto::ERROR_NOT_IMPLEMENTED, "unknown method");
    }
}

std::string socketPathFromArgs(int argc, char** argv) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--socket") {
            return argv[i + 1];
        }
    }
    return {};
}

pid_t peerPid(int client_fd) {
    struct ucred cred {};
    socklen_t len = sizeof(cred);
    if (::getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) {
        return -1;
    }
    return cred.pid;
}

void serviceLoop(int client_fd) {
    const pid_t client_pid = peerPid(client_fd);
    if (!verifyConnectingClient(client_pid)) {
        helperLog("rejected unauthenticated client");
        return;
    }
    helperLog("client authenticated");

    WireOutbound outbound(client_fd);
    DriveWatchService drive_watch;
    Helper helper;
    helper.setPushContext(&outbound, &drive_watch);

    wire::FrameAccumulator acc;
    char buf[8192];
    std::string payload;

    for (;;) {
        bool oversize = false;
        if (!acc.next(payload, oversize)) {
            if (oversize) {
                break;
            }
            const ssize_t got = ::read(client_fd, buf, sizeof(buf));
            if (got <= 0) {
                break;
            }
            acc.append(buf, static_cast<std::size_t>(got));
            continue;
        }

        proto::WireRequest req;
        if (!req.ParseFromString(payload)) {
            break;
        }

        proto::WireResponse resp;
        resp.set_request_id(req.request_id());
        std::string reply_payload;
        bool response_deferred = false;
        const proto::ErrorInfo err = helper.dispatch(req, reply_payload, response_deferred);
        if (response_deferred) {
            continue;
        }
        *resp.mutable_error() = err;
        if (err.code() == proto::ERROR_NONE) {
            resp.set_payload(reply_payload);
        }

        const int pass_fd = (req.method() == proto::WIRE_MAP_BULK_BUFFER
                             && err.code() == proto::ERROR_NONE)
                                ? helper.pending_fd()
                                : -1;
        if (!outbound.sendResponse(resp, pass_fd)) {
            break;
        }
        helper.clearPendingFd();
    }

    drive_watch.unsubscribe();
}

} // namespace

int RpiImagerWriterServiceMainLinux(int argc, char** argv) {
    const std::string socket_path = socketPathFromArgs(argc, argv);
    if (socket_path.empty()) {
        return 2;
    }

    ::unlink(socket_path.c_str());

    const int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return 1;
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        ::close(listen_fd);
        return 1;
    }
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0
        || ::listen(listen_fd, 1) != 0) {
        ::close(listen_fd);
        return 1;
    }

    const int client_fd = ::accept(listen_fd, nullptr, nullptr);
    ::close(listen_fd);
    if (client_fd < 0) {
        return 1;
    }

    serviceLoop(client_fd);
    ::close(client_fd);
    ::unlink(socket_path.c_str());
    return 0;
}

} // namespace rpi_imager::writer
