// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// LinuxEmbeddedBackend - in-process IPrivilegedWriter for environments where
// the imager already runs as root (embedded / kiosk installs). No helper
// process, no polkit prompt: device I/O happens in the GUI process with
// memfd-backed bulk writes (§6).
//
// Selected by the factory when geteuid() == 0 on Linux.

#pragma once

#include "../privileged_writer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rpi_imager::privileged::backends {

class LinuxEmbeddedBackend final : public IPrivilegedWriter {
public:
    LinuxEmbeddedBackend();
    ~LinuxEmbeddedBackend() override;

    LinuxEmbeddedBackend(const LinuxEmbeddedBackend&) = delete;
    LinuxEmbeddedBackend& operator=(const LinuxEmbeddedBackend&) = delete;

    Result<proto_ns::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override { return BackendKind::LinuxEmbedded; }
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

    // Bulk plane (concrete-class API, mirrors WindowsUacBackend / MacOSXpcBackend).
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

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rpi_imager::privileged::backends
