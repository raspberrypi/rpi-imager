// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// LocalShimBackend - "do it ourselves" backend.
//
// Implements IPrivilegedWriter by delegating to the existing Qt-based
// platform code (PlatformQuirks for unmount/eject; FileOperations for
// device I/O). The shim lives in the client codebase rather than inside
// the privileged_io library because it depends on Qt; the library itself
// is Qt-free (see doc/privileged-helper-plan.md §8a) so a future Rust
// helper can reuse the proto schema without dragging Qt along.
//
// This is the opt-out / non-macOS fallback backend: on macOS the
// helper-routed MacOSXpcBackend is the default, and the shim is selected
// only when the helper path is disabled. On Linux and Windows it remains
// the active backend until native privileged backends are built.
//
// Methods without a native implementation here return
// ERROR_NOT_IMPLEMENTED, which makes "not wired on this backend" obvious
// in logs.

#pragma once

#include "privileged_io/privileged_writer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class LocalShimBackend final
    : public rpi_imager::privileged::IPrivilegedWriter {
    using Self = LocalShimBackend;
    template <typename T>
    using Result = rpi_imager::privileged::Result<T>;
    using Slot = rpi_imager::privileged::Slot;
    using BackendKind = rpi_imager::privileged::BackendKind;

public:
    LocalShimBackend();
    ~LocalShimBackend() override;

    LocalShimBackend(const Self&) = delete;
    Self& operator=(const Self&) = delete;

    // Helper lifecycle - in-process backends have nothing to install.
    Result<rpi_imager::privileged::proto::HelperStatus> queryHelperStatus() override;
    Result<void> installHelper() override;
    Result<void> uninstallHelper() override;
    BackendKind  backend() const override;
    std::string  backendDescription() const override;

    // Drive enumeration - not implemented by the shim; the client's
    // DriveListModel uses its own poll loop on this backend.
    Result<std::vector<rpi_imager::privileged::proto::DriveInfo>> listDrivesNow() override;
    Result<void> subscribeDrives(DriveChangeCallback cb) override;
    Result<void> unsubscribeDrives() override;

    // Sessions / bulk write plane - all return NOT_IMPLEMENTED; these are
    // provided only by a native privileged backend (MacOSXpcBackend).
    Result<rpi_imager::privileged::proto::SessionId> openSession(
        const std::string& device_path,
        const rpi_imager::privileged::proto::OpenOptions&) override;
    Result<rpi_imager::privileged::proto::DeviceLimits> queryDeviceLimits(
        const rpi_imager::privileged::proto::SessionId&) override;
    Result<void> prepareDevice(
        const rpi_imager::privileged::proto::SessionId&,
        const rpi_imager::privileged::proto::PrepareOptions&) override;
    Result<Slot> acquireSlot(const rpi_imager::privileged::proto::SessionId&) override;
    void submitWrite(const rpi_imager::privileged::proto::SessionId&,
                     std::uint32_t slot_index,
                     std::uint64_t offset,
                     std::uint64_t length,
                     WriteCompleteCallback on_complete) override;
    Result<std::size_t> readChunk(
        const rpi_imager::privileged::proto::SessionId&,
        std::uint64_t offset,
        std::byte* buf,
        std::size_t len) override;
    Result<std::size_t> writeChunk(
        const rpi_imager::privileged::proto::SessionId&,
        std::uint64_t offset,
        const std::byte* buf,
        std::size_t len) override;
    Result<void> syncDevice(const rpi_imager::privileged::proto::SessionId&) override;
    Result<rpi_imager::privileged::proto::SessionStats> closeSession(
        const rpi_imager::privileged::proto::SessionId&) override;

    // Maintenance - delegate to PlatformQuirks::unmountDisk / ::ejectDisk.
    Result<void> unmount(const std::string& device_path) override;
    Result<void> eject(const std::string& device_path) override;
};

// Convenience: a constructor function suitable for use as
// PrivilegedWriterFactory::Config::local_backend_constructor.
inline rpi_imager::privileged::PrivilegedWriterFactory::BackendConstructor
makeLocalShimConstructor() {
    return []() -> std::unique_ptr<rpi_imager::privileged::IPrivilegedWriter> {
        return std::make_unique<LocalShimBackend>();
    };
}
