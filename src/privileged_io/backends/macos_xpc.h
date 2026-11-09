// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// MacOSXpcBackend - client-side IPrivilegedWriter that talks to the
// rpi-imager-writer helper over NSXPC.
//
// Phase 1b proof-of-architecture: only `unmount`, `eject`, and the
// helper-status methods are implemented. All write/read/session
// methods return ERROR_NOT_IMPLEMENTED while the bulk-write plane
// (shared-memory ring) is being designed in subsequent phases.
//
// macOS-only. Compile-gated in privileged_io/CMakeLists.txt.

#pragma once

#include "../privileged_writer.h"
#include "../../drivelist/drivelist.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef __OBJC__
@class NSXPCConnection;
typedef NSXPCConnection* NSXPCConnectionPtr;
#else
typedef void* NSXPCConnectionPtr;  // opaque from C++ TUs
#endif

namespace rpi_imager::privileged::backends {

class MacOSXpcBackend final : public IPrivilegedWriter {
public:
    struct Options {
        // The Mach service name to connect to. Defaults to the production
        // value defined in RpiImagerWriterProtocol; tests can override it
        // to point at a per-test launchd job.
        std::string mach_service_name;

        // If non-zero, fail openSession (and eventually any privileged op)
        // if helper handshake takes longer than this. 0 disables.
        std::uint32_t handshake_timeout_ms = 5000;
    };

    MacOSXpcBackend();
    explicit MacOSXpcBackend(Options opts);
    ~MacOSXpcBackend() override;

    MacOSXpcBackend(const MacOSXpcBackend&) = delete;
    MacOSXpcBackend& operator=(const MacOSXpcBackend&) = delete;

    // Helper lifecycle
    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::MacOSXpc; }
    std::string  backendDescription() const override;

    // Drive enumeration - phase 1b returns NOT_IMPLEMENTED.
    Result<std::vector<proto_ns::DriveInfo>> listDrivesNow() override;
    Result<void> subscribeDrives(DriveChangeCallback cb) override;
    Result<void> unsubscribeDrives() override;

    // Sessions / bulk plane - phase 1b returns NOT_IMPLEMENTED.
    Result<proto_ns::SessionId> openSession(const std::string& device_path,
                                             const proto_ns::OpenOptions&) override;
    Result<proto_ns::DeviceLimits> queryDeviceLimits(const proto_ns::SessionId&) override;
    Result<void> prepareDevice(const proto_ns::SessionId&,
                                const proto_ns::PrepareOptions&) override;
    Result<Slot> acquireSlot(const proto_ns::SessionId&) override;
    void submitWrite(const proto_ns::SessionId&,
                     std::uint32_t slot_index,
                     std::uint64_t offset,
                     std::uint64_t length,
                     WriteCompleteCallback on_complete) override;
    Result<std::size_t> readChunk(const proto_ns::SessionId&,
                                    std::uint64_t offset,
                                    std::byte* buf,
                                    std::size_t len) override;
    Result<std::size_t> writeChunk(const proto_ns::SessionId&,
                                     std::uint64_t offset,
                                     const std::byte* buf,
                                     std::size_t len) override;
    Result<void> syncDevice(const proto_ns::SessionId&) override;
    Result<std::string> hashDeviceSha256(
        const proto_ns::SessionId&,
        const std::string& prefix,
        std::uint64_t device_offset,
        std::uint64_t length) override;
    Result<proto_ns::SessionStats> closeSession(const proto_ns::SessionId&) override;

    // Maintenance - phase 1b proof-of-concept entry points.
    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;

    // ---- Bulk-write shared-memory path (concrete-class-only) -----------
    //
    // These methods are deliberately NOT on IPrivilegedWriter because
    // backends without a privilege boundary (InProcessTestBackend etc.)
    // don't need them. Production callers that want the bulk path
    // dynamic_cast / static_cast to MacOSXpcBackend.
    //
    // Lifecycle:
    //   1. mapBulkBuffer(sid, N)   - allocates an N-byte shm region,
    //                                 maps it both client and helper
    //                                 sides; client owns 'buffer base'.
    //   2. bulkWriteFromBuffer(sid, bufOff, devOff, len)
    //                                - helper pwrites len bytes from
    //                                  bufOff into deviceOffset.
    //   3. mapBulkBuffer(sid, 0)   - releases the buffer.
    //
    // bulkBufferBase() returns the pointer the client can write into
    // between the map and bulkWriteFromBuffer calls. Lifetime ends
    // when mapBulkBuffer(sid, 0) is called or the session closes.

    Result<void>          mapBulkBuffer(const proto_ns::SessionId& sid,
                                          std::size_t size_bytes);
    void*                 bulkBufferBase() const;
    std::size_t           bulkBufferSize() const;
    Result<std::size_t>   bulkWriteFromBuffer(const proto_ns::SessionId& sid,
                                                std::uint64_t buffer_offset,
                                                std::uint64_t device_offset,
                                                std::size_t length);

    // §5 Drivelist-in-helper, concrete-class extension. The PAL's own
    // listDrivesNow() returns proto::DriveInfo for cross-platform use;
    // the imager's existing UI code consumes Drivelist::DeviceDescriptor
    // verbatim, so we expose a method that returns the latter and
    // skips the proto round-trip. Phase 2/3 backends that adopt the
    // PAL-typed flow will plumb through the proto path instead.
    Result<std::vector<Drivelist::DeviceDescriptor>> listDevicesNow();

    // Async variant: returns immediately, invokes `on_complete` from a
    // GCD background queue when the helper's pwrite finishes. The
    // helper bounds in-flight writes at kBulkInFlightCap (8) regardless
    // of how many submissions the client queues up; this matches the
    // SSD/SD-card queue depths typical of the devices we target.
    //
    // The caller is responsible for slot management on the shared
    // buffer: don't reuse a slot's bytes until its corresponding
    // on_complete fires.
    using BulkWriteCallback = std::function<void(Result<std::size_t>)>;
    void                  submitBulkWriteAsync(const proto_ns::SessionId& sid,
                                                  std::uint64_t buffer_offset,
                                                  std::uint64_t device_offset,
                                                  std::size_t length,
                                                  BulkWriteCallback on_complete);

public:
    // Implementation detail exposed publicly so free helpers in the .mm
    // can take a State& without friend declarations. Not intended to be
    // referenced from outside the backend's implementation file.
    struct State;
private:
    std::unique_ptr<State> state_;
};

} // namespace rpi_imager::privileged::backends
