// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Platform Abstraction Layer for privileged disk operations.
//
// This is the SINGLE interface the unprivileged client uses for any
// operation that requires elevated privilege (device open, write, read,
// unmount, drive enumeration). Backends implement this interface against
// platform-specific privilege models:
//
//   - MacOSXpcBackend          : SMAppService daemon + NSXPC + shared memory
//   - MacOSAuthopenLegacyBackend : authopen + in-process pwrite (legacy)
//   - LinuxPolkitBackend       : polkit + D-Bus + memfd_create
//   - LinuxEmbeddedBackend     : direct in-process I/O (already root)
//   - WindowsUacBackend        : UAC-elevated helper + named pipes
//   - InProcessTestBackend     : tempfile-backed pseudo-device for tests
//   - FaultInjectingBackend    : wraps another backend; injects errors
//
// The boundary between client and helper is expressed entirely via the
// protobuf message types in proto/imager.proto. The C++ interface here
// uses those generated types directly to minimise translation cost.
//
// Design notes (see doc/privileged-helper-plan.md):
//   - Result<T> is the only way information flows back; no exceptions.
//   - submitWrite() is fire-and-callback; backends notify on real events.
//   - Slot data lives in shared memory (or a tempfile for the test backend);
//     it is NOT serialised through protobuf. Slot metadata is.
//   - The interface is deliberately small. Add methods only when a real
//     caller needs them.

#pragma once

#include "proto/imager.pb.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rpi_imager::privileged {

namespace proto_ns = rpi_imager::privileged::proto;

// Result<T> - explicit success/failure pair.
//
// On success, `value` is populated and `error.code() == ERROR_NONE`.
// On failure, `error` is populated with an actionable code + detail.
//
// This is a stable C++ type, deliberately hand-written rather than generated:
// callers should not have to include protobuf headers transitively just to
// read an error code. The contained `error` is itself a proto message, but
// that's an opaque field from the caller's point of view.
template <typename T>
struct Result {
    bool ok;
    T value;
    proto_ns::ErrorInfo error;

    static Result success(T v) {
        Result r;
        r.ok = true;
        r.value = std::move(v);
        return r;
    }

    static Result failure(proto_ns::ErrorInfo err) {
        Result r;
        r.ok = false;
        r.error = std::move(err);
        return r;
    }
};

// Specialisation for void operations.
template <>
struct Result<void> {
    bool ok;
    proto_ns::ErrorInfo error;

    static Result success() { return {true, {}}; }
    static Result failure(proto_ns::ErrorInfo err) { return {false, std::move(err)}; }
};

// A handle into the bulk-write shared-memory ring.
//
// `data` points at a region the caller may write into. The region remains
// valid until the corresponding `submitWrite()` completion callback fires.
// `index` is the opaque identifier the caller passes back via submitWrite().
struct Slot {
    std::uint8_t* data = nullptr;
    std::size_t   capacity = 0;
    std::uint32_t index = 0;
};

// Identifies which concrete backend is in use. Useful for diagnostics
// (the "Backend: ..." line we'll log at session start) and for tests
// that want to assert "the factory selected the right one".
enum class BackendKind {
    InProcessTest,
    FaultInjecting,
    MacOSAuthopenLegacy,
    MacOSXpc,
    LinuxPolkit,
    LinuxEmbedded,
    WindowsUac,
};

class IPrivilegedWriter {
public:
    virtual ~IPrivilegedWriter() = default;

    // -------------------------------------------------------------------
    // Helper lifecycle
    // -------------------------------------------------------------------

    // Returns the current installation state of the helper. Cheap; safe to
    // call repeatedly to drive UI state ("helper is disabled" prompts).
    virtual Result<proto_ns::HelperStatus> queryHelperStatus() = 0;

    // Installs/registers the helper. May prompt the user (e.g. macOS
    // SMAppService Login Items, Linux polkit auth, Windows UAC). Backends
    // that don't need installation (InProcess, Embedded) succeed instantly.
    virtual Result<void> installHelper() = 0;

    // Unregisters the helper. Best-effort; returns success even if the
    // helper was already absent.
    virtual Result<void> uninstallHelper() = 0;

    virtual BackendKind backend() const = 0;
    virtual std::string backendDescription() const = 0;

    // -------------------------------------------------------------------
    // Drive enumeration
    // -------------------------------------------------------------------

    using DriveChangeCallback = std::function<void(const proto_ns::DriveChange&)>;

    // Returns the current snapshot of attached drives. Equivalent to a
    // single point-in-time call.
    virtual Result<std::vector<proto_ns::DriveInfo>> listDrivesNow() = 0;

    // Streams drive change events. The callback fires on the backend's
    // thread; clients are responsible for thread-safe handoff (e.g. into
    // a Qt queued connection on the GUI thread).
    virtual Result<void> subscribeDrives(DriveChangeCallback) = 0;
    virtual Result<void> unsubscribeDrives() = 0;

    // -------------------------------------------------------------------
    // Write session lifecycle
    // -------------------------------------------------------------------

    virtual Result<proto_ns::SessionId> openSession(
        const std::string& device_path,
        const proto_ns::OpenOptions& opts) = 0;

    virtual Result<proto_ns::DeviceLimits> queryDeviceLimits(
        const proto_ns::SessionId&) = 0;

    virtual Result<void> prepareDevice(
        const proto_ns::SessionId&,
        const proto_ns::PrepareOptions&) = 0;

    // Acquires a slot from the bulk-write ring. Blocks the caller (with
    // periodic cancellation checks) until a slot is free; that's the
    // back-pressure mechanism.
    virtual Result<Slot> acquireSlot(const proto_ns::SessionId&) = 0;

    // Submits a slot for writing at the given device offset. The slot's
    // memory must remain untouched by the caller until on_complete fires.
    using WriteCompleteCallback =
        std::function<void(const proto_ns::WriteResult&)>;
    virtual void submitWrite(
        const proto_ns::SessionId&,
        std::uint32_t slot_index,
        std::uint64_t offset,
        std::uint64_t length,
        WriteCompleteCallback on_complete) = 0;

    // Synchronous read for verify path. `len` is bounded by the backend's
    // verify buffer size; large reads should be issued in chunks.
    virtual Result<std::size_t> readChunk(
        const proto_ns::SessionId&,
        std::uint64_t offset,
        std::byte* buf,
        std::size_t len) = 0;

    // Synchronous write counterpart to readChunk, for diagnostics and
    // small-data paths. Bulk writes go through the slot-based
    // acquireSlot/submitWrite path and shared memory; this method is
    // for cases where simplicity matters more than throughput (a few KB
    // of metadata, the diagnostic GUI's "test the helper" button, etc.).
    virtual Result<std::size_t> writeChunk(
        const proto_ns::SessionId&,
        std::uint64_t offset,
        const std::byte* buf,
        std::size_t len) = 0;

    virtual Result<void> syncDevice(const proto_ns::SessionId&) = 0;

    // Compute a SHA-256 digest of `length` bytes from the device,
    // starting at `device_offset`, optionally prefixed by `prefix`
    // (the imager's "first block", which is hashed during write but
    // written to disk last so isn't on the device at verify time).
    //
    // Helper-side execution: no data crosses the IPC boundary -
    // only the 32-byte digest. This is the fast path for verify
    // after a large write. Returns a 32-byte byte string on success.
    //
    // Default base-class implementation reports NOT_IMPLEMENTED so
    // legacy/test backends keep working. Backends that override it
    // claim ownership of the verify pass.
    virtual Result<std::string> hashDeviceSha256(
        const proto_ns::SessionId& /*sid*/,
        const std::string& /*prefix*/,
        std::uint64_t /*device_offset*/,
        std::uint64_t /*length*/) {
        proto_ns::ErrorInfo err;
        err.set_code(proto_ns::ERROR_NOT_IMPLEMENTED);
        err.set_detail("hashDeviceSha256 not implemented by this backend");
        return Result<std::string>::failure(err);
    }

    // Closes the session and returns the accumulated SessionStats payload
    // (per §7a of the design doc). The session id is invalid afterwards.
    virtual Result<proto_ns::SessionStats> closeSession(
        const proto_ns::SessionId&) = 0;

    // -------------------------------------------------------------------
    // Maintenance
    // -------------------------------------------------------------------

    virtual Result<void> unmount(const std::string& device_path) = 0;
    virtual Result<void> eject(const std::string& device_path) = 0;
};

// Factory selects the appropriate backend based on platform, OS version,
// and configuration. Single entry point for production code.
class PrivilegedWriterFactory {
public:
    // Constructor signature for client-supplied backends. The library is
    // Qt-free by design; production code paths that wrap Qt-using
    // platform machinery (e.g. PlatformQuirks) live outside the library
    // and inject themselves via this hook.
    using BackendConstructor = std::function<std::unique_ptr<IPrivilegedWriter>()>;

    struct Config {
        // Prefer the privileged helper backend over legacy paths (default).
        // When false, the factory skips the native helper backend and uses
        // the client-supplied local_backend_constructor instead - useful for
        // diagnostic comparison against the legacy in-process path.
        bool prefer_helper = true;

        // Whether the factory may show user prompts during creation
        // (e.g. SMAppService registration). Set false in headless / CLI
        // contexts where a prompt would block indefinitely.
        bool allow_user_prompt = true;

        // Absolute path to the calling .app bundle (macOS) or installation
        // root (Linux/Windows). Used by helper installation paths to locate
        // the helper binary inside the bundle.
        std::string app_bundle_path;

        // Client-supplied constructor for a "do it ourselves" backend that
        // wraps existing in-process code (the LocalShimBackend). Selected on
        // platforms without a native privileged backend and on the macOS
        // opt-out path. Leaving it null causes the factory to fall back to
        // InProcessTestBackend, which is appropriate for tests but not for
        // production.
        BackendConstructor local_backend_constructor;

        // Test override: when non-null, the factory returns this instance
        // unchanged. Wins over local_backend_constructor.
        std::unique_ptr<IPrivilegedWriter> testing_override;
    };

    static std::unique_ptr<IPrivilegedWriter> create(Config config);
};

} // namespace rpi_imager::privileged
