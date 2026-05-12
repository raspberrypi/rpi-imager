// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "disk_arbitration.h"

#import <CoreFoundation/CoreFoundation.h>
#import <DiskArbitration/DiskArbitration.h>
#import <Foundation/Foundation.h>
#include <os/log.h>

#include <chrono>
#include <thread>

namespace rpi_imager::writer::da {

namespace {

thread_local std::int32_t g_last_status = 0;
thread_local std::string  g_last_detail;

os_log_t writer_log() {
    static os_log_t log = os_log_create("com.raspberrypi.rpi-imager.writer", "da");
    return log;
}

struct DiskOpContext {
    Result result = Result::Success;
    std::int32_t status = 0;
    std::string detail;
    bool completed = false;
};

Result translateDissenter(DADissenterRef dissenter, std::int32_t* out_status,
                           std::string* out_detail) {
    if (!dissenter) {
        if (out_status) *out_status = 0;
        if (out_detail) out_detail->clear();
        return Result::Success;
    }

    DAReturn status = DADissenterGetStatus(dissenter);
    if (out_status) *out_status = static_cast<std::int32_t>(status);

    CFStringRef statusString = DADissenterGetStatusString(dissenter);
    if (out_detail) {
        if (statusString) {
            char buf[256] = {0};
            CFStringGetCString(statusString, buf, sizeof(buf), kCFStringEncodingUTF8);
            out_detail->assign(buf);
        } else {
            *out_detail = "DiskArbitration error " + std::to_string(status);
        }
    }
    os_log_error(writer_log(), "DiskArbitration error: status=%#x", status);

    switch (status) {
        case kDAReturnSuccess:
            return Result::Success;
        case kDAReturnBadArgument:
        case kDAReturnNotFound:
            return Result::InvalidDrive;
        case kDAReturnNotPermitted:
        case kDAReturnNotPrivileged:
            return Result::AccessDenied;
        case kDAReturnBusy:
        case kDAReturnExclusiveAccess:
            return Result::Busy;
        default:
            return Result::Error;
    }
}

void unmountCallback(DADiskRef /*disk*/, DADissenterRef dissenter, void* context) {
    auto* ctx = static_cast<DiskOpContext*>(context);
    ctx->result = translateDissenter(dissenter, &ctx->status, &ctx->detail);
    ctx->completed = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

void ejectCallback(DADiskRef /*disk*/, DADissenterRef dissenter, void* context) {
    auto* ctx = static_cast<DiskOpContext*>(context);
    ctx->result = translateDissenter(dissenter, &ctx->status, &ctx->detail);
    ctx->completed = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

void ejectAfterUnmountCallback(DADiskRef disk, DADissenterRef dissenter, void* context) {
    auto* ctx = static_cast<DiskOpContext*>(context);
    if (dissenter) {
        ctx->result = translateDissenter(dissenter, &ctx->status, &ctx->detail);
        ctx->completed = true;
        CFRunLoopStop(CFRunLoopGetCurrent());
    } else {
        DADiskEject(disk, kDADiskEjectOptionDefault, ejectCallback, context);
    }
}

// Strip /dev/ prefix and validate it's a sane disk identifier. DA wants
// the BSD name (e.g. "disk7") not the full path. Reject anything that
// looks like a path traversal attempt.
bool deviceToBsdName(const std::string& device_path, std::string& bsd) {
    constexpr std::string_view kPrefix = "/dev/";
    if (device_path.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    bsd = device_path.substr(kPrefix.size());
    if (bsd.empty() || bsd.find('/') != std::string::npos) {
        return false;
    }
    // /dev/rdiskN is the raw character device; DA wants the block device
    // /dev/diskN, so strip the leading 'r' if present.
    if (bsd.size() > 1 && bsd[0] == 'r' && bsd.compare(0, 5, "rdisk") == 0) {
        bsd = bsd.substr(1);
    }
    return true;
}

Result runDiskOperation(const std::string& device_path,
                         DADiskUnmountCallback callback,
                         int max_retries = 12) {
    std::string bsd;
    if (!deviceToBsdName(device_path, bsd)) {
        g_last_status = 0;
        g_last_detail = "invalid device path: " + device_path;
        return Result::InvalidDrive;
    }

    DiskOpContext context;
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session) {
        g_last_status = 0;
        g_last_detail = "failed to create DA session";
        return Result::Error;
    }

    DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, bsd.c_str());
    if (!disk) {
        CFRelease(session);
        g_last_status = 0;
        g_last_detail = "failed to create disk object for " + device_path;
        return Result::InvalidDrive;
    }

    DADiskUnmount(disk,
                  kDADiskUnmountOptionWhole | kDADiskUnmountOptionForce,
                  callback, &context);

    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    int retries = 0;
    while (!context.completed && retries < max_retries * 100) {
        SInt32 status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
        if (status == kCFRunLoopRunStopped || status == kCFRunLoopRunFinished) {
            break;
        }
        retries++;
    }

    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRelease(disk);
    CFRelease(session);

    if (!context.completed) {
        g_last_status = 0;
        g_last_detail = "timeout";
        return Result::Busy;
    }

    g_last_status = context.status;
    g_last_detail = std::move(context.detail);
    return context.result;
}

}  // namespace

Result unmountDisk(const std::string& device_path) {
    return runDiskOperation(device_path, unmountCallback);
}

Result ejectDisk(const std::string& device_path) {
    return runDiskOperation(device_path, ejectAfterUnmountCallback);
}

std::int32_t lastKernelStatus() { return g_last_status; }
std::string  lastErrorDetail()  { return g_last_detail; }

} // namespace rpi_imager::writer::da
