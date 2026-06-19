// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// LinuxPolkitBackend - client-side IPrivilegedWriter that talks to the
// rpi-imager-writer helper over a Unix-domain socket, with a memfd bulk
// plane. Elevation is via pkexec + polkit (phase 2).
//
// Linux-only translation unit: CMake compiles it only when
// RPI_IMAGER_ENABLE_LINUX_HELPER is set.

#pragma once

#include "../privileged_writer.h"
#include "../../drivelist/drivelist.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rpi_imager::privileged::backends {

class LinuxPolkitBackend final : public IPrivilegedWriter {
public:
    struct Options {
        // Absolute path to rpi-imager-writer. If empty, resolved next to the
        // running client executable, then /usr/bin/rpi-imager-writer.
        std::string helper_exe_path;

        std::uint32_t handshake_timeout_ms = 30000;
    };

    LinuxPolkitBackend();
    explicit LinuxPolkitBackend(Options opts);
    ~LinuxPolkitBackend() override;

    LinuxPolkitBackend(const LinuxPolkitBackend&) = delete;
    LinuxPolkitBackend& operator=(const LinuxPolkitBackend&) = delete;

    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::LinuxPolkit; }
    std::string  backendDescription() const override;

    Result<std::vector<proto_ns::DriveInfo>> listDrivesNow() override;
    Result<void> subscribeDrives(DriveChangeCallback cb) override;
    Result<void> unsubscribeDrives() override;

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

    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;

    Result<void>        mapBulkBuffer(const proto_ns::SessionId& sid,
                                      std::size_t size_bytes,
                                      std::uint32_t async_queue_depth = 0);
    void*               bulkBufferBase() const;
    std::size_t         bulkBufferSize() const;
    Result<std::size_t> bulkWriteFromBuffer(const proto_ns::SessionId& sid,
                                              std::uint64_t buffer_offset,
                                              std::uint64_t device_offset,
                                              std::size_t length);

    using BulkWriteCallback = std::function<void(Result<std::size_t>)>;
    void submitBulkWriteAsync(const proto_ns::SessionId& sid,
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
