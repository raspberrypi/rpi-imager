// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "macos_xpc.h"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>
#import "../../writer/server/mac/RpiImagerWriterProtocol.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <sys/mman.h>
#include <unistd.h>

// File-scope ObjC types (must live outside the C++ namespace).
//
// §5 push-notification delegate. The helper invokes its
// driveListChanged method via the connection's exportedObject when
// DiskArbitration sees an appearance/disappearance. We forward to the
// user-supplied callback held by the C++ side.
@interface RpiImagerDriveSubDelegate : NSObject <RpiImagerWriterClientProtocol>
- (instancetype)initWithCallback:
        (std::function<void(const rpi_imager::privileged::proto::DriveChange&)>)cb;
@end

@implementation RpiImagerDriveSubDelegate {
    std::function<void(const rpi_imager::privileged::proto::DriveChange&)> _cb;
}
- (instancetype)initWithCallback:
        (std::function<void(const rpi_imager::privileged::proto::DriveChange&)>)cb {
    if ((self = [super init])) { _cb = std::move(cb); }
    return self;
}
- (void)driveListChanged {
    // Pull-on-push design: the helper has signalled a topology change.
    // We don't know which device specifically - the caller re-runs
    // listDevicesNow to get a fresh snapshot. Deliver a sentinel change
    // with kind=KIND_UPDATED and an empty DriveInfo so existing
    // subscribers don't have to special-case nil payloads.
    if (_cb) {
        rpi_imager::privileged::proto::DriveChange dc;
        dc.set_kind(rpi_imager::privileged::proto::DriveChange::KIND_UPDATED);
        _cb(dc);
    }
}
@end

namespace rpi_imager::privileged::backends {

namespace proto = rpi_imager::privileged::proto;

namespace {

proto::ErrorInfo makeError(proto::ErrorCode code, const std::string& detail,
                             int kernel_status = 0) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    e.set_kernel_errno(kernel_status);
    return e;
}

template <typename T>
Result<T> notImplementedT(const char* op) {
    return Result<T>::failure(
        makeError(proto::ERROR_NOT_IMPLEMENTED,
                  std::string("MacOSXpcBackend::") + op + " not yet implemented"));
}

Result<void> notImplemented(const char* op) {
    return Result<void>::failure(
        makeError(proto::ERROR_NOT_IMPLEMENTED,
                  std::string("MacOSXpcBackend::") + op + " not yet implemented"));
}

} // namespace

struct MacOSXpcBackend::State {
    Options opts;
    NSXPCConnection* connection = nil;
    std::mutex connection_mutex;
    std::atomic<bool> invalidated{false};
    std::string helper_version;

    // §5 push-notification delegate. Set when subscribeDrives is
    // called; the helper invokes its driveListChanged method via the
    // connection's exportedObject. Holds the user-supplied callback.
    RpiImagerDriveSubDelegate* drive_sub_delegate = nil;
    bool drive_sub_active = false;

    // Maps proto::SessionId.v -> the helper-side opaque token returned
    // by RpiImagerWriterProtocol.openDevice. Local to the client side.
    std::mutex                                  session_mutex;
    std::unordered_map<std::uint64_t,
                        std::uint64_t>           sessions;
    std::atomic<std::uint64_t>                  next_session_id{1};

    // Bulk-write shared-memory buffer (set by mapBulkBuffer).
    int           bulk_fd = -1;
    void*         bulk_base = nullptr;
    std::size_t   bulk_size = 0;

    NSXPCConnection* ensureConnection() {
        std::lock_guard<std::mutex> lk(connection_mutex);
        if (connection && !invalidated.load()) {
            return connection;
        }

        // Tear down any stale connection first.
        if (connection) {
            [connection invalidate];
            connection = nil;
        }
        invalidated.store(false);

        NSString* serviceName = opts.mach_service_name.empty()
            ? RpiImagerWriterServiceName
            : [NSString stringWithUTF8String:opts.mach_service_name.c_str()];

        NSXPCConnection* conn =
            [[NSXPCConnection alloc] initWithMachServiceName:serviceName options:0];
        conn.remoteObjectInterface =
            [NSXPCInterface interfaceWithProtocol:@protocol(RpiImagerWriterProtocol)];

        // Capture state pointer to flip the invalidation flag from the
        // connection's invalidation handler. We deliberately use a weak
        // capture-equivalent via the atomic - the State outlives any in-
        // flight connection by virtue of being owned by the backend.
        std::atomic<bool>* flag = &invalidated;
        conn.invalidationHandler = ^{
            flag->store(true);
        };
        conn.interruptionHandler = ^{
            flag->store(true);
        };

        [conn resume];
        connection = conn;
        return connection;
    }
};

MacOSXpcBackend::MacOSXpcBackend() : MacOSXpcBackend(Options{}) {}

MacOSXpcBackend::MacOSXpcBackend(Options opts)
    : state_(std::make_unique<State>()) {
    state_->opts = std::move(opts);
}

MacOSXpcBackend::~MacOSXpcBackend() {
    if (state_) {
        if (state_->bulk_base) {
            ::munmap(state_->bulk_base, state_->bulk_size);
            state_->bulk_base = nullptr;
        }
        if (state_->bulk_fd >= 0) {
            ::close(state_->bulk_fd);
            state_->bulk_fd = -1;
        }
        if (state_->connection) {
            [state_->connection invalidate];
            state_->connection = nil;
        }
    }
}

std::string MacOSXpcBackend::backendDescription() const {
    return "MacOSXpcBackend (NSXPC -> rpi-imager-writer)";
}

// ----- Helper lifecycle -----------------------------------------------------

Result<proto::HelperStatus> MacOSXpcBackend::queryHelperStatus() {
    @autoreleasepool {
        // Read the SMAppService status FIRST so we can distinguish
        // "never registered" from "user disabled in Settings" from
        // "registered and ready". The ping check that follows confirms
        // the helper is actually responsive (covers transient launchd
        // failures), but the SMAppService status is the source of
        // truth for the user-facing UX decisions.
        proto::HelperStatus pre_status;
        pre_status.set_state(proto::HELPER_STATE_UNKNOWN);
        pre_status.set_client_version("phase-1b-poc");
        if (@available(macOS 13.0, *)) {
            SMAppService* svc = [SMAppService daemonServiceWithPlistName:
                @"com.raspberrypi.rpi-imager.writer.plist"];
            if (svc) {
                switch (svc.status) {
                    case SMAppServiceStatusNotRegistered:
                        pre_status.set_state(proto::HELPER_STATE_NOT_INSTALLED);
                        break;
                    case SMAppServiceStatusRequiresApproval:
                        // Registered, but the user hasn't confirmed in
                        // System Settings yet. Treat as disabled so the
                        // UI surfaces the right call-to-action.
                        pre_status.set_state(proto::HELPER_STATE_INSTALLED_DISABLED);
                        break;
                    case SMAppServiceStatusEnabled:
                        // Looks good - the ping below confirms it.
                        break;
                    case SMAppServiceStatusNotFound:
                    default:
                        pre_status.set_state(proto::HELPER_STATE_NOT_INSTALLED);
                        break;
                }
            }
        } else {
            pre_status.set_state(proto::HELPER_STATE_NOT_INSTALLED);
            return Result<proto::HelperStatus>::success(std::move(pre_status));
        }
        // If the system reports the service is not-enabled there is no
        // point in attempting the ping; the connection will hang for
        // the full handshake timeout. Return early with the precise
        // state so the UI can act on it.
        if (pre_status.state() == proto::HELPER_STATE_NOT_INSTALLED ||
            pre_status.state() == proto::HELPER_STATE_INSTALLED_DISABLED) {
            return Result<proto::HelperStatus>::success(std::move(pre_status));
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<proto::HelperStatus>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "could not create XPC connection"));
        }

        // Use a heap-allocated synchronisation block so the lambdas can
        // safely reference it. ObjC blocks capture by-value-and-const by
        // default; std::mutex / std::condition_variable aren't copyable
        // and we want the same instance shared between the proxy block,
        // the error handler, and the waiting thread.
        struct PingState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::string version;
            bool connection_error = false;
            std::string err_detail;
        };
        auto sync = std::make_shared<PingState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy pingWithReply:^(NSString* helperVersion) {
            std::lock_guard<std::mutex> lk(sync->m);
            if (helperVersion) sync->version = helperVersion.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        const auto timeout = std::chrono::milliseconds(
            state_->opts.handshake_timeout_ms == 0 ? 5000
                                                    : state_->opts.handshake_timeout_ms);
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, timeout, [&] { return sync->done; })) {
            return Result<proto::HelperStatus>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "helper handshake timed out"));
        }

        if (sync->connection_error) {
            return Result<proto::HelperStatus>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "XPC error: " + sync->err_detail));
        }

        state_->helper_version = sync->version;
        proto::HelperStatus s;
        s.set_state(proto::HELPER_STATE_INSTALLED_READY);
        s.set_installed_version(sync->version);
        s.set_client_version("phase-1b-poc");
        return Result<proto::HelperStatus>::success(std::move(s));
    }
}

Result<void> MacOSXpcBackend::installHelper() {
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService* service = [SMAppService daemonServiceWithPlistName:
                @"com.raspberrypi.rpi-imager.writer.plist"];
            if (!service) {
                return Result<void>::failure(makeError(
                    proto::ERROR_HELPER_INSTALL_REJECTED,
                    "SMAppService.daemonServiceWithPlistName returned nil "
                    "(plist missing from Contents/Library/LaunchDaemons?)"));
            }

            NSError* err = nil;
            BOOL ok = [service registerAndReturnError:&err];
            if (!ok) {
                std::string detail = "SMAppService.register() failed";
                int kerr = 0;
                if (err) {
                    detail += ": ";
                    detail += err.localizedDescription.UTF8String;
                    kerr = static_cast<int>(err.code);
                }
                return Result<void>::failure(makeError(
                    proto::ERROR_HELPER_INSTALL_REJECTED, detail, kerr));
            }
            return Result<void>::success();
        }
        return Result<void>::failure(makeError(
            proto::ERROR_HELPER_INSTALL_REJECTED,
            "SMAppService requires macOS 13 (Ventura) or later"));
    }
}

Result<void> MacOSXpcBackend::uninstallHelper() {
    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            SMAppService* service = [SMAppService daemonServiceWithPlistName:
                @"com.raspberrypi.rpi-imager.writer.plist"];
            if (!service) {
                // Treat "no plist" as already-uninstalled.
                return Result<void>::success();
            }
            NSError* err = nil;
            BOOL ok = [service unregisterAndReturnError:&err];
            if (!ok && err && err.code != 0) {
                std::string detail = err.localizedDescription.UTF8String;
                return Result<void>::failure(makeError(
                    proto::ERROR_UNKNOWN, "uninstall failed: " + detail));
            }
            return Result<void>::success();
        }
        return Result<void>::success();
    }
}

// ----- Drive enumeration ---------------------------------------------------

Result<std::vector<proto::DriveInfo>> MacOSXpcBackend::listDrivesNow() {
    // §5: PAL-typed enumeration. Implemented by translating the
    // concrete-class result into proto::DriveInfo. Most callers in the
    // imager talk directly to listDevicesNow() and consume the
    // DeviceDescriptor, but the cross-platform PAL path lives here so
    // native Linux/Windows backends can use a uniform call site.
    auto rich = listDevicesNow();
    if (!rich.ok) {
        return Result<std::vector<proto::DriveInfo>>::failure(rich.error);
    }
    std::vector<proto::DriveInfo> out;
    out.reserve(rich.value.size());
    for (const auto& d : rich.value) {
        proto::DriveInfo info;
        info.set_device_path(d.device);
        info.set_display_name(d.description);
        info.set_size_bytes(d.size);
        info.set_is_removable(d.isRemovable);
        info.set_is_usb(d.isUSB);
        info.set_is_system_drive(d.isSystem);
        info.set_logical_block_size(d.logicalBlockSize);
        for (std::size_t i = 0; i < d.mountpoints.size(); ++i) {
            auto* mv = info.add_mounted_volumes();
            mv->set_mount_path(d.mountpoints[i]);
            if (i < d.mountpointLabels.size()) {
                mv->set_filesystem(d.mountpointLabels[i]);
            }
        }
        out.push_back(std::move(info));
    }
    return Result<std::vector<proto::DriveInfo>>::success(std::move(out));
}

Result<std::vector<Drivelist::DeviceDescriptor>>
MacOSXpcBackend::listDevicesNow() {
    @autoreleasepool {
        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "could not create XPC connection"));
        }

        struct ListState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool connection_error = false;
            std::string err_detail;
            NSArray<NSDictionary*>* devices = nil;
        };
        auto sync = std::make_shared<ListState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy listDrivesNow:^(NSArray<NSDictionary*>* _Nullable devices,
                               NSString* _Nullable detail) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->devices = devices;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        // DiskArbitration enumeration usually completes in <100ms but
        // pathological cases (slow USB enumeration, hung volumes) can
        // take longer. 5s is generous without locking up the UI thread
        // if the helper truly hangs.
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(5),
                                 [&] { return sync->done; })) {
            return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(
                makeError(proto::ERROR_UNKNOWN, "listDrivesNow timed out"));
        }
        if (sync->connection_error) {
            return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "XPC error: " + sync->err_detail));
        }
        if (!sync->devices) {
            return Result<std::vector<Drivelist::DeviceDescriptor>>::failure(
                makeError(proto::ERROR_UNKNOWN,
                          "listDrivesNow: " + sync->err_detail));
        }

        // Translate NSArray<NSDictionary> -> vector<DeviceDescriptor>.
        std::vector<Drivelist::DeviceDescriptor> out;
        out.reserve(sync->devices.count);

        auto strs = [](id v) -> std::vector<std::string> {
            std::vector<std::string> r;
            if (![v isKindOfClass:[NSArray class]]) return r;
            for (NSString* s in (NSArray*)v) {
                if ([s isKindOfClass:[NSString class]]) {
                    r.emplace_back(s.UTF8String);
                }
            }
            return r;
        };
        auto cstr = [](id v) -> std::string {
            if ([v isKindOfClass:[NSString class]]) {
                return std::string(((NSString*)v).UTF8String);
            }
            return {};
        };
        auto u64 = [](id v) -> std::uint64_t {
            if ([v isKindOfClass:[NSNumber class]]) {
                return ((NSNumber*)v).unsignedLongLongValue;
            }
            return 0;
        };
        auto u32 = [](id v) -> std::uint32_t {
            if ([v isKindOfClass:[NSNumber class]]) {
                return ((NSNumber*)v).unsignedIntValue;
            }
            return 0;
        };
        auto boolv = [](id v) -> bool {
            if ([v isKindOfClass:[NSNumber class]]) {
                return ((NSNumber*)v).boolValue;
            }
            return false;
        };

        for (NSDictionary* d in sync->devices) {
            Drivelist::DeviceDescriptor dd;
            dd.device           = cstr(d[@"device"]);
            dd.raw              = cstr(d[@"raw"]);
            dd.description      = cstr(d[@"description"]);
            dd.size             = u64(d[@"size"]);
            dd.blockSize        = u32(d[@"blockSize"]);
            dd.logicalBlockSize = u32(d[@"logicalBlockSize"]);
            dd.busType          = cstr(d[@"busType"]);
            dd.busVersion       = cstr(d[@"busVersion"]);
            dd.enumerator       = cstr(d[@"enumerator"]);
            dd.devicePath       = cstr(d[@"devicePath"]);
            dd.parentDevice     = cstr(d[@"parentDevice"]);
            dd.mountpoints      = strs(d[@"mountpoints"]);
            dd.mountpointLabels = strs(d[@"mountpointLabels"]);
            dd.childDevices     = strs(d[@"childDevices"]);
            dd.isReadOnly       = boolv(d[@"isReadOnly"]);
            dd.isSystem         = boolv(d[@"isSystem"]);
            dd.isVirtual        = boolv(d[@"isVirtual"]);
            dd.isRemovable      = boolv(d[@"isRemovable"]);
            dd.isCard           = boolv(d[@"isCard"]);
            dd.isSCSI           = boolv(d[@"isSCSI"]);
            dd.isUSB            = boolv(d[@"isUSB"]);
            dd.isUAS            = boolv(d[@"isUAS"]);
            dd.busVersionNull   = boolv(d[@"busVersionNull"]);
            dd.devicePathNull   = boolv(d[@"devicePathNull"]);
            dd.isUASNull        = boolv(d[@"isUASNull"]);
            out.push_back(std::move(dd));
        }
        return Result<std::vector<Drivelist::DeviceDescriptor>>::success(
            std::move(out));
    }
}

Result<void> MacOSXpcBackend::subscribeDrives(DriveChangeCallback cb) {
    @autoreleasepool {
        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        // Wire the client-facing direction of the connection (helper ->
        // us). Must be done BEFORE the subscribe call so the helper can
        // immediately invoke driveListChanged on its remoteObjectProxy.
        conn.exportedInterface = [NSXPCInterface
            interfaceWithProtocol:@protocol(RpiImagerWriterClientProtocol)];
        state_->drive_sub_delegate =
            [[RpiImagerDriveSubDelegate alloc] initWithCallback:std::move(cb)];
        conn.exportedObject = state_->drive_sub_delegate;

        struct SubState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            bool connection_error = false;
            std::string err_detail;
        };
        auto sync = std::make_shared<SubState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy subscribeDriveChangesWithReply:^(BOOL ok, NSString* _Nullable detail) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->ok = ok;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(5),
                                 [&] { return sync->done; })) {
            return Result<void>::failure(makeError(
                proto::ERROR_UNKNOWN, "subscribeDrives timed out"));
        }
        if (sync->connection_error) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (!sync->ok) {
            return Result<void>::failure(makeError(
                proto::ERROR_UNKNOWN,
                "subscribeDrives failed: " + sync->err_detail));
        }
        state_->drive_sub_active = true;
        return Result<void>::success();
    }
}

Result<void> MacOSXpcBackend::unsubscribeDrives() {
    @autoreleasepool {
        if (!state_->drive_sub_active) return Result<void>::success();
        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct UnsubState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
        };
        auto sync = std::make_shared<UnsubState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* /*error*/) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy unsubscribeDriveChangesWithReply:^(BOOL /*wasSubscribed*/) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        sync->cv.wait_for(lk, std::chrono::seconds(2),
                            [&] { return sync->done; });
        state_->drive_sub_active = false;
        state_->drive_sub_delegate = nil;
        return Result<void>::success();
    }
}

// ----- Sessions / bulk plane (§6) -----------------------------------------
//
// openSession/queryDeviceLimits/prepareDevice/read/write/sync/hash/close
// are implemented below. The slot-based acquireSlot/submitWrite PAL methods
// are NOT used by this backend - the bulk-write path goes through the
// concrete-class mapBulkBuffer/submitBulkWriteAsync API instead.

Result<proto::SessionId> MacOSXpcBackend::openSession(
    const std::string& device_path, const proto::OpenOptions&) {
    @autoreleasepool {
        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<proto::SessionId>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        NSString* path = [NSString stringWithUTF8String:device_path.c_str()];
        struct OpenState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::uint64_t token = 0;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<OpenState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy openDevice:path
                    reply:^(uint64_t token, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->token = token;
            sync->kernel_errno = kerrno;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(30),
                                 [&] { return sync->done; })) {
            return Result<proto::SessionId>::failure(makeError(
                proto::ERROR_DEVICE_BUSY, "openDevice timed out"));
        }
        if (sync->connection_error) {
            return Result<proto::SessionId>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error during openDevice: " + sync->err_detail));
        }
        if (sync->token == 0) {
            return Result<proto::SessionId>::failure(makeError(
                proto::ERROR_DEVICE_PERMISSION,
                "openDevice failed: " + sync->err_detail,
                sync->kernel_errno));
        }

        // Map our local SessionId.v -> helper token.
        std::uint64_t our_id = state_->next_session_id.fetch_add(1);
        {
            std::lock_guard<std::mutex> lk2(state_->session_mutex);
            state_->sessions[our_id] = sync->token;
        }
        proto::SessionId sid;
        sid.set_v(our_id);
        return Result<proto::SessionId>::success(std::move(sid));
    }
}

Result<proto::DeviceLimits> MacOSXpcBackend::queryDeviceLimits(
    const proto::SessionId& sid) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<proto::DeviceLimits>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "queryDeviceLimits: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<proto::DeviceLimits>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct SizeState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::uint64_t size_bytes = 0;
            std::string err_detail;
            bool connection_error = false;
        };
        auto sync = std::make_shared<SizeState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy queryDeviceSize:token
                          reply:^(uint64_t size, NSString* _Nullable detail) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->size_bytes = size;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(10),
                                 [&] { return sync->done; })) {
            return Result<proto::DeviceLimits>::failure(makeError(
                proto::ERROR_DEVICE_IO, "queryDeviceSize timed out"));
        }
        if (sync->connection_error) {
            return Result<proto::DeviceLimits>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (sync->size_bytes == 0 && !sync->err_detail.empty()) {
            return Result<proto::DeviceLimits>::failure(makeError(
                proto::ERROR_DEVICE_IO,
                "queryDeviceSize failed: " + sync->err_detail));
        }

        proto::DeviceLimits limits;
        limits.set_total_bytes(sync->size_bytes);
        // Block size + max transfer left at default for now; the write
        // path doesn't need them, and they aren't surfaced over XPC yet.
        limits.set_logical_block_size(512);
        limits.set_max_transfer_bytes(0);
        return Result<proto::DeviceLimits>::success(std::move(limits));
    }
}
Result<void> MacOSXpcBackend::prepareDevice(const proto::SessionId& sid,
                                              const proto::PrepareOptions& opts) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<void>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "prepareDevice: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct PrepState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<PrepState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy prepareDevice:token
                 zeroFirstMb:(opts.zero_first_mb() ? YES : NO)
                  zeroLastMb:(opts.zero_last_mb()  ? YES : NO)
                       reply:^(BOOL ok, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->ok = ok;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        // Last-MB scrub on large SD cards can take many seconds; give
        // ample headroom before declaring the helper unresponsive.
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::minutes(5),
                                 [&] { return sync->done; })) {
            return Result<void>::failure(makeError(
                proto::ERROR_WRITE_FAILED, "prepareDevice timed out"));
        }
        if (sync->connection_error) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (!sync->ok) {
            return Result<void>::failure(makeError(
                proto::ERROR_WRITE_FAILED,
                "prepareDevice failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        return Result<void>::success();
    }
}
Result<Slot> MacOSXpcBackend::acquireSlot(const proto::SessionId&) {
    return notImplementedT<Slot>("acquireSlot");
}
void MacOSXpcBackend::submitWrite(const proto::SessionId& sid,
                                    std::uint32_t slot_index,
                                    std::uint64_t offset,
                                    std::uint64_t length,
                                    WriteCompleteCallback on_complete) {
    // The slot-based submitWrite PAL method is not used against this
    // backend: the high-throughput path is the concrete-class shared-
    // memory ring (mapBulkBuffer/submitBulkWriteAsync). This entry point
    // exists only to satisfy the interface and surfaces a clear error.
    @autoreleasepool {
        if (!on_complete) return;

        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                proto::WriteResult wr;
                wr.set_slot_index(slot_index);
                *wr.mutable_error() = makeError(proto::ERROR_SESSION_NOT_FOUND,
                                                "submitWrite: unknown session id");
                on_complete(wr);
                return;
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            proto::WriteResult wr;
            wr.set_slot_index(slot_index);
            *wr.mutable_error() = makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                                            "could not create XPC connection");
            on_complete(wr);
            return;
        }

        // The XPC backend has no PAL-level slot pool; the bulk-write ring
        // is reached via the concrete-class API instead. Surface this as
        // ERROR_NOT_IMPLEMENTED with a clear message. (The diagnostic GUI
        // uses the lower-level openSession + queryDeviceLimits path plus a
        // separate writeBlocking helper - see writeBlockingForDiagnostic.)
        (void)offset; (void)length; (void)token;
        proto::WriteResult wr;
        wr.set_slot_index(slot_index);
        *wr.mutable_error() = makeError(proto::ERROR_NOT_IMPLEMENTED,
            "submitWrite via XPC requires the concrete-class bulk-write ring. "
            "Use mapBulkBuffer/submitBulkWriteAsync or the diagnostic helpers.");
        on_complete(wr);
    }
}

Result<std::size_t> MacOSXpcBackend::readChunk(const proto::SessionId& sid,
                                                 std::uint64_t offset,
                                                 std::byte* buf,
                                                 std::size_t len) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<std::size_t>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "readChunk: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct ReadState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            NSData* data = nil;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<ReadState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy readAt:token offset:offset length:len
                reply:^(NSData* _Nullable data, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->data = data;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(30),
                                 [&] { return sync->done; })) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_DEVICE_IO, "readAt timed out"));
        }
        if (sync->connection_error) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (sync->data == nil) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_DEVICE_IO,
                "readAt failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        std::size_t copied = std::min(len, (std::size_t)sync->data.length);
        std::memcpy(buf, sync->data.bytes, copied);
        return Result<std::size_t>::success(copied);
    }
}
Result<std::size_t> MacOSXpcBackend::writeChunk(const proto::SessionId& sid,
                                                  std::uint64_t offset,
                                                  const std::byte* buf,
                                                  std::size_t len) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<std::size_t>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "writeChunk: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        NSData* payload = [NSData dataWithBytes:buf length:len];

        struct WriteState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::uint64_t bytes_written = 0;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<WriteState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy writeAt:token offset:offset data:payload
                 reply:^(uint64_t wrote, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->bytes_written = wrote;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(30),
                                 [&] { return sync->done; })) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_WRITE_FAILED, "writeAt timed out"));
        }
        if (sync->connection_error) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (sync->bytes_written == 0 && !sync->err_detail.empty()) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_WRITE_FAILED,
                "writeAt failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        return Result<std::size_t>::success((std::size_t)sync->bytes_written);
    }
}

Result<void> MacOSXpcBackend::syncDevice(const proto::SessionId& sid) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<void>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "syncDevice: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct SyncState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<SyncState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy syncSession:token
                     reply:^(BOOL ok, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->ok = ok;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        // fsync on a multi-GB SD card can be slow - give it generous
        // time before declaring the helper unresponsive.
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::minutes(5),
                                 [&] { return sync->done; })) {
            return Result<void>::failure(makeError(
                proto::ERROR_SYNC_FAILED, "syncSession timed out"));
        }
        if (sync->connection_error) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (!sync->ok) {
            return Result<void>::failure(makeError(
                proto::ERROR_SYNC_FAILED,
                "syncSession failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        return Result<void>::success();
    }
}
Result<std::string> MacOSXpcBackend::hashDeviceSha256(
    const proto::SessionId& sid,
    const std::string& prefix,
    std::uint64_t device_offset,
    std::uint64_t length) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<std::string>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "hashDeviceSha256: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<std::string>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct HashState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::string digest;
            std::uint64_t bytes_hashed = 0;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<HashState>();

        NSData* prefixData = prefix.empty()
            ? nil
            : [NSData dataWithBytes:prefix.data() length:prefix.size()];

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy hashDeviceSha256:token
                    prefixBytes:prefixData
                   deviceOffset:device_offset
                         length:length
                          reply:^(NSData* _Nullable digestData,
                                  uint64_t bytesHashed,
                                  NSString* _Nullable detail,
                                  int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            if (digestData && digestData.length == 32) {
                sync->digest.assign(
                    (const char*)digestData.bytes, digestData.length);
            }
            sync->bytes_hashed = bytesHashed;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        // Hashing a 5+ GB device at 100 MB/s takes ~50s; give an
        // upper bound that tolerates slow flash without leaving the
        // client hung if the helper genuinely dies.
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::minutes(30),
                                 [&] { return sync->done; })) {
            return Result<std::string>::failure(makeError(
                proto::ERROR_UNKNOWN, "hashDeviceSha256 timed out"));
        }
        if (sync->connection_error) {
            return Result<std::string>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (sync->digest.empty()) {
            return Result<std::string>::failure(makeError(
                proto::ERROR_DEVICE_IO,
                "hashDeviceSha256 failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        return Result<std::string>::success(std::move(sync->digest));
    }
}

Result<proto::SessionStats> MacOSXpcBackend::closeSession(
    const proto::SessionId& sid) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<proto::SessionStats>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "closeSession: unknown session id"));
            }
            token = it->second;
            state_->sessions.erase(it);
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<proto::SessionStats>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct CloseState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            bool connection_error = false;
            std::string err_detail;
            // §7a payload dictionary (retained until backend processes it).
            NSDictionary* stats = nil;
        };
        auto sync = std::make_shared<CloseState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy closeSession:token reply:^(BOOL ok, NSDictionary* _Nullable stats) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->ok = ok;
            sync->stats = stats;  // ARC retains for our lifetime
            sync->done = true;
            sync->cv.notify_one();
        }];

        // fsync of the final ring drain can take a long time on slow
        // flash; give 60s before declaring the helper unresponsive on
        // the close path.
        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(60),
                                 [&] { return sync->done; })) {
            return Result<proto::SessionStats>::failure(makeError(
                proto::ERROR_UNKNOWN, "closeSession timed out"));
        }

        // Translate the helper's NSDictionary into proto::SessionStats.
        // Unknown keys are tolerated for forward compatibility - the
        // helper may add fields at higher schema versions while older
        // clients keep working with the v1 subset.
        proto::SessionStats stats;
        stats.set_state(sync->ok ? proto::SESSION_STATE_SUCCEEDED
                                  : proto::SESSION_STATE_FAILED);
        if (NSDictionary* d = sync->stats) {
            auto num = [d](NSString* key) -> std::uint64_t {
                NSNumber* n = d[key];
                return n ? (std::uint64_t)n.unsignedLongLongValue : 0;
            };
            stats.set_bytes_written(num(@"bytesWritten"));
            stats.set_duration_ms(num(@"durationMs"));

            // Per-write latency histogram: min/avg/max, sample count, and
            // (§7a) fixed-edge buckets. Edges arrive alongside counts so we
            // don't hard-code them; mismatched/absent arrays are tolerated.
            auto* hist = stats.mutable_write_latency();
            hist->set_min_us(num(@"minWriteLatencyUs"));
            hist->set_avg_us(num(@"avgWriteLatencyUs"));
            hist->set_max_us(num(@"maxWriteLatencyUs"));
            hist->set_sample_count((std::uint32_t)num(@"writeCount"));

            NSArray* edges = [d[@"writeLatencyBucketUpperUs"] isKindOfClass:[NSArray class]]
                                 ? (NSArray*)d[@"writeLatencyBucketUpperUs"] : nil;
            NSArray* counts = [d[@"writeLatencyBucketCounts"] isKindOfClass:[NSArray class]]
                                  ? (NSArray*)d[@"writeLatencyBucketCounts"] : nil;
            if (edges && counts && edges.count == counts.count) {
                for (NSUInteger i = 0; i < edges.count; ++i) {
                    NSNumber* e = edges[i];
                    NSNumber* c = counts[i];
                    if (![e isKindOfClass:[NSNumber class]] ||
                        ![c isKindOfClass:[NSNumber class]]) {
                        continue;
                    }
                    hist->add_bucket_upper_us((std::uint64_t)e.unsignedLongLongValue);
                    hist->add_bucket_counts((std::uint64_t)c.unsignedLongLongValue);
                }
            }

            // Convert helper-side discrete timings into TimedEvent
            // entries so the existing PerformanceStats event readers
            // can pick them up by name.
            auto add_event = [&](const char* type, std::uint64_t duration_us) {
                if (duration_us == 0) return;
                auto* ev = stats.add_events();
                ev->set_type(type);
                ev->set_start_ms(0);  // unknown without per-call timestamps
                ev->set_duration_ms((duration_us + 500) / 1000);
                ev->set_success(true);
                ev->set_metadata("source:helper");
            };
            add_event("helper.prepareDevice", num(@"prepareDeviceUs"));
            add_event("helper.fsync",          num(@"totalFsyncUs"));
            add_event("helper.hashDevice",     num(@"hashDeviceUs"));
        }
        if (sync->connection_error) {
            stats.set_state(proto::SESSION_STATE_FAILED);
        }
        return Result<proto::SessionStats>::success(std::move(stats));
    }
}

// ----- Maintenance (real XPC roundtrip) ------------------------------------

namespace {

Result<void> doUnmountOrEject(MacOSXpcBackend::State& state,
                               const std::string& device_path,
                               bool eject_after,
                               const char* op_name,
                               proto::ErrorCode default_failure_code) {
    @autoreleasepool {
        NSXPCConnection* conn = state.ensureConnection();
        if (!conn) {
            return Result<void>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          "could not create XPC connection"));
        }

        NSString* path = [NSString stringWithUTF8String:device_path.c_str()];

        struct OpState {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            bool success = false;
            std::string err_detail;
            std::int32_t kernel_status = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<OpState>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        // Only unmount is wired through XPC. eject reuses the unmount
        // endpoint until we add a dedicated method (small, versioned
        // schema change in the protocol).
        (void)eject_after;
        [proxy unmountDevice:path
                       reply:^(BOOL ok, NSString* _Nullable detail, int32_t status) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->success = ok;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_status = status;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(60),
                                 [&] { return sync->done; })) {
            return Result<void>::failure(
                makeError(default_failure_code,
                          std::string(op_name) + " timed out"));
        }

        if (sync->connection_error) {
            return Result<void>::failure(
                makeError(proto::ERROR_HELPER_NOT_INSTALLED,
                          std::string("XPC error during ") + op_name + ": "
                          + sync->err_detail));
        }

        if (!sync->success) {
            return Result<void>::failure(
                makeError(default_failure_code,
                          std::string(op_name) + " failed: " + sync->err_detail,
                          sync->kernel_status));
        }
        return Result<void>::success();
    }
}

} // namespace

Result<void> MacOSXpcBackend::unmount(const std::string& device_path) {
    return doUnmountOrEject(*state_, device_path, /*eject_after=*/false,
                             "unmount", proto::ERROR_UNMOUNT_FAILED);
}

Result<void> MacOSXpcBackend::eject(const std::string& device_path) {
    return doUnmountOrEject(*state_, device_path, /*eject_after=*/true,
                             "eject", proto::ERROR_EJECT_FAILED);
}

// ---------------------------------------------------------------------------
// Bulk-write shared-memory path
// ---------------------------------------------------------------------------

void* MacOSXpcBackend::bulkBufferBase() const { return state_->bulk_base; }
std::size_t MacOSXpcBackend::bulkBufferSize() const { return state_->bulk_size; }

Result<void> MacOSXpcBackend::mapBulkBuffer(const proto::SessionId& sid,
                                              std::size_t size_bytes) {
    @autoreleasepool {
        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<void>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "mapBulkBuffer: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        // Set up the proxy interface to allow NSFileHandle through
        // (NSXPCInterface has to whitelist mutable classes for replies).
        NSXPCInterface* iface =
            [NSXPCInterface interfaceWithProtocol:@protocol(RpiImagerWriterProtocol)];
        NSSet* allowed = [NSSet setWithObjects:[NSFileHandle class], nil];
        [iface setClasses:allowed
              forSelector:@selector(mapBulkBuffer:sizeBytes:reply:)
            argumentIndex:0 ofReply:YES];
        conn.remoteObjectInterface = iface;

        struct St {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            int fd = -1;
            std::string err_detail;
            bool connection_error = false;
        };
        auto sync = std::make_shared<St>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy mapBulkBuffer:token sizeBytes:size_bytes
                       reply:^(NSFileHandle* _Nullable fh, NSString* _Nullable detail) {
            std::lock_guard<std::mutex> lk(sync->m);
            if (fh) sync->fd = ::dup(fh.fileDescriptor);
            if (detail) sync->err_detail = detail.UTF8String;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(10),
                                 [&] { return sync->done; })) {
            return Result<void>::failure(makeError(
                proto::ERROR_UNKNOWN, "mapBulkBuffer timed out"));
        }
        if (sync->connection_error) {
            return Result<void>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }

        // Tear down any prior buffer.
        if (state_->bulk_base) {
            ::munmap(state_->bulk_base, state_->bulk_size);
            state_->bulk_base = nullptr;
            state_->bulk_size = 0;
        }
        if (state_->bulk_fd >= 0) {
            ::close(state_->bulk_fd);
            state_->bulk_fd = -1;
        }

        // size_bytes==0 is "release"; the helper returns no fd and we're
        // done.
        if (size_bytes == 0) {
            return Result<void>::success();
        }

        if (sync->fd < 0) {
            return Result<void>::failure(makeError(
                proto::ERROR_UNKNOWN,
                "mapBulkBuffer: helper returned no fd; "
                + sync->err_detail));
        }

        void* base = ::mmap(NULL, size_bytes,
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED, sync->fd, 0);
        if (base == MAP_FAILED) {
            int e = errno;
            ::close(sync->fd);
            return Result<void>::failure(makeError(
                proto::ERROR_UNKNOWN,
                std::string("client-side mmap: ") + std::strerror(e)));
        }
        state_->bulk_fd = sync->fd;
        state_->bulk_base = base;
        state_->bulk_size = size_bytes;
        return Result<void>::success();
    }
}

Result<std::size_t> MacOSXpcBackend::bulkWriteFromBuffer(
    const proto::SessionId& sid,
    std::uint64_t buffer_offset,
    std::uint64_t device_offset,
    std::size_t length) {
    @autoreleasepool {
        if (!state_->bulk_base || length == 0
            || buffer_offset > state_->bulk_size
            || length > state_->bulk_size - buffer_offset) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_UNKNOWN,
                "bulkWriteFromBuffer: no buffer mapped or out of bounds"));
        }

        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                return Result<std::size_t>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "bulkWriteFromBuffer: unknown session id"));
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection"));
        }

        struct St {
            std::mutex m;
            std::condition_variable cv;
            bool done = false;
            std::uint64_t bytes_written = 0;
            std::string err_detail;
            std::int32_t kernel_errno = 0;
            bool connection_error = false;
        };
        auto sync = std::make_shared<St>();

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::lock_guard<std::mutex> lk(sync->m);
                sync->connection_error = true;
                if (error.localizedDescription) {
                    sync->err_detail = error.localizedDescription.UTF8String;
                }
                sync->done = true;
                sync->cv.notify_one();
            }];

        [proxy bulkWriteFromBuffer:token
                       bufferOffset:buffer_offset
                       deviceOffset:device_offset
                             length:length
                              reply:^(uint64_t wrote, NSString* _Nullable detail, int32_t kerrno) {
            std::lock_guard<std::mutex> lk(sync->m);
            sync->bytes_written = wrote;
            if (detail) sync->err_detail = detail.UTF8String;
            sync->kernel_errno = kerrno;
            sync->done = true;
            sync->cv.notify_one();
        }];

        std::unique_lock<std::mutex> lk(sync->m);
        if (!sync->cv.wait_for(lk, std::chrono::seconds(60),
                                 [&] { return sync->done; })) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_WRITE_FAILED, "bulkWriteFromBuffer timed out"));
        }
        if (sync->connection_error) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "XPC error: " + sync->err_detail));
        }
        if (sync->bytes_written == 0 && !sync->err_detail.empty()) {
            return Result<std::size_t>::failure(makeError(
                proto::ERROR_WRITE_FAILED,
                "bulkWriteFromBuffer failed: " + sync->err_detail,
                sync->kernel_errno));
        }
        return Result<std::size_t>::success((std::size_t)sync->bytes_written);
    }
}

void MacOSXpcBackend::submitBulkWriteAsync(
    const proto::SessionId& sid,
    std::uint64_t buffer_offset,
    std::uint64_t device_offset,
    std::size_t length,
    BulkWriteCallback on_complete) {
    @autoreleasepool {
        if (!on_complete) return;

        if (!state_->bulk_base || length == 0
            || buffer_offset > state_->bulk_size
            || length > state_->bulk_size - buffer_offset) {
            on_complete(Result<std::size_t>::failure(makeError(
                proto::ERROR_UNKNOWN,
                "submitBulkWriteAsync: no buffer mapped or out of bounds")));
            return;
        }

        std::uint64_t token = 0;
        {
            std::lock_guard<std::mutex> lk(state_->session_mutex);
            auto it = state_->sessions.find(sid.v());
            if (it == state_->sessions.end()) {
                on_complete(Result<std::size_t>::failure(makeError(
                    proto::ERROR_SESSION_NOT_FOUND,
                    "submitBulkWriteAsync: unknown session id")));
                return;
            }
            token = it->second;
        }

        NSXPCConnection* conn = state_->ensureConnection();
        if (!conn) {
            on_complete(Result<std::size_t>::failure(makeError(
                proto::ERROR_HELPER_NOT_INSTALLED,
                "could not create XPC connection")));
            return;
        }

        // The reply block is captured here and invoked from whatever
        // queue NSXPC dispatches the reply on (typically a GCD bg
        // queue). Capture user callback by value into a heap-shared
        // wrapper so it stays alive across the XPC roundtrip.
        auto cb_holder = std::make_shared<BulkWriteCallback>(std::move(on_complete));

        id<RpiImagerWriterProtocol> proxy = [conn remoteObjectProxyWithErrorHandler:
            ^(NSError* error) {
                std::string detail = "(unknown)";
                if (error.localizedDescription) {
                    detail = error.localizedDescription.UTF8String;
                }
                (*cb_holder)(Result<std::size_t>::failure(makeError(
                    proto::ERROR_HELPER_NOT_INSTALLED,
                    "XPC error: " + detail)));
            }];

        [proxy bulkWriteFromBuffer:token
                       bufferOffset:buffer_offset
                       deviceOffset:device_offset
                             length:length
                              reply:^(uint64_t wrote, NSString* _Nullable detail, int32_t kerrno) {
            if (wrote == 0 && detail) {
                std::string d = detail.UTF8String;
                (*cb_holder)(Result<std::size_t>::failure(makeError(
                    proto::ERROR_WRITE_FAILED,
                    "bulkWriteFromBuffer failed: " + d, kerrno)));
            } else {
                (*cb_holder)(Result<std::size_t>::success((std::size_t)wrote));
            }
        }];
    }
}

} // namespace rpi_imager::privileged::backends
