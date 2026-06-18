// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// WindowsUacBackend - client-side IPrivilegedWriter that talks to the
// rpi-imager-writer.exe helper over a named pipe, with a shared-memory bulk
// plane. This is the Windows analogue of MacOSXpcBackend.
//
// Elevation model (doc/privileged-helper-plan.md §14.2, decided: vehicle A):
// the helper is launched lazily, on the first privileged operation, via
// ShellExecuteEx with the "runas" verb (one UAC prompt per app run). It stays
// resident for the GUI's lifetime and exits when the control pipe closes
// (§14.8). The client authenticates the pipe via its ACL; the helper
// authenticates the connecting client via publisher-pinned Authenticode
// verification (§14.4, peer_auth_win.cpp).
//
// SCAFFOLD STATUS: the transport plumbing (launch, connect, framed RPC) is
// wired; the privileged operations are still being brought up against the
// helper server (src/writer/server/win). Gated behind the
// RPI_IMAGER_ENABLE_WINDOWS_HELPER build option and an explicit runtime
// opt-in, so it never changes default behavior.
//
// Windows-only translation unit: CMake compiles it only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set (like the macOS XPC backend), so it
// carries no platform #ifdef of its own.

#pragma once

#include "../privileged_writer.h"
#include "../../drivelist/drivelist.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rpi_imager::privileged::backends {

class WindowsUacBackend final : public IPrivilegedWriter {
public:
    struct Options {
        // Absolute path to rpi-imager-writer.exe. If empty, the backend
        // resolves it next to the running client executable.
        std::string helper_exe_path;

        // Fail privileged ops if the helper handshake takes longer than this.
        // 0 disables the timeout.
        std::uint32_t handshake_timeout_ms = 30000;  // includes UAC prompt time
    };

    WindowsUacBackend();
    explicit WindowsUacBackend(Options opts);
    ~WindowsUacBackend() override;

    WindowsUacBackend(const WindowsUacBackend&) = delete;
    WindowsUacBackend& operator=(const WindowsUacBackend&) = delete;

    // Helper lifecycle
    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::WindowsUac; }
    std::string  backendDescription() const override;

    // Drive enumeration (§5)
    Result<std::vector<proto_ns::DriveInfo>> listDrivesNow() override;
    Result<void> subscribeDrives(DriveChangeCallback cb) override;
    Result<void> unsubscribeDrives() override;

    // Sessions / bulk plane (§6)
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

    // Maintenance
    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;

    // ---- Bulk-write shared-memory path (concrete-class-only) -----------
    // Mirrors MacOSXpcBackend so the Windows FileOperations adapter
    // (WindowsHelperFileOperations) can drive the bulk plane identically.
    Result<void>          mapBulkBuffer(const proto_ns::SessionId& sid,
                                          std::size_t size_bytes);
    void*                 bulkBufferBase() const;
    std::size_t           bulkBufferSize() const;
    Result<std::size_t>   bulkWriteFromBuffer(const proto_ns::SessionId& sid,
                                                std::uint64_t buffer_offset,
                                                std::uint64_t device_offset,
                                                std::size_t length);

    using BulkWriteCallback = std::function<void(Result<std::size_t>)>;
    void                  submitBulkWriteAsync(const proto_ns::SessionId& sid,
                                                  std::uint64_t buffer_offset,
                                                  std::uint64_t device_offset,
                                                  std::size_t length,
                                                  BulkWriteCallback on_complete);

    // §5 concrete extension matching MacOSXpcBackend::listDevicesNow.
    Result<std::vector<Drivelist::DeviceDescriptor>> listDevicesNow();

public:
    struct State;
private:
    std::unique_ptr<State> state_;
};

} // namespace rpi_imager::privileged::backends
