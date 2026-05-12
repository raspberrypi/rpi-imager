// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "local_shim_backend.h"
#include "platformquirks.h"

#include <QString>

namespace proto = rpi_imager::privileged::proto;
namespace priv = rpi_imager::privileged;

namespace {

priv::Result<void> notImplemented(const char* method) {
    proto::ErrorInfo e;
    e.set_code(proto::ERROR_NOT_IMPLEMENTED);
    e.set_detail(std::string("LocalShimBackend::") + method
                 + " not implemented in phase 1a");
    return priv::Result<void>::failure(std::move(e));
}

template <typename T>
priv::Result<T> notImplementedT(const char* method) {
    proto::ErrorInfo e;
    e.set_code(proto::ERROR_NOT_IMPLEMENTED);
    e.set_detail(std::string("LocalShimBackend::") + method
                 + " not implemented in phase 1a");
    return priv::Result<T>::failure(std::move(e));
}

priv::Result<void> fromDiskResult(PlatformQuirks::DiskResult r,
                                    const std::string& op,
                                    const std::string& device) {
    if (r == PlatformQuirks::DiskResult::Success) {
        return priv::Result<void>::success();
    }

    proto::ErrorInfo e;
    switch (r) {
        case PlatformQuirks::DiskResult::InvalidDrive:
            e.set_code(proto::ERROR_DEVICE_NOT_FOUND);
            e.set_detail(op + ": invalid or unknown device " + device);
            break;
        case PlatformQuirks::DiskResult::AccessDenied:
            e.set_code(proto::ERROR_DEVICE_PERMISSION);
            e.set_detail(op + ": access denied for " + device);
            break;
        case PlatformQuirks::DiskResult::Busy:
            e.set_code(proto::ERROR_DEVICE_BUSY);
            e.set_detail(op + ": device busy " + device);
            break;
        case PlatformQuirks::DiskResult::Error:
        default:
            e.set_code(op == "unmount" ? proto::ERROR_UNMOUNT_FAILED
                                       : proto::ERROR_EJECT_FAILED);
            e.set_detail(op + " failed for " + device);
            break;
    }
    return priv::Result<void>::failure(std::move(e));
}

}  // namespace

LocalShimBackend::LocalShimBackend() = default;
LocalShimBackend::~LocalShimBackend() = default;

// ---------------------------------------------------------------------------
// Helper lifecycle - in-process, nothing to install
// ---------------------------------------------------------------------------

priv::Result<proto::HelperStatus> LocalShimBackend::queryHelperStatus() {
    proto::HelperStatus s;
    s.set_state(proto::HELPER_STATE_INSTALLED_READY);
    s.set_installed_version("local-shim");
    s.set_client_version("local-shim");
    return priv::Result<proto::HelperStatus>::success(std::move(s));
}

priv::Result<void> LocalShimBackend::installHelper() {
    return priv::Result<void>::success();
}

priv::Result<void> LocalShimBackend::uninstallHelper() {
    return priv::Result<void>::success();
}

priv::BackendKind LocalShimBackend::backend() const {
    // Phase 1a: pick the closest existing kind. We aren't a "real" backend
    // so we co-opt one of the legacy backend kinds for the time being.
    // When phase 1b lands proper backends, this enum gains a dedicated
    // LocalShim entry and we remove the special case.
    return priv::BackendKind::MacOSAuthopenLegacy;
}

std::string LocalShimBackend::backendDescription() const {
    return "LocalShimBackend (phase 1a; delegates to PlatformQuirks)";
}

// ---------------------------------------------------------------------------
// Drive enumeration - not migrated yet
// ---------------------------------------------------------------------------

priv::Result<std::vector<proto::DriveInfo>> LocalShimBackend::listDrivesNow() {
    return notImplementedT<std::vector<proto::DriveInfo>>("listDrivesNow");
}

priv::Result<void> LocalShimBackend::subscribeDrives(DriveChangeCallback) {
    return notImplemented("subscribeDrives");
}

priv::Result<void> LocalShimBackend::unsubscribeDrives() {
    return notImplemented("unsubscribeDrives");
}

// ---------------------------------------------------------------------------
// Sessions - all NOT_IMPLEMENTED until later phases
// ---------------------------------------------------------------------------

priv::Result<proto::SessionId> LocalShimBackend::openSession(
    const std::string&, const proto::OpenOptions&) {
    return notImplementedT<proto::SessionId>("openSession");
}

priv::Result<proto::DeviceLimits> LocalShimBackend::queryDeviceLimits(
    const proto::SessionId&) {
    return notImplementedT<proto::DeviceLimits>("queryDeviceLimits");
}

priv::Result<void> LocalShimBackend::prepareDevice(const proto::SessionId&,
                                                     const proto::PrepareOptions&) {
    return notImplemented("prepareDevice");
}

priv::Result<priv::Slot> LocalShimBackend::acquireSlot(
    const proto::SessionId&) {
    return notImplementedT<priv::Slot>("acquireSlot");
}

void LocalShimBackend::submitWrite(const proto::SessionId&,
                                    std::uint32_t slot_index,
                                    std::uint64_t /*offset*/,
                                    std::uint64_t /*length*/,
                                    WriteCompleteCallback on_complete) {
    if (!on_complete) return;
    proto::WriteResult wr;
    wr.set_slot_index(slot_index);
    wr.set_bytes_written(0);
    *wr.mutable_error() = [&] {
        proto::ErrorInfo e;
        e.set_code(proto::ERROR_NOT_IMPLEMENTED);
        e.set_detail("LocalShimBackend::submitWrite not implemented in phase 1a");
        return e;
    }();
    on_complete(wr);
}

priv::Result<std::size_t> LocalShimBackend::readChunk(
    const proto::SessionId&, std::uint64_t, std::byte*, std::size_t) {
    return notImplementedT<std::size_t>("readChunk");
}

priv::Result<std::size_t> LocalShimBackend::writeChunk(
    const proto::SessionId&, std::uint64_t, const std::byte*, std::size_t) {
    return notImplementedT<std::size_t>("writeChunk");
}

priv::Result<void> LocalShimBackend::syncDevice(const proto::SessionId&) {
    return notImplemented("syncDevice");
}

priv::Result<proto::SessionStats> LocalShimBackend::closeSession(
    const proto::SessionId&) {
    return notImplementedT<proto::SessionStats>("closeSession");
}

// ---------------------------------------------------------------------------
// Maintenance - phase 1a proof-of-concept migration
// ---------------------------------------------------------------------------

priv::Result<void> LocalShimBackend::unmount(const std::string& device_path) {
    QString qPath = QString::fromStdString(device_path);
    auto r = PlatformQuirks::unmountDisk(qPath);
    return fromDiskResult(r, "unmount", device_path);
}

priv::Result<void> LocalShimBackend::eject(const std::string& device_path) {
    QString qPath = QString::fromStdString(device_path);
    auto r = PlatformQuirks::ejectDisk(qPath);
    return fromDiskResult(r, "eject", device_path);
}
