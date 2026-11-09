// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// See service_impl.h. The pipe server + framed dispatch loop route the write-
// session lifecycle (§6) to a per-session Qt-free WindowsFileOperations
// (§14.5). The bulk shared-memory plane (§14.3) is wired; drive enumeration /
// maintenance through the helper (§14.7) remain stubbed.

#include "service_impl.h"
#include "peer_auth_win.h"

// Resolved via privileged_io's PUBLIC include dirs (source dir for wire/,
// binary dir for the generated proto/).
#include "wire/frame.h"
#include "wire/win_shared_memory.h"
#include "proto/imager.pb.h"

// The helper opens \\.\PhysicalDriveN with its own handle and writes from its
// own process - the whole point of the privilege boundary (§2). It reuses the
// Qt-free WindowsFileOperations the client uses in-process today (§14.5).
#include "windows/file_operations_windows.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto = rpi_imager::privileged::proto;
namespace wire = rpi_imager::privileged::wire;

namespace rpi_imager::writer {

namespace {

constexpr std::uint32_t kProtocolVersion = 1;

// Cap a single synchronous read/write chunk crossing the wire. Bulk transfers
// use the shared-memory plane (§14.3); this path is for metadata + verify.
constexpr std::size_t kMaxSyncChunk = 8u * 1024 * 1024;

// Per-session bytes-written ceiling (defense in depth; mirrors macOS helper).
constexpr std::uint64_t kMaxSessionBytesWritten = 64ull * 1024 * 1024 * 1024;

std::uint64_t physicalMemoryBytes() {
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st)) {
        return 0;
    }
    return static_cast<std::uint64_t>(st.ullTotalPhys);
}

// RAM-proportional bulk-buffer cap (1/3 physical RAM, floored at 256 MB).
std::uint64_t maxBulkBufferBytes() {
    constexpr std::uint64_t kFloor = 256ull * 1024 * 1024;
    const std::uint64_t phys = physicalMemoryBytes();
    if (phys == 0) return kFloor;
    const std::uint64_t third = phys / 3;
    return third > kFloor ? third : kFloor;
}

proto::ErrorInfo ok() { return proto::ErrorInfo(); }  // code == ERROR_NONE

proto::ErrorInfo fail(proto::ErrorCode code, const std::string& detail) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    return e;
}

proto::ErrorCode mapFileError(FileError e) {
    switch (e) {
        case FileError::kSuccess:   return proto::ERROR_NONE;
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
        case FileError::kTimeout:   return proto::ERROR_DEVICE_IO;
        default:                    return proto::ERROR_UNKNOWN;
    }
}

// §2 threat model: the helper refuses non-device paths. Only \\.\PhysicalDriveN
// is accepted, mirroring the macOS helper's /dev/disk* restriction.
bool isPhysicalDrivePath(const std::string& p) {
    constexpr char kPrefix[] = "\\\\.\\PhysicalDrive";
    const std::size_t n = sizeof(kPrefix) - 1;
    if (p.size() <= n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(p[i])) !=
            std::tolower(static_cast<unsigned char>(kPrefix[i]))) {
            return false;
        }
    }
    return true;
}

// Minimal Qt-free SHA-256 over CNG (BCrypt). The shipping client hash uses
// QCryptographicHash-backed CNG, which we can't link here (§8a); this is the
// helper-side equivalent for the fast verify path (§6 hashDeviceSha256).
class Sha256 {
public:
    bool init() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            return false;
        }
        DWORD obj_len = 0, cb = 0;
        if (BCryptGetProperty(alg_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&obj_len),
                              sizeof(obj_len), &cb, 0) < 0) {
            return false;
        }
        obj_.resize(obj_len);
        if (BCryptCreateHash(alg_, &hash_, obj_.data(), obj_len, nullptr, 0, 0) < 0) {
            return false;
        }
        return true;
    }
    bool update(const void* data, std::size_t len) {
        return BCryptHashData(hash_,
                              const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                              static_cast<ULONG>(len), 0) >= 0;
    }
    bool finish(std::string& out32) {
        UCHAR digest[32] = {0};
        if (BCryptFinishHash(hash_, digest, sizeof(digest), 0) < 0) {
            return false;
        }
        out32.assign(reinterpret_cast<char*>(digest), sizeof(digest));
        return true;
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
private:
    BCRYPT_ALG_HANDLE  alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    std::vector<UCHAR> obj_;
};

struct Session {
    std::unique_ptr<rpi_imager::WindowsFileOperations> fops;
    wire::WinSharedMemory bulk;
    std::uint64_t bytes_written = 0;
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
};

// Owns the privileged session state for one connected client. The dispatch
// loop is single-threaded (one client, one pipe), so no locking is needed.
class Helper {
public:
    explicit Helper(DWORD client_pid) : client_pid_(client_pid) {}

    proto::ErrorInfo dispatch(const proto::WireRequest& req, std::string& out_payload);

private:
    Session* find(const proto::SessionId& sid) {
        auto it = sessions_.find(sid.v());
        return it == sessions_.end() ? nullptr : &it->second;
    }

    proto::ErrorInfo handleOpenSession(const std::string& payload, std::string& out);
    proto::ErrorInfo handleQueryDeviceLimits(const std::string& payload, std::string& out);
    proto::ErrorInfo handlePrepareDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleMapBulkBuffer(const std::string& payload, std::string& out);
    proto::ErrorInfo handleBulkWrite(const std::string& payload, std::string& out);
    proto::ErrorInfo handleWriteChunk(const std::string& payload, std::string& out);
    proto::ErrorInfo handleReadChunk(const std::string& payload, std::string& out);
    proto::ErrorInfo handleSyncDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleHashDevice(const std::string& payload, std::string& out);
    proto::ErrorInfo handleCloseSession(const std::string& payload, std::string& out);

    DWORD client_pid_ = 0;
    std::unordered_map<std::uint64_t, Session> sessions_;
    std::uint64_t next_id_ = 1;
};

proto::ErrorInfo Helper::handleOpenSession(const std::string& payload, std::string& out) {
    proto::OpenSessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad OpenSessionRequest");
    if (!isPhysicalDrivePath(req.device_path())) {
        return fail(proto::ERROR_DEVICE_PERMISSION,
                    "helper only opens \\\\.\\PhysicalDriveN paths");
    }

    Session s;
    s.fops = std::make_unique<rpi_imager::WindowsFileOperations>();
    const FileError e = s.fops->OpenDevice(req.device_path());
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "OpenDevice failed for " + req.device_path());
    }

    const std::uint64_t id = next_id_++;
    sessions_.emplace(id, std::move(s));

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
    out_limits.set_logical_block_size(512);  // TODO(§14.5): query actual LBS
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
    const FileError e = s->fops->PrepareDevice(size, req.options().zero_last_mb());
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "PrepareDevice failed");
    }
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
        return fail(proto::ERROR_UNKNOWN,
                    "mapBulkBuffer: size exceeds RAM-proportional cap");
    }
    if (s->bytes_written >= kMaxSessionBytesWritten) {
        return fail(proto::ERROR_WRITE_FAILED,
                    "mapBulkBuffer: session bytes-written cap exhausted");
    }

    s->bulk.release();
    if (!s->bulk.createOwned(static_cast<std::size_t>(want))) {
        return fail(proto::ERROR_UNKNOWN, "mapBulkBuffer: CreateFileMapping failed");
    }

    std::uint64_t handle_value = 0;
    if (!s->bulk.duplicateInto(client_pid_, handle_value)) {
        s->bulk.release();
        return fail(proto::ERROR_UNKNOWN, "mapBulkBuffer: DuplicateHandle failed");
    }

    proto::MapBulkBufferReply reply;
    reply.set_section_handle(handle_value);
    reply.set_size_bytes(want);
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleBulkWrite(const std::string& payload, std::string& out) {
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
        return fail(proto::ERROR_WRITE_FAILED, "bulkWrite: buffer bounds check failed");
    }
    if (s->bytes_written + len > kMaxSessionBytesWritten) {
        return fail(proto::ERROR_WRITE_FAILED, "bulkWrite: session bytes-written cap reached");
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto* src = static_cast<const std::uint8_t*>(s->bulk.base()) + buf_off;
    const FileError e = s->fops->WriteAtOffset(req.offset(), src, static_cast<std::size_t>(len));
    const auto t1 = std::chrono::steady_clock::now();
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "bulkWrite: WriteAtOffset failed");
    }

    s->bytes_written += len;
    const auto lat_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    proto::WriteResult wr;
    wr.set_slot_index(req.slot_index());
    wr.set_bytes_written(len);
    wr.set_latency_us(static_cast<std::uint64_t>(lat_us));
    wr.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleWriteChunk(const std::string& payload, std::string& out) {
    proto::WriteChunkRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad WriteChunkRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "writeChunk: no session");
    if (req.data().size() > kMaxSyncChunk) {
        return fail(proto::ERROR_WRITE_FAILED, "writeChunk: chunk exceeds sync cap");
    }

    const FileError e = s->fops->WriteAtOffset(
        req.offset(),
        reinterpret_cast<const std::uint8_t*>(req.data().data()),
        req.data().size());
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "WriteAtOffset failed");
    }
    s->bytes_written += req.data().size();

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
    const FileError e = s->fops->ForceSync();
    if (e != FileError::kSuccess) {
        return fail(mapFileError(e), "ForceSync failed");
    }
    return ok();
}

proto::ErrorInfo Helper::handleHashDevice(const std::string& payload, std::string& out) {
    proto::HashDeviceRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad HashDeviceRequest");
    Session* s = find(req.session_id());
    if (!s) return fail(proto::ERROR_SESSION_NOT_FOUND, "hashDeviceSha256: no session");

    Sha256 sha;
    if (!sha.init()) return fail(proto::ERROR_UNKNOWN, "BCrypt SHA-256 init failed");

    // Optional prefix (the first block, written to disk last) is hashed first.
    if (!req.prefix().empty() && !sha.update(req.prefix().data(), req.prefix().size())) {
        return fail(proto::ERROR_UNKNOWN, "hash update (prefix) failed");
    }

    if (s->fops->Seek(req.device_offset()) != FileError::kSuccess) {
        return fail(proto::ERROR_DEVICE_IO, "hashDeviceSha256: seek failed");
    }
    // TODO(§14.5): with direct I/O the tail read must be sector-aligned; the
    // shipping verify path reads in aligned chunks. This straightforward
    // chunked read is adequate for bring-up and will be aligned during
    // hardware validation.
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

    proto::HashDeviceReply reply;
    reply.set_digest(digest);
    reply.SerializeToString(&out);
    return ok();
}

proto::ErrorInfo Helper::handleCloseSession(const std::string& payload, std::string& out) {
    proto::SessionRequest req;
    if (!req.ParseFromString(payload)) return fail(proto::ERROR_UNKNOWN, "bad SessionRequest");
    auto it = sessions_.find(req.session_id().v());
    if (it == sessions_.end()) return fail(proto::ERROR_SESSION_NOT_FOUND, "closeSession: no session");

    Session& s = it->second;
    s.bulk.release();
    const FileError e = s.fops->Close();
    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - s.started).count();

    proto::SessionStats stats;
    stats.set_bytes_written(s.bytes_written);
    stats.set_duration_ms(static_cast<std::uint64_t>(dur));
    stats.set_state(e == FileError::kSuccess ? proto::SESSION_STATE_SUCCEEDED
                                             : proto::SESSION_STATE_FAILED);
    if (e != FileError::kSuccess) {
        *stats.mutable_terminal_error() = fail(mapFileError(e), "Close failed");
    }
    stats.SerializeToString(&out);

    sessions_.erase(it);
    return ok();
}

proto::ErrorInfo Helper::dispatch(const proto::WireRequest& req, std::string& out_payload) {
    switch (req.method()) {
        case proto::WIRE_HELLO:
        case proto::WIRE_QUERY_HELPER_STATUS: {
            proto::HelperStatus status;
            status.set_state(proto::HELPER_STATE_INSTALLED_READY);
            status.set_installed_version(std::to_string(kProtocolVersion));
            status.set_client_version(std::to_string(kProtocolVersion));
            status.SerializeToString(&out_payload);
            return ok();
        }
        case proto::WIRE_OPEN_SESSION:        return handleOpenSession(req.payload(), out_payload);
        case proto::WIRE_QUERY_DEVICE_LIMITS: return handleQueryDeviceLimits(req.payload(), out_payload);
        case proto::WIRE_PREPARE_DEVICE:      return handlePrepareDevice(req.payload(), out_payload);
        case proto::WIRE_WRITE_CHUNK:         return handleWriteChunk(req.payload(), out_payload);
        case proto::WIRE_READ_CHUNK:          return handleReadChunk(req.payload(), out_payload);
        case proto::WIRE_SYNC_DEVICE:         return handleSyncDevice(req.payload(), out_payload);
        case proto::WIRE_HASH_DEVICE_SHA256:  return handleHashDevice(req.payload(), out_payload);
        case proto::WIRE_CLOSE_SESSION:       return handleCloseSession(req.payload(), out_payload);
        case proto::WIRE_MAP_BULK_BUFFER:     return handleMapBulkBuffer(req.payload(), out_payload);
        case proto::WIRE_BULK_WRITE:          return handleBulkWrite(req.payload(), out_payload);

        // Pending: drive enumeration / maintenance through the helper (§14.7).
        case proto::WIRE_LIST_DRIVES:
            return fail(proto::ERROR_NOT_IMPLEMENTED, "listDrives pending (§14.7)");
        case proto::WIRE_UNMOUNT:
        case proto::WIRE_EJECT:
            return fail(proto::ERROR_NOT_IMPLEMENTED, "maintenance pending");
        default:
            return fail(proto::ERROR_NOT_IMPLEMENTED, "unknown method");
    }
}

// Pulls the value following "--pipe" out of argv. Returns empty if absent.
std::wstring pipeNameFromArgs(int argc, char** argv) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--pipe") {
            const char* name = argv[i + 1];
            const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
            if (n > 1) {
                MultiByteToWideChar(CP_UTF8, 0, name, -1, w.data(), n);
            }
            return w;
        }
    }
    return std::wstring();
}

bool writeAll(HANDLE pipe, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(
            (len - off) > 0x7fffffffu ? 0x7fffffffu : (len - off));
        if (!WriteFile(pipe, data + off, chunk, &wrote, nullptr) || wrote == 0) {
            return false;
        }
        off += wrote;
    }
    return true;
}

// Creates a named-pipe security descriptor granting generic access to
// interactive users (§14.4). Caller must LocalFree the returned descriptor.
PSECURITY_DESCRIPTOR createPipeSecurityDescriptor() {
    PSECURITY_DESCRIPTOR psd = nullptr;
    // IU = INTERACTIVE USERS; allows the unprivileged GUI client to connect
    // to the elevated helper's pipe server.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;IU)", SDDL_REVISION_1, &psd, nullptr)) {
        return nullptr;
    }
    return psd;
}

// Reads framed requests and writes framed responses until the client
// disconnects. Returns when the pipe closes (§14.8 disconnect watchdog).
void serviceLoop(HANDLE pipe, DWORD client_pid) {
    if (!verifyConnectingClient(client_pid)) {
        return;
    }

    Helper helper(client_pid);
    wire::FrameAccumulator acc;
    char buf[8192];
    std::string payload;

    for (;;) {
        bool oversize = false;
        if (!acc.next(payload, oversize)) {
            if (oversize) {
                return;  // protocol violation - drop the client
            }
            DWORD got = 0;
            if (!ReadFile(pipe, buf, sizeof(buf), &got, nullptr) || got == 0) {
                return;  // client disconnected
            }
            acc.append(buf, got);
            continue;
        }

        proto::WireRequest req;
        if (!req.ParseFromString(payload)) {
            return;  // malformed - drop
        }

        proto::WireResponse resp;
        resp.set_request_id(req.request_id());
        std::string reply_payload;
        const proto::ErrorInfo err = helper.dispatch(req, reply_payload);
        *resp.mutable_error() = err;
        if (err.code() == proto::ERROR_NONE) {
            resp.set_payload(reply_payload);
        }

        std::string ser;
        if (!resp.SerializeToString(&ser) || ser.size() > wire::kMaxFrameBytes) {
            return;
        }
        const std::string frame = wire::encodeFrame(ser);
        if (!writeAll(pipe, frame.data(), frame.size())) {
            return;
        }
    }
}

} // namespace

int RpiImagerWriterServiceMainWin(int argc, char** argv) {
    const std::wstring pipe_name = pipeNameFromArgs(argc, argv);
    if (pipe_name.empty()) {
        return 2;  // misuse: launched without a pipe name
    }

    // §14.4: restrict the pipe to interactive users; reject remote clients.
    PSECURITY_DESCRIPTOR psd = createPipeSecurityDescriptor();
    if (psd == nullptr) {
        return 4;
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = psd;
    sa.bInheritHandle = FALSE;

    HANDLE pipe = CreateNamedPipeW(
        pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,                       // a single client (the launching GUI)
        64 * 1024, 64 * 1024,
        0,
        &sa);
    LocalFree(psd);
    if (pipe == INVALID_HANDLE_VALUE) {
        return 3;
    }

    const BOOL connected =
        ConnectNamedPipe(pipe, nullptr) ? TRUE
                                        : (GetLastError() == ERROR_PIPE_CONNECTED);
    if (connected) {
        DWORD client_pid = 0;
        if (GetNamedPipeClientProcessId(pipe, &client_pid) && client_pid != 0) {
            serviceLoop(pipe, client_pid);
        }
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    return 0;
}

} // namespace rpi_imager::writer
