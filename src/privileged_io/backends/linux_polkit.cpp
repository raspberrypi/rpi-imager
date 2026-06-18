// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "linux_polkit.h"

#include "../wire/handshake.h"
#include "../wire/linux_shared_memory.h"
#include "../wire/linux_socket_io.h"
#include "../wire/drive_descriptor_json.h"
#include "../wire/duplex_connection.h"
#include "../wire/protocol.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <limits.h>
#include <mutex>
#include <random>
#include <spawn.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace rpi_imager::privileged::backends {

namespace {

constexpr std::uint32_t kProtocolVersion = wire::kProtocolVersion;

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

std::string clientExePath() {
    char buf[PATH_MAX] = {0};
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return {};
    }
    return std::string(buf, static_cast<std::size_t>(n));
}

std::string clientExeDir() {
    const std::string path = clientExePath();
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::string resolveHelperPath(const std::string& configured) {
    if (!configured.empty()) {
        return configured;
    }
    const std::string beside = clientExeDir() + "rpi-imager-writer";
    if (::access(beside.c_str(), X_OK) == 0) {
        return beside;
    }
    if (::access("/usr/bin/rpi-imager-writer", X_OK) == 0) {
        return "/usr/bin/rpi-imager-writer";
    }
    return beside;
}

std::string makeSocketPath() {
    std::random_device rd;
    std::ostringstream ss;
    ss << "/tmp/rpi-imager-writer-" << std::hex << rd() << rd();
    return ss.str();
}

void reapHelperChild(pid_t& pid) {
    if (pid <= 0) {
        return;
    }
    int status = 0;
    const pid_t w = ::waitpid(pid, &status, WNOHANG);
    if (w == 0) {
        ::kill(pid, SIGTERM);
        ::waitpid(pid, &status, 0);
    }
    pid = -1;
}

proto_ns::ErrorInfo elevationFailureFromStatus(int status) {
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code == 127) {
            return makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                             "pkexec could not execute privileged helper");
        }
        if (code == 126) {
            return makeError(proto_ns::ERROR_HELPER_INSTALL_REJECTED,
                             "pkexec permission denied");
        }
        return makeError(proto_ns::ERROR_CANCELLED,
                         "privilege elevation failed (exit " + std::to_string(code) + ")");
    }
    return makeError(proto_ns::ERROR_CANCELLED,
                     "privileged helper exited before connecting");
}

} // namespace

struct LinuxPolkitBackend::State {
    Options options;

    std::mutex mutex;
    int sock = -1;
    pid_t helper_pid = -1;
    std::string socket_path;
    bool connected = false;
    wire::DuplexConnection duplex;

    wire::LinuxSharedMemory bulk;
    proto_ns::SessionId bulk_session;

    struct AsyncJob {
        proto_ns::SessionId sid;
        std::uint64_t buffer_offset;
        std::uint64_t device_offset;
        std::size_t length;
        BulkWriteCallback cb;
    };
    std::thread worker;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<AsyncJob> queue;
    bool worker_stop = false;
    bool worker_started = false;

    ~State() {
        duplex.detach();
        {
            std::lock_guard<std::mutex> lk(queue_mutex);
            worker_stop = true;
        }
        queue_cv.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
        if (sock >= 0) {
            ::close(sock);
        }
        if (!socket_path.empty()) {
            ::unlink(socket_path.c_str());
        }
    }
};

LinuxPolkitBackend::LinuxPolkitBackend() : LinuxPolkitBackend(Options{}) {}

LinuxPolkitBackend::LinuxPolkitBackend(Options opts)
    : state_(std::make_unique<State>()) {
    state_->options = std::move(opts);
}

LinuxPolkitBackend::~LinuxPolkitBackend() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->duplex.detach();
    if (state_->sock >= 0) {
        ::close(state_->sock);
        state_->sock = -1;
        state_->connected = false;
    }
    reapHelperChild(state_->helper_pid);
    if (!state_->socket_path.empty()) {
        ::unlink(state_->socket_path.c_str());
        state_->socket_path.clear();
    }
}

namespace {

std::string clientAppImagePath() {
    const char* appimage = ::getenv("APPIMAGE");
    if (appimage && appimage[0] != '\0') {
        return appimage;
    }
    return {};
}

bool ensureConnectedImpl(LinuxPolkitBackend::State* st, proto_ns::ErrorInfo& err) {
    if (st->connected && st->sock >= 0 && st->duplex.isAttached()) {
        return true;
    }

    const std::string appimage = clientAppImagePath();
    const std::string helper = appimage.empty() ? resolveHelperPath(st->options.helper_exe_path)
                                                : appimage;
    if (!appimage.empty()) {
        if (::access(helper.c_str(), X_OK) != 0) {
            err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                            "AppImage not executable: " + helper, errno);
            return false;
        }
    } else if (::access(helper.c_str(), X_OK) != 0) {
        err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                        "rpi-imager-writer not found or not executable: " + helper,
                        errno);
        return false;
    }

    st->socket_path = makeSocketPath();
    ::unlink(st->socket_path.c_str());

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attrs;
    if (posix_spawn_file_actions_init(&actions) != 0 ||
        posix_spawnattr_init(&attrs) != 0) {
        err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                        "posix_spawn init failed", errno);
        return false;
    }

    const char* argv_appimage[] = {
        "/usr/bin/pkexec", "--disable-internal-agent",
        helper.c_str(), "--privileged-helper", "--socket", st->socket_path.c_str(), nullptr,
    };
    const char* argv_writer[] = {
        "/usr/bin/pkexec", "--disable-internal-agent",
        helper.c_str(), "--socket", st->socket_path.c_str(), nullptr,
    };
    const char* const* argv = appimage.empty() ? argv_writer : argv_appimage;
    pid_t child = -1;
    const int spawn_rc = ::posix_spawn(&child, "/usr/bin/pkexec", &actions, &attrs,
                                       const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attrs);
    if (spawn_rc != 0) {
        err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                        "posix_spawn(pkexec) failed", spawn_rc);
        return false;
    }
    st->helper_pid = child;

    const int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                        "socket() failed", errno);
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(
            st->options.handshake_timeout_ms ? st->options.handshake_timeout_ms : 30000);

    bool connected = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (st->helper_pid > 0) {
            int status = 0;
            const pid_t w = ::waitpid(st->helper_pid, &status, WNOHANG);
            if (w == st->helper_pid) {
                reapHelperChild(st->helper_pid);
                ::close(sock);
                err = elevationFailureFromStatus(status);
                return false;
            }
        }

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        if (st->socket_path.size() >= sizeof(addr.sun_path)) {
            break;
        }
        std::strncpy(addr.sun_path, st->socket_path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (!connected) {
        ::close(sock);
        reapHelperChild(st->helper_pid);
        err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                        "timed out connecting to privileged helper socket");
        return false;
    }

    st->sock = sock;
    st->connected = true;
    if (!st->duplex.isAttached()) {
        st->duplex.attach(sock);
    }
    return true;
}

struct RpcResult {
    bool ok = false;
    proto_ns::ErrorInfo err;
    std::string payload;
    int ancillary_fd = -1;
};

RpcResult callRpcLocked(LinuxPolkitBackend::State* st,
                        proto_ns::WireMethod method,
                        const std::string& request_payload,
                        bool expect_ancillary_fd = false) {
    RpcResult out;
    if (!ensureConnectedImpl(st, out.err)) {
        return out;
    }

    const wire::RpcResult rpc =
        st->duplex.call(method, request_payload, expect_ancillary_fd);
    if (!rpc.ok) {
        out.err = rpc.error;
        if (out.err.code() == proto_ns::ERROR_DEVICE_DISCONNECTED
            || out.err.code() == proto_ns::ERROR_DEVICE_IO) {
            st->connected = false;
        }
        return out;
    }
    out.payload = rpc.payload;
    out.ancillary_fd = rpc.ancillary_fd;
    out.ok = true;
    return out;
}

} // namespace

Result<proto_ns::HelperStatus> LinuxPolkitBackend::queryHelperStatus() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ProtocolVersion pv;
    pv.set_version(kProtocolVersion);
    std::string pv_ser;
    pv.SerializeToString(&pv_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_HELLO, pv_ser);
    if (!rpc.ok) {
        proto_ns::HelperStatus s;
        if (rpc.err.code() == proto_ns::ERROR_HELPER_VERSION_MISMATCH) {
            s.set_state(proto_ns::HELPER_STATE_VERSION_MISMATCH);
        } else {
            s.set_state(proto_ns::HELPER_STATE_NOT_INSTALLED);
        }
        s.set_client_version(std::to_string(kProtocolVersion));
        return Result<proto_ns::HelperStatus>::success(std::move(s));
    }
    proto_ns::HelperStatus s;
    s.ParseFromString(rpc.payload);
    wire::applyClientProtocolVersion(s, kProtocolVersion);
    return Result<proto_ns::HelperStatus>::success(std::move(s));
}

Result<void> LinuxPolkitBackend::installHelper() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    if (!ensureConnectedImpl(state_.get(), err)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<void> LinuxPolkitBackend::uninstallHelper() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->duplex.detach();
    if (state_->sock >= 0) {
        ::close(state_->sock);
        state_->sock = -1;
        state_->connected = false;
    }
    return Result<void>::success();
}

std::string LinuxPolkitBackend::backendDescription() const {
    return "LinuxPolkitBackend (pkexec rpi-imager-writer over Unix socket)";
}

Result<std::vector<proto_ns::DriveInfo>> LinuxPolkitBackend::listDrivesNow() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_LIST_DRIVES, {});
    if (!rpc.ok) {
        return Result<std::vector<proto_ns::DriveInfo>>::failure(std::move(rpc.err));
    }
    proto_ns::DriveList list;
    list.ParseFromString(rpc.payload);
    std::vector<proto_ns::DriveInfo> out(list.drives().begin(), list.drives().end());
    return Result<std::vector<proto_ns::DriveInfo>>::success(std::move(out));
}

Result<void> LinuxPolkitBackend::subscribeDrives(DriveChangeCallback cb) {
    if (!cb) {
        return Result<void>::failure(
            makeError(proto_ns::ERROR_UNKNOWN, "null drive-change callback"));
    }
    state_->duplex.setEventCallback(
        [cb = std::move(cb)](const proto_ns::WireEvent& ev) {
            if (ev.kind() != proto_ns::WIRE_EVENT_DRIVE_CHANGED) {
                return;
            }
            proto_ns::DriveChange dc = ev.change();
            if (dc.kind() == proto_ns::DriveChange::KIND_UNKNOWN) {
                dc.set_kind(proto_ns::DriveChange::KIND_UPDATED);
            }
            cb(dc);
        });

    std::lock_guard<std::mutex> lk(state_->mutex);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_SUBSCRIBE_DRIVES, {});
    if (!rpc.ok) {
        state_->duplex.setEventCallback(nullptr);
        return Result<void>::failure(std::move(rpc.err));
    }
    return Result<void>::success();
}

Result<void> LinuxPolkitBackend::unsubscribeDrives() {
    state_->duplex.setEventCallback(nullptr);
    std::lock_guard<std::mutex> lk(state_->mutex);
    (void)callRpcLocked(state_.get(), proto_ns::WIRE_UNSUBSCRIBE_DRIVES, {});
    return Result<void>::success();
}

Result<std::vector<Drivelist::DeviceDescriptor>>
LinuxPolkitBackend::listDevicesNow() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_LIST_DRIVES, {});
    if (!rpc.ok) {
        return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(
            std::move(rpc.err));
    }
    proto_ns::DriveList list;
    list.ParseFromString(rpc.payload);
    return Result<std::vector<Drivelist::DeviceDescriptor>>::success(
        wire::deviceDescriptorsFromDriveList(list));
}

Result<proto_ns::SessionId> LinuxPolkitBackend::openSession(
    const std::string& device_path, const proto_ns::OpenOptions& opts) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::OpenSessionRequest req;
    req.set_device_path(device_path);
    *req.mutable_options() = opts;
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_OPEN_SESSION, req_ser);
    if (!rpc.ok) {
        return Result<proto_ns::SessionId>::failure(std::move(rpc.err));
    }
    proto_ns::SessionId sid;
    sid.ParseFromString(rpc.payload);
    return Result<proto_ns::SessionId>::success(std::move(sid));
}

Result<proto_ns::DeviceLimits> LinuxPolkitBackend::queryDeviceLimits(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_QUERY_DEVICE_LIMITS, req_ser);
    if (!rpc.ok) {
        return Result<proto_ns::DeviceLimits>::failure(std::move(rpc.err));
    }
    proto_ns::DeviceLimits limits;
    limits.ParseFromString(rpc.payload);
    return Result<proto_ns::DeviceLimits>::success(std::move(limits));
}

Result<void> LinuxPolkitBackend::prepareDevice(
    const proto_ns::SessionId& sid, const proto_ns::PrepareOptions& opts) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PrepareDeviceRequest req;
    *req.mutable_session_id() = sid;
    *req.mutable_options() = opts;
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_PREPARE_DEVICE, req_ser);
    if (!rpc.ok) {
        return Result<void>::failure(std::move(rpc.err));
    }
    return Result<void>::success();
}

Result<Slot> LinuxPolkitBackend::acquireSlot(const proto_ns::SessionId&) {
    return Result<Slot>::failure(
        makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                  "acquireSlot: use mapBulkBuffer/bulkWriteFromBuffer"));
}

void LinuxPolkitBackend::submitWrite(const proto_ns::SessionId&,
                                     std::uint32_t, std::uint64_t, std::uint64_t,
                                     WriteCompleteCallback on_complete) {
    if (on_complete) {
        proto_ns::WriteResult wr;
        *wr.mutable_error() = makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                                        "submitWrite: use submitBulkWriteAsync");
        on_complete(wr);
    }
}

Result<std::size_t> LinuxPolkitBackend::readChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    std::byte* out, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ReadRequest req;
    *req.mutable_session_id() = sid;
    req.set_offset(offset);
    req.set_length(len);
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_READ_CHUNK, req_ser);
    if (!rpc.ok) {
        return Result<std::size_t>::failure(std::move(rpc.err));
    }
    proto_ns::ReadReply rr;
    rr.ParseFromString(rpc.payload);
    const std::size_t n = std::min<std::size_t>(len, rr.data().size());
    std::memcpy(out, rr.data().data(), n);
    return Result<std::size_t>::success(n);
}

Result<std::size_t> LinuxPolkitBackend::writeChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    const std::byte* in, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::WriteChunkRequest req;
    *req.mutable_session_id() = sid;
    req.set_offset(offset);
    req.set_data(std::string(reinterpret_cast<const char*>(in), len));
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_WRITE_CHUNK, req_ser);
    if (!rpc.ok) {
        return Result<std::size_t>::failure(std::move(rpc.err));
    }
    proto_ns::WriteChunkReply wr;
    wr.ParseFromString(rpc.payload);
    return Result<std::size_t>::success(static_cast<std::size_t>(wr.bytes_written()));
}

Result<void> LinuxPolkitBackend::syncDevice(const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_SYNC_DEVICE, req_ser);
    if (!rpc.ok) {
        return Result<void>::failure(std::move(rpc.err));
    }
    return Result<void>::success();
}

Result<std::string> LinuxPolkitBackend::hashDeviceSha256(
    const proto_ns::SessionId& sid, const std::string& prefix,
    std::uint64_t device_offset, std::uint64_t length) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::HashDeviceRequest req;
    *req.mutable_session_id() = sid;
    req.set_prefix(prefix);
    req.set_device_offset(device_offset);
    req.set_length(length);
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_HASH_DEVICE_SHA256, req_ser);
    if (!rpc.ok) {
        return Result<std::string>::failure(std::move(rpc.err));
    }
    proto_ns::HashDeviceReply hr;
    hr.ParseFromString(rpc.payload);
    return Result<std::string>::success(hr.digest());
}

Result<proto_ns::SessionStats> LinuxPolkitBackend::closeSession(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->bulk.release();
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_CLOSE_SESSION, req_ser);
    if (!rpc.ok) {
        return Result<proto_ns::SessionStats>::failure(std::move(rpc.err));
    }
    proto_ns::SessionStats stats;
    stats.ParseFromString(rpc.payload);
    return Result<proto_ns::SessionStats>::success(std::move(stats));
}

Result<void> LinuxPolkitBackend::unmount(const std::string& device_path) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PathRequest req;
    req.set_device_path(device_path);
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_UNMOUNT, req_ser);
    if (!rpc.ok) {
        return Result<void>::failure(std::move(rpc.err));
    }
    return Result<void>::success();
}

Result<void> LinuxPolkitBackend::eject(const std::string& device_path) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PathRequest req;
    req.set_device_path(device_path);
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_EJECT, req_ser);
    if (!rpc.ok) {
        return Result<void>::failure(std::move(rpc.err));
    }
    return Result<void>::success();
}

Result<void> LinuxPolkitBackend::mapBulkBuffer(const proto_ns::SessionId& sid,
                                               std::size_t size_bytes) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::MapBulkBufferRequest req;
    *req.mutable_session_id() = sid;
    req.set_size_bytes(size_bytes);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    if (size_bytes == 0) {
        const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_MAP_BULK_BUFFER, req_ser);
        if (!rpc.ok) {
            return Result<void>::failure(std::move(rpc.err));
        }
        state_->bulk.release();
        return Result<void>::success();
    }

    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_MAP_BULK_BUFFER, req_ser, true);
    if (!rpc.ok) {
        return Result<void>::failure(std::move(rpc.err));
    }
    proto_ns::MapBulkBufferReply map_reply;
    if (!map_reply.ParseFromString(rpc.payload)) {
        return Result<void>::failure(makeError(proto_ns::ERROR_UNKNOWN,
                                              "mapBulkBuffer: malformed reply"));
    }
    state_->bulk.release();
    if (!state_->bulk.adoptFromFd(rpc.ancillary_fd,
                                  static_cast<std::size_t>(map_reply.size_bytes()))) {
        if (rpc.ancillary_fd >= 0) {
            ::close(rpc.ancillary_fd);
        }
        return Result<void>::failure(makeError(proto_ns::ERROR_UNKNOWN,
                                              "mapBulkBuffer: mmap failed", errno));
    }
    state_->bulk_session = sid;
    return Result<void>::success();
}

void* LinuxPolkitBackend::bulkBufferBase() const {
    return state_->bulk.base();
}

std::size_t LinuxPolkitBackend::bulkBufferSize() const {
    return state_->bulk.size();
}

Result<std::size_t> LinuxPolkitBackend::bulkWriteFromBuffer(
    const proto_ns::SessionId& sid, std::uint64_t buffer_offset,
    std::uint64_t device_offset, std::size_t length) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->bulk.valid() || length == 0
        || buffer_offset > state_->bulk.size()
        || length > state_->bulk.size() - buffer_offset) {
        return Result<std::size_t>::failure(
            makeError(proto_ns::ERROR_UNKNOWN, "bulkWrite: no buffer mapped or OOB"));
    }
    proto_ns::WriteRequest req;
    *req.mutable_session_id() = sid;
    req.set_buffer_offset(buffer_offset);
    req.set_offset(device_offset);
    req.set_length(length);
    std::string req_ser;
    req.SerializeToString(&req_ser);
    const auto rpc = callRpcLocked(state_.get(), proto_ns::WIRE_BULK_WRITE, req_ser);
    if (!rpc.ok) {
        return Result<std::size_t>::failure(std::move(rpc.err));
    }
    proto_ns::WriteResult wr;
    wr.ParseFromString(rpc.payload);
    return Result<std::size_t>::success(static_cast<std::size_t>(wr.bytes_written()));
}

void LinuxPolkitBackend::submitBulkWriteAsync(const proto_ns::SessionId& sid,
                                              std::uint64_t buffer_offset,
                                              std::uint64_t device_offset,
                                              std::size_t length,
                                              BulkWriteCallback on_complete) {
    {
        std::lock_guard<std::mutex> lk(state_->queue_mutex);
        if (!state_->worker_started) {
            state_->worker_started = true;
            State* st = state_.get();
            LinuxPolkitBackend* self = this;
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
