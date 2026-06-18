// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// WindowsUacBackend implementation. See windows_uac.h and
// doc/privileged-helper-plan.md §14.
//
// SCAFFOLD: the launch/connect/framed-RPC transport is implemented; session,
// bulk shared-memory, and verify paths are wired over the helper server
// (src/writer/server/win). Drive enumeration and the FileOperations adapter
// remain stubbed. This file compiles only under RPI_IMAGER_ENABLE_WINDOWS_HELPER
// and is never the default backend.

#include "windows_uac.h"

#include "../wire/handshake.h"
#include "../wire/win_shared_memory.h"
#include "../wire/drive_descriptor_json.h"
#include "../wire/duplex_connection.h"
#include "../wire/protocol.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

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

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

// Path to the running client executable, used to resolve the helper exe that
// ships beside it when Options::helper_exe_path is empty.
std::wstring clientExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

} // namespace

struct WindowsUacBackend::State {
    Options options;

    std::mutex      mutex;          // serialises one in-flight RPC at a time
    HANDLE          pipe = INVALID_HANDLE_VALUE;
    HANDLE          helper_process = nullptr;
    bool            connected = false;
    wire::DuplexConnection duplex;

    // Bulk plane.
    wire::WinSharedMemory bulk;
    proto_ns::SessionId   bulk_session;

    // Single-worker async submit queue (bounded in-flight is the helper's
    // job; the producer ring is the back-pressure - §13).
    struct AsyncJob {
        proto_ns::SessionId sid;
        std::uint64_t buffer_offset;
        std::uint64_t device_offset;
        std::size_t   length;
        BulkWriteCallback cb;
    };
    std::thread             worker;
    std::mutex              queue_mutex;
    std::condition_variable queue_cv;
    std::deque<AsyncJob>    queue;
    bool                    worker_stop = false;
    bool                    worker_started = false;

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
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
        if (helper_process != nullptr) {
            CloseHandle(helper_process);
        }
    }
};

WindowsUacBackend::WindowsUacBackend() : WindowsUacBackend(Options{}) {}

WindowsUacBackend::WindowsUacBackend(Options opts)
    : state_(std::make_unique<State>()) {
    state_->options = std::move(opts);
}

WindowsUacBackend::~WindowsUacBackend() = default;

// ---------------------------------------------------------------------------
// Connection lifecycle (§14.8 vehicle A: lazy launch, resident per app run)
// ---------------------------------------------------------------------------

namespace {

bool ensureConnectedImpl(WindowsUacBackend::State* st,
                         proto_ns::ErrorInfo& err) {
    if (st->connected && st->pipe != INVALID_HANDLE_VALUE && st->duplex.isAttached()) {
        return true;
    }

    // Unique, unpredictable pipe name. The helper is launched with this name
    // and creates the server end; we connect as the client.
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t guid_str[64] = {0};
    StringFromGUID2(guid, guid_str, 64);
    std::wstring pipe_name = std::wstring(L"\\\\.\\pipe\\rpi-imager-writer-") + guid_str;

    std::wstring helper = st->options.helper_exe_path.empty()
                              ? clientExeDir() + L"rpi-imager-writer.exe"
                              : widen(st->options.helper_exe_path);

    std::wstring params = L"--pipe " + pipe_name;

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";                 // triggers the UAC consent prompt
    sei.lpFile = helper.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        const DWORD gle = GetLastError();
        if (gle == ERROR_CANCELLED) {
            err = makeError(proto_ns::ERROR_HELPER_INSTALL_REJECTED,
                            "User declined the elevation prompt",
                            static_cast<int>(gle));
        } else {
            err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                            "Failed to launch privileged helper",
                            static_cast<int>(gle));
        }
        return false;
    }
    st->helper_process = sei.hProcess;

    // Wait for the helper's pipe server to come up, then connect. The helper
    // creates the named pipe shortly after launch; poll within the timeout.
    const DWORD deadline = GetTickCount() +
        (st->options.handshake_timeout_ms ? st->options.handshake_timeout_ms : 30000);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (;;) {
        pipe = CreateFileW(pipe_name.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(pipe_name.c_str(), 1000);
        } else {
            Sleep(50);  // not created yet
        }
        if (GetTickCount() > deadline) {
            err = makeError(proto_ns::ERROR_HELPER_NOT_INSTALLED,
                            "Timed out connecting to the privileged helper pipe");
            return false;
        }
    }

    st->pipe = pipe;
    st->connected = true;
    if (!st->duplex.isAttached()) {
        st->duplex.attach(pipe);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Framed RPC core (§14.3)
// ---------------------------------------------------------------------------

namespace {

bool callRpcLocked(WindowsUacBackend::State* st,
                   proto_ns::WireMethod method,
                   const std::string& request_payload,
                   proto_ns::ErrorInfo& err,
                   std::string& out_reply_payload) {
    if (!ensureConnectedImpl(st, err)) {
        return false;
    }

    const wire::RpcResult rpc = st->duplex.call(method, request_payload);
    if (!rpc.ok) {
        err = rpc.error;
        if (err.code() == proto_ns::ERROR_DEVICE_DISCONNECTED
            || err.code() == proto_ns::ERROR_DEVICE_IO) {
            st->connected = false;
        }
        return false;
    }
    out_reply_payload = rpc.payload;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Helper lifecycle
// ---------------------------------------------------------------------------

Result<proto_ns::HelperStatus> WindowsUacBackend::queryHelperStatus() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    std::string reply;
    // Handshake doubles as a status probe: it forces a connect + version
    // exchange. If the helper isn't reachable we report NOT_INSTALLED.
    proto_ns::ProtocolVersion pv;
    pv.set_version(kProtocolVersion);
    std::string pv_ser;
    pv.SerializeToString(&pv_ser);
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_HELLO, pv_ser, err, reply)) {
        proto_ns::HelperStatus s;
        if (err.code() == proto_ns::ERROR_HELPER_VERSION_MISMATCH) {
            s.set_state(proto_ns::HELPER_STATE_VERSION_MISMATCH);
        } else {
            s.set_state(proto_ns::HELPER_STATE_NOT_INSTALLED);
        }
        s.set_client_version(std::to_string(kProtocolVersion));
        return Result<proto_ns::HelperStatus>::success(std::move(s));
    }
    proto_ns::HelperStatus s;
    s.ParseFromString(reply);
    wire::applyClientProtocolVersion(s, kProtocolVersion);
    return Result<proto_ns::HelperStatus>::success(std::move(s));
}

Result<void> WindowsUacBackend::installHelper() {
    // Vehicle A has no install step; "install" is the lazy elevated launch on
    // first use (§14.8). Force a connect here so callers that want an explicit
    // install point get the UAC prompt now.
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    if (!ensureConnectedImpl(state_.get(), err)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<void> WindowsUacBackend::uninstallHelper() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->duplex.detach();
    if (state_->pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(state_->pipe);
        state_->pipe = INVALID_HANDLE_VALUE;
        state_->connected = false;
    }
    return Result<void>::success();
}

std::string WindowsUacBackend::backendDescription() const {
    return "WindowsUacBackend (elevated rpi-imager-writer.exe over named pipe)";
}

// ---------------------------------------------------------------------------
// Drive enumeration (§5)
// ---------------------------------------------------------------------------

Result<std::vector<proto_ns::DriveInfo>> WindowsUacBackend::listDrivesNow() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_LIST_DRIVES, {}, err, reply)) {
        return Result<std::vector<proto_ns::DriveInfo>>::failure(std::move(err));
    }
    proto_ns::DriveList list;
    list.ParseFromString(reply);
    std::vector<proto_ns::DriveInfo> out(list.drives().begin(), list.drives().end());
    return Result<std::vector<proto_ns::DriveInfo>>::success(std::move(out));
}

Result<void> WindowsUacBackend::subscribeDrives(DriveChangeCallback cb) {
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
    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_SUBSCRIBE_DRIVES, {}, err, reply)) {
        state_->duplex.setEventCallback(nullptr);
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<void> WindowsUacBackend::unsubscribeDrives() {
    state_->duplex.setEventCallback(nullptr);
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    std::string reply;
    (void)callRpcLocked(state_.get(), proto_ns::WIRE_UNSUBSCRIBE_DRIVES, {}, err, reply);
    return Result<void>::success();
}

Result<std::vector<Drivelist::DeviceDescriptor>>
WindowsUacBackend::listDevicesNow() {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_LIST_DRIVES, {}, err, reply)) {
        return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(std::move(err));
    }
    proto_ns::DriveList list;
    list.ParseFromString(reply);
    return Result<std::vector<Drivelist::DeviceDescriptor>>::success(
        wire::deviceDescriptorsFromDriveList(list));
}

// ---------------------------------------------------------------------------
// Session lifecycle (§6)
// ---------------------------------------------------------------------------

Result<proto_ns::SessionId> WindowsUacBackend::openSession(
    const std::string& device_path, const proto_ns::OpenOptions& opts) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::OpenSessionRequest req;
    req.set_device_path(device_path);
    *req.mutable_options() = opts;
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_OPEN_SESSION, req_ser, err, reply)) {
        return Result<proto_ns::SessionId>::failure(std::move(err));
    }
    proto_ns::SessionId sid;
    sid.ParseFromString(reply);
    return Result<proto_ns::SessionId>::success(std::move(sid));
}

Result<proto_ns::DeviceLimits> WindowsUacBackend::queryDeviceLimits(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_QUERY_DEVICE_LIMITS, req_ser, err, reply)) {
        return Result<proto_ns::DeviceLimits>::failure(std::move(err));
    }
    proto_ns::DeviceLimits limits;
    limits.ParseFromString(reply);
    return Result<proto_ns::DeviceLimits>::success(std::move(limits));
}

Result<void> WindowsUacBackend::prepareDevice(
    const proto_ns::SessionId& sid, const proto_ns::PrepareOptions& opts) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PrepareDeviceRequest req;
    *req.mutable_session_id() = sid;
    *req.mutable_options() = opts;
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_PREPARE_DEVICE, req_ser, err, reply)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<Slot> WindowsUacBackend::acquireSlot(const proto_ns::SessionId&) {
    // The bulk plane uses mapBulkBuffer + bulkWriteFromBuffer like macOS, not
    // the generic acquireSlot/submitWrite pair; that PAL path is unused here.
    Result<Slot> r;
    r.ok = false;
    r.error = makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                        "acquireSlot: use mapBulkBuffer/bulkWriteFromBuffer");
    return r;
}

void WindowsUacBackend::submitWrite(const proto_ns::SessionId&,
                                    std::uint32_t, std::uint64_t, std::uint64_t,
                                    WriteCompleteCallback on_complete) {
    if (on_complete) {
        proto_ns::WriteResult wr;
        *wr.mutable_error() = makeError(proto_ns::ERROR_NOT_IMPLEMENTED,
                                        "submitWrite: use submitBulkWriteAsync");
        on_complete(wr);
    }
}

Result<std::size_t> WindowsUacBackend::readChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    std::byte* out, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::ReadRequest req;
    *req.mutable_session_id() = sid;
    req.set_offset(offset);
    req.set_length(len);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_READ_CHUNK, req_ser, err, reply)) {
        return Result<std::size_t>::failure(std::move(err));
    }
    proto_ns::ReadReply rr;
    rr.ParseFromString(reply);
    const std::size_t n = std::min<std::size_t>(len, rr.data().size());
    std::memcpy(out, rr.data().data(), n);
    return Result<std::size_t>::success(n);
}

Result<std::size_t> WindowsUacBackend::writeChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    const std::byte* in, std::size_t len) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::WriteChunkRequest req;
    *req.mutable_session_id() = sid;
    req.set_offset(offset);
    req.set_data(std::string(reinterpret_cast<const char*>(in), len));
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_WRITE_CHUNK, req_ser, err, reply)) {
        return Result<std::size_t>::failure(std::move(err));
    }
    proto_ns::WriteChunkReply wr;
    wr.ParseFromString(reply);
    return Result<std::size_t>::success(static_cast<std::size_t>(wr.bytes_written()));
}

Result<void> WindowsUacBackend::syncDevice(const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_SYNC_DEVICE, req_ser, err, reply)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<std::string> WindowsUacBackend::hashDeviceSha256(
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

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_HASH_DEVICE_SHA256, req_ser, err, reply)) {
        return Result<std::string>::failure(std::move(err));
    }
    proto_ns::HashDeviceReply hr;
    hr.ParseFromString(reply);
    return Result<std::string>::success(hr.digest());
}

Result<proto_ns::SessionStats> WindowsUacBackend::closeSession(
    const proto_ns::SessionId& sid) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    state_->bulk.release();
    proto_ns::SessionRequest req;
    *req.mutable_session_id() = sid;
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_CLOSE_SESSION, req_ser, err, reply)) {
        return Result<proto_ns::SessionStats>::failure(std::move(err));
    }
    proto_ns::SessionStats stats;
    stats.ParseFromString(reply);
    return Result<proto_ns::SessionStats>::success(std::move(stats));
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

Result<void> WindowsUacBackend::unmount(const std::string& device_path) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PathRequest req;
    req.set_device_path(device_path);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_UNMOUNT, req_ser, err, reply)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

Result<void> WindowsUacBackend::eject(const std::string& device_path) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    proto_ns::PathRequest req;
    req.set_device_path(device_path);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_EJECT, req_ser, err, reply)) {
        return Result<void>::failure(std::move(err));
    }
    return Result<void>::success();
}

// ---------------------------------------------------------------------------
// Bulk-write shared-memory plane (§6, §14.3)
// ---------------------------------------------------------------------------

Result<void> WindowsUacBackend::mapBulkBuffer(const proto_ns::SessionId& sid,
                                              std::size_t size_bytes) {
    std::lock_guard<std::mutex> lk(state_->mutex);

    proto_ns::MapBulkBufferRequest req;
    *req.mutable_session_id() = sid;
    req.set_size_bytes(size_bytes);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_MAP_BULK_BUFFER, req_ser, err, reply)) {
        return Result<void>::failure(std::move(err));
    }

    if (size_bytes == 0) {
        state_->bulk.release();
        return Result<void>::success();
    }

    proto_ns::MapBulkBufferReply map_reply;
    if (!map_reply.ParseFromString(reply)) {
        return Result<void>::failure(makeError(proto_ns::ERROR_UNKNOWN,
                                                "mapBulkBuffer: malformed reply"));
    }
    if (map_reply.section_handle() == 0 || map_reply.size_bytes() == 0) {
        return Result<void>::failure(makeError(proto_ns::ERROR_UNKNOWN,
                                                "mapBulkBuffer: helper returned no handle"));
    }

    state_->bulk.release();
    if (!state_->bulk.adoptFromHandle(
            map_reply.section_handle(),
            static_cast<std::size_t>(map_reply.size_bytes()))) {
        return Result<void>::failure(makeError(
            proto_ns::ERROR_UNKNOWN,
            "mapBulkBuffer: client MapViewOfFile failed",
            static_cast<int>(GetLastError())));
    }
    state_->bulk_session = sid;
    return Result<void>::success();
}

void* WindowsUacBackend::bulkBufferBase() const {
    return state_->bulk.base();
}

std::size_t WindowsUacBackend::bulkBufferSize() const {
    return state_->bulk.size();
}

Result<std::size_t> WindowsUacBackend::bulkWriteFromBuffer(
    const proto_ns::SessionId& sid, std::uint64_t buffer_offset,
    std::uint64_t device_offset, std::size_t length) {
    std::lock_guard<std::mutex> lk(state_->mutex);
    if (!state_->bulk.valid() || length == 0
        || buffer_offset > state_->bulk.size()
        || length > state_->bulk.size() - buffer_offset) {
        return Result<std::size_t>::failure(makeError(
            proto_ns::ERROR_UNKNOWN,
            "bulkWriteFromBuffer: no buffer mapped or out of bounds"));
    }

    proto_ns::WriteRequest req;
    *req.mutable_session_id() = sid;
    req.set_buffer_offset(buffer_offset);
    req.set_offset(device_offset);
    req.set_length(length);
    std::string req_ser;
    req.SerializeToString(&req_ser);

    proto_ns::ErrorInfo err;
    std::string reply;
    if (!callRpcLocked(state_.get(), proto_ns::WIRE_BULK_WRITE, req_ser, err, reply)) {
        return Result<std::size_t>::failure(std::move(err));
    }
    proto_ns::WriteResult wr;
    wr.ParseFromString(reply);
    return Result<std::size_t>::success(static_cast<std::size_t>(wr.bytes_written()));
}

void WindowsUacBackend::submitBulkWriteAsync(const proto_ns::SessionId& sid,
                                             std::uint64_t buffer_offset,
                                             std::uint64_t device_offset,
                                             std::size_t length,
                                             BulkWriteCallback on_complete) {
    {
        std::lock_guard<std::mutex> lk(state_->queue_mutex);
        if (!state_->worker_started) {
            state_->worker_started = true;
            State* st = state_.get();
            WindowsUacBackend* self = this;
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
                    // bulkWriteFromBuffer takes State::mutex itself, so the
                    // single worker serialises the bulk RPCs against the
                    // control-plane RPCs.
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
