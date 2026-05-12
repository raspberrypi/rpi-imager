// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "fault_injecting.h"

#include <thread>
#include <utility>

namespace rpi_imager::privileged::backends {

namespace {

proto_ns::ErrorInfo makeError(proto_ns::ErrorCode code,
                                const std::string& detail,
                                int kernel_errno = 0) {
    proto_ns::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    e.set_kernel_errno(kernel_errno);
    return e;
}

} // namespace

FaultInjectingBackend::FaultInjectingBackend(
    std::unique_ptr<IPrivilegedWriter> inner, Plan plan)
    : inner_(std::move(inner)), plan_(std::move(plan)) {}

void FaultInjectingBackend::setPlan(Plan plan) {
    std::lock_guard<std::mutex> lk(plan_mutex_);
    plan_ = std::move(plan);
}

// ---- Forwarded methods that don't take part in fault injection ----------

Result<proto_ns::HelperStatus> FaultInjectingBackend::queryHelperStatus() {
    return inner_->queryHelperStatus();
}
Result<void> FaultInjectingBackend::installHelper() { return inner_->installHelper(); }
Result<void> FaultInjectingBackend::uninstallHelper() { return inner_->uninstallHelper(); }

std::string FaultInjectingBackend::backendDescription() const {
    return "FaultInjectingBackend wrapping " + inner_->backendDescription();
}

Result<std::vector<proto_ns::DriveInfo>> FaultInjectingBackend::listDrivesNow() {
    return inner_->listDrivesNow();
}
Result<void> FaultInjectingBackend::subscribeDrives(DriveChangeCallback cb) {
    return inner_->subscribeDrives(std::move(cb));
}
Result<void> FaultInjectingBackend::unsubscribeDrives() {
    return inner_->unsubscribeDrives();
}

Result<proto_ns::DeviceLimits> FaultInjectingBackend::queryDeviceLimits(
    const proto_ns::SessionId& sid) { return inner_->queryDeviceLimits(sid); }

Result<void> FaultInjectingBackend::prepareDevice(
    const proto_ns::SessionId& sid, const proto_ns::PrepareOptions& opts) {
    return inner_->prepareDevice(sid, opts);
}

Result<Slot> FaultInjectingBackend::acquireSlot(
    const proto_ns::SessionId& sid) { return inner_->acquireSlot(sid); }

Result<std::size_t> FaultInjectingBackend::readChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    std::byte* buf, std::size_t len) {
    return inner_->readChunk(sid, offset, buf, len);
}

Result<std::size_t> FaultInjectingBackend::writeChunk(
    const proto_ns::SessionId& sid, std::uint64_t offset,
    const std::byte* buf, std::size_t len) {
    return inner_->writeChunk(sid, offset, buf, len);
}

Result<void> FaultInjectingBackend::syncDevice(
    const proto_ns::SessionId& sid) { return inner_->syncDevice(sid); }

Result<proto_ns::SessionStats> FaultInjectingBackend::closeSession(
    const proto_ns::SessionId& sid) { return inner_->closeSession(sid); }

Result<void> FaultInjectingBackend::unmount(const std::string& device_path) {
    return inner_->unmount(device_path);
}

Result<void> FaultInjectingBackend::eject(const std::string& device_path) {
    return inner_->eject(device_path);
}

// ---- Methods that may inject ---------------------------------------------

Result<proto_ns::SessionId> FaultInjectingBackend::openSession(
    const std::string& device_path, const proto_ns::OpenOptions& opts) {

    proto_ns::ErrorCode fail_code;
    {
        std::lock_guard<std::mutex> lk(plan_mutex_);
        fail_code = plan_.fail_open_with;
    }
    if (fail_code != proto_ns::ERROR_NONE) {
        return Result<proto_ns::SessionId>::failure(
            makeError(fail_code, "FaultInjectingBackend: openSession injected failure"));
    }
    return inner_->openSession(device_path, opts);
}

void FaultInjectingBackend::submitWrite(const proto_ns::SessionId& sid,
                                         std::uint32_t slot_index,
                                         std::uint64_t offset,
                                         std::uint64_t length,
                                         WriteCompleteCallback on_complete) {
    // Snapshot the plan once for this submission.
    Plan local_plan;
    {
        std::lock_guard<std::mutex> lk(plan_mutex_);
        local_plan = plan_;
    }

    const std::uint32_t my_count = submit_count_.fetch_add(1) + 1;

    // Decide whether to inject. The first failing write is `fail_write_at`;
    // subsequent failures fire every `fail_every_n_after` after that.
    bool should_fail = false;
    if (local_plan.fail_write_at != 0 && my_count == local_plan.fail_write_at) {
        should_fail = true;
    } else if (local_plan.fail_every_n_after != 0
               && local_plan.fail_write_at != 0
               && my_count > local_plan.fail_write_at) {
        const std::uint32_t delta = my_count - local_plan.fail_write_at;
        if (delta % local_plan.fail_every_n_after == 0) should_fail = true;
    }

    if (local_plan.added_write_latency.count() > 0) {
        std::this_thread::sleep_for(local_plan.added_write_latency);
    }

    // Always forward to the inner backend so slot lifecycle (release on
    // completion) is owned in one place. If injection is requested, rewrite
    // the successful completion into a synthetic failure before invoking
    // the caller's callback. The inner pwrite still happens - that's
    // acceptable for tests; we're exercising error propagation in the
    // upper-half pipeline, not the device's view of the data.
    if (should_fail) {
        const auto fail_code   = local_plan.fail_write_code;
        const auto fail_errno  = local_plan.fail_write_kernel_errno;
        const auto fail_detail = local_plan.fail_write_detail;
        auto rewriting_cb = [fail_code, fail_errno, fail_detail,
                             user_cb = std::move(on_complete)]
                            (const proto_ns::WriteResult& r) mutable {
            proto_ns::WriteResult rewritten = r;
            rewritten.set_bytes_written(0);
            *rewritten.mutable_error() = makeError(fail_code, fail_detail, fail_errno);
            if (user_cb) user_cb(rewritten);
        };
        inner_->submitWrite(sid, slot_index, offset, length, std::move(rewriting_cb));
        return;
    }

    inner_->submitWrite(sid, slot_index, offset, length, std::move(on_complete));
}

} // namespace rpi_imager::privileged::backends
