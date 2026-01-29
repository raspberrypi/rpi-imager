/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * macOS platform-specific implementation.
 *
 * Design notes:
 * - Uses os_log for unified logging (Console.app, log collect)
 * - Uses DiskArbitration for disk operations (unmount, eject)
 * - Uses SystemConfiguration for network reachability
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <cstdio>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <QProcess>
#include <QDebug>
#import <AppKit/AppKit.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <DiskArbitration/DiskArbitration.h>
#include <os/log.h>

// Unified logging - visible in Console.app
// Users can capture with: log collect --device --last 1h
static os_log_t platform_log() {
    static os_log_t log = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        log = os_log_create("com.raspberrypi.imager", "platform");
    });
    return log;
}

#define PLATFORM_LOG_INFO(fmt, ...) os_log_info(platform_log(), fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_ERROR(fmt, ...) os_log_error(platform_log(), fmt, ##__VA_ARGS__)
#define PLATFORM_LOG_DEBUG(fmt, ...) os_log_debug(platform_log(), fmt, ##__VA_ARGS__)

namespace {
    // Network monitoring state
    SCNetworkReachabilityRef g_reachabilityRef = nullptr;
    PlatformQuirks::NetworkStatusCallback g_networkCallback = nullptr;
    pthread_mutex_t g_networkCallbackMutex = PTHREAD_MUTEX_INITIALIZER;
    
    void reachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void* info) {
        (void)target;
        (void)info;
        
        // Lock mutex to safely access callback - prevents race with stopNetworkMonitoring()
        pthread_mutex_lock(&g_networkCallbackMutex);
        PlatformQuirks::NetworkStatusCallback callback = g_networkCallback;
        pthread_mutex_unlock(&g_networkCallbackMutex);
        
        if (!callback) return;
        
        bool isReachable = (flags & kSCNetworkReachabilityFlagsReachable) != 0;
        bool needsConnection = (flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0;
        bool isAvailable = isReachable && !needsConnection;
        
        PLATFORM_LOG_INFO("Network status changed: flags=%#x reachable=%d needsConnection=%d available=%d",
                          flags, isReachable, needsConnection, isAvailable);
        callback(isAvailable);
    }
    
    // DiskArbitration context for unmount/eject operations
    struct DiskOpContext {
        PlatformQuirks::DiskResult result = PlatformQuirks::DiskResult::Success;
        bool completed = false;
    };
    
    PlatformQuirks::DiskResult translateDissenter(DADissenterRef dissenter) {
        if (!dissenter) {
            return PlatformQuirks::DiskResult::Success;
        }
        
        DAReturn status = DADissenterGetStatus(dissenter);
        CFStringRef statusString = DADissenterGetStatusString(dissenter);
        
        // Log the full error details for remote debugging
        if (statusString) {
            PLATFORM_LOG_ERROR("DiskArbitration error: status=%#x (%{public}@)", 
                               status, (__bridge NSString*)statusString);
        } else {
            PLATFORM_LOG_ERROR("DiskArbitration error: status=%#x", status);
        }
        
        // Map DAReturn to our result enum
        // See DiskArbitration/DADissenter.h for full list
        switch (status) {
            case kDAReturnSuccess:
                return PlatformQuirks::DiskResult::Success;
            case kDAReturnBadArgument:
            case kDAReturnNotFound:
                return PlatformQuirks::DiskResult::InvalidDrive;
            case kDAReturnNotPermitted:
            case kDAReturnNotPrivileged:
                return PlatformQuirks::DiskResult::AccessDenied;
            case kDAReturnBusy:
            case kDAReturnExclusiveAccess:
                return PlatformQuirks::DiskResult::Busy;
            default:
                return PlatformQuirks::DiskResult::Error;
        }
    }
    
    void unmountCallback(DADiskRef disk, DADissenterRef dissenter, void* context) {
        (void)disk;
        DiskOpContext* ctx = static_cast<DiskOpContext*>(context);
        ctx->result = translateDissenter(dissenter);
        ctx->completed = true;
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
    
    void ejectCallback(DADiskRef disk, DADissenterRef dissenter, void* context) {
        (void)disk;
        DiskOpContext* ctx = static_cast<DiskOpContext*>(context);
        ctx->result = translateDissenter(dissenter);
        ctx->completed = true;
        CFRunLoopStop(CFRunLoopGetCurrent());
    }
    
    void ejectUnmountCallback(DADiskRef disk, DADissenterRef dissenter, void* context) {
        DiskOpContext* ctx = static_cast<DiskOpContext*>(context);
        if (dissenter) {
            ctx->result = translateDissenter(dissenter);
            ctx->completed = true;
            CFRunLoopStop(CFRunLoopGetCurrent());
        } else {
            // Unmount succeeded, now eject
            DADiskEject(disk, kDADiskEjectOptionDefault, ejectCallback, context);
        }
    }
    
    // Default timeout: 60 seconds (maxRetries * 100 * 0.05s = 60s with maxRetries=12)
    // Slow USB drives with many files open may take significant time to unmount
    PlatformQuirks::DiskResult runDiskOperation(const char* device, DADiskUnmountCallback callback, int maxRetries = 12) {
        DiskOpContext context;
        
        PLATFORM_LOG_INFO("runDiskOperation: starting operation on %{public}s", device);
        
        // Create DiskArbitration session
        DASessionRef session = DASessionCreate(kCFAllocatorDefault);
        if (!session) {
            PLATFORM_LOG_ERROR("runDiskOperation: failed to create DA session");
            return PlatformQuirks::DiskResult::Error;
        }
        
        // Get disk object from BSD name
        DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, device);
        if (!disk) {
            PLATFORM_LOG_ERROR("runDiskOperation: failed to create disk object for %{public}s", device);
            CFRelease(session);
            return PlatformQuirks::DiskResult::InvalidDrive;
        }
        
        // Initiate unmount with whole disk flag
        DADiskUnmount(disk, kDADiskUnmountOptionWhole | kDADiskUnmountOptionForce, callback, &context);
        
        // Schedule session on run loop
        DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        
        // Run loop with timeout
        int retries = 0;
        while (!context.completed && retries < maxRetries * 100) {
            SInt32 status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
            if (status == kCFRunLoopRunStopped || status == kCFRunLoopRunFinished) {
                break;
            }
            retries++;
        }
        
        // Clean up
        DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        CFRelease(disk);
        CFRelease(session);
        
        if (!context.completed) {
            PLATFORM_LOG_ERROR("runDiskOperation: timeout after %d iterations on %{public}s", retries, device);
            return PlatformQuirks::DiskResult::Busy;
        }
        
        PLATFORM_LOG_INFO("runDiskOperation: completed on %{public}s with result %d", 
                          device, static_cast<int>(context.result));
        return context.result;
    }
}

namespace PlatformQuirks {

void applyQuirks() {
    @autoreleasepool {
        // Log macOS version for remote debugging
        NSOperatingSystemVersion osVersion = [[NSProcessInfo processInfo] operatingSystemVersion];
        PLATFORM_LOG_INFO("Platform: macOS %ld.%ld.%ld (%{public}s)",
                          (long)osVersion.majorVersion,
                          (long)osVersion.minorVersion,
                          (long)osVersion.patchVersion,
                          [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String]);
        
        // Currently no platform-specific quirks needed for macOS
        // macOS has a sensible permissions model that operates as expected
    }
}

void beep() {
    // Use macOS NSBeep for system beep sound
    NSBeep();
}

bool isBeepAvailable() {
    // macOS NSBeep is always available via AppKit
    return true;
}

bool hasNetworkConnectivity() {
    // Use SystemConfiguration framework to check network reachability
    // Check reachability to 0.0.0.0 (general internet connectivity)
    // rather than a specific host, which would fail if DNS is broken
    struct sockaddr_in zeroAddress;
    bzero(&zeroAddress, sizeof(zeroAddress));
    zeroAddress.sin_len = sizeof(zeroAddress);
    zeroAddress.sin_family = AF_INET;
    
    SCNetworkReachabilityRef reachability = SCNetworkReachabilityCreateWithAddress(
        NULL, reinterpret_cast<const struct sockaddr*>(&zeroAddress));
    if (!reachability) {
        PLATFORM_LOG_ERROR("hasNetworkConnectivity: failed to create reachability ref");
        return false;
    }
    
    SCNetworkReachabilityFlags flags = 0;
    bool success = SCNetworkReachabilityGetFlags(reachability, &flags);
    CFRelease(reachability);
    
    if (!success) {
        PLATFORM_LOG_ERROR("hasNetworkConnectivity: failed to get reachability flags");
        return false;
    }
    
    // Check if network is reachable
    bool isReachable = (flags & kSCNetworkReachabilityFlagsReachable) != 0;
    // Check if connection is required (e.g., captive portal, VPN on demand)
    bool needsConnection = (flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0;
    
    PLATFORM_LOG_DEBUG("hasNetworkConnectivity: flags=%#x reachable=%d needsConnection=%d",
                       flags, isReachable, needsConnection);
    
    return isReachable && !needsConnection;
}

bool isNetworkReady() {
    // On macOS, no special time sync check needed - system time is reliable
    return hasNetworkConnectivity();
}

void startNetworkMonitoring(NetworkStatusCallback callback) {
    // Stop any existing monitoring
    stopNetworkMonitoring();
    
    // Set callback under mutex
    pthread_mutex_lock(&g_networkCallbackMutex);
    g_networkCallback = callback;
    pthread_mutex_unlock(&g_networkCallbackMutex);
    
    // Create reachability reference for general connectivity (0.0.0.0)
    // This monitors overall network status rather than a specific host
    struct sockaddr_in zeroAddress;
    bzero(&zeroAddress, sizeof(zeroAddress));
    zeroAddress.sin_len = sizeof(zeroAddress);
    zeroAddress.sin_family = AF_INET;
    
    g_reachabilityRef = SCNetworkReachabilityCreateWithAddress(
        NULL, reinterpret_cast<const struct sockaddr*>(&zeroAddress));
    if (!g_reachabilityRef) {
        PLATFORM_LOG_ERROR("startNetworkMonitoring: failed to create reachability reference");
        return;
    }
    
    // Set up callback context
    SCNetworkReachabilityContext context = {0, nullptr, nullptr, nullptr, nullptr};
    
    if (!SCNetworkReachabilitySetCallback(g_reachabilityRef, reachabilityCallback, &context)) {
        PLATFORM_LOG_ERROR("startNetworkMonitoring: failed to set callback");
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        return;
    }
    
    // Schedule on main run loop
    if (!SCNetworkReachabilityScheduleWithRunLoop(g_reachabilityRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode)) {
        PLATFORM_LOG_ERROR("startNetworkMonitoring: failed to schedule on run loop");
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        return;
    }
    
    PLATFORM_LOG_INFO("Network monitoring started");
}

void stopNetworkMonitoring() {
    if (g_reachabilityRef) {
        SCNetworkReachabilityUnscheduleFromRunLoop(g_reachabilityRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        PLATFORM_LOG_INFO("Network monitoring stopped");
    }
    
    // Clear callback under mutex to prevent race with reachabilityCallback
    pthread_mutex_lock(&g_networkCallbackMutex);
    g_networkCallback = nullptr;
    pthread_mutex_unlock(&g_networkCallbackMutex);
}

void bringWindowToForeground(void* windowHandle) {
    @autoreleasepool {
        // Activate the application and bring it to foreground
        // This works when called from user interaction context
        [NSApp activateIgnoringOtherApps:YES];
        
        // If we have a specific window handle, make it key
        if (windowHandle) {
            // windowHandle could be NSWindow* or NSView*
            // For Qt windows, it's typically the NSView from QWindow::winId()
            NSView* view = (__bridge NSView*)windowHandle;
            NSWindow* window = [view window];
            if (window) {
                [window makeKeyAndOrderFront:nil];
            }
        }
        
        PLATFORM_LOG_DEBUG("bringWindowToForeground: activated app");
    }
}

bool hasElevatedPrivileges() {
    // macOS has a sensible permissions model that operates as expected
    // No special privilege check needed - return true
    return true;
}

void attachConsole() {
    // No-op on macOS - console is already available
}

bool isElevatableBundle() {
    // macOS .app bundles don't need this mechanism - Authorization Services handles elevation
    return false;
}

const char* getBundlePath() {
    // Not applicable on macOS
    return nullptr;
}

bool hasElevationPolicyInstalled() {
    // Not applicable on macOS
    return false;
}

bool installElevationPolicy() {
    // Not applicable on macOS
    return false;
}

bool tryElevate(int argc, char** argv) {
    // macOS uses Authorization Services, not polkit-style elevation
    (void)argc;
    (void)argv;
    return false;
}

bool launchDetached(const QString& program, const QStringList& arguments) {
    // On macOS, QProcess::startDetached works correctly for launching
    // detached processes that outlive the parent
    return QProcess::startDetached(program, arguments);
}

bool runElevatedPolicyInstaller() {
    return false;
}

void execElevated(const QStringList& extraArgs) {
    Q_UNUSED(extraArgs);
}

bool isScrollInverted(bool qtInvertedFlag) {
    // On macOS, Qt correctly reports the inverted flag in WheelEvent
    // so we just pass through the Qt value.
    return qtInvertedFlag;
}

QString getWriteDevicePath(const QString& devicePath) {
    // On macOS, use raw disk device (/dev/rdisk) for direct I/O.
    // This bypasses the macOS buffer cache and provides significantly
    // faster write performance for large sequential writes.
    QString result = devicePath;
    result.replace("/dev/disk", "/dev/rdisk");
    return result;
}

QString getEjectDevicePath(const QString& devicePath) {
    // Convert back to block device path for eject operations.
    // While DADiskCreateFromBSDName technically accepts both forms,
    // using /dev/disk is the canonical form for disk operations.
    QString result = devicePath;
    result.replace("/dev/rdisk", "/dev/disk");
    return result;
}

DiskResult unmountDisk(const QString& device) {
    // Ensure we're using block device path (disk, not rdisk)
    QString diskPath = getEjectDevicePath(device);
    
    // Extract BSD name (remove /dev/ prefix)
    QString bsdName = diskPath;
    if (bsdName.startsWith("/dev/")) {
        bsdName = bsdName.mid(5);
    }
    
    QByteArray bsdNameBytes = bsdName.toUtf8();
    qDebug() << "unmountDisk: unmounting" << bsdNameBytes.constData();
    
    return runDiskOperation(bsdNameBytes.constData(), unmountCallback);
}

DiskResult ejectDisk(const QString& device) {
    // Ensure we're using block device path (disk, not rdisk)
    QString diskPath = getEjectDevicePath(device);
    
    // Extract BSD name (remove /dev/ prefix)
    QString bsdName = diskPath;
    if (bsdName.startsWith("/dev/")) {
        bsdName = bsdName.mid(5);
    }
    
    QByteArray bsdNameBytes = bsdName.toUtf8();
    qDebug() << "ejectDisk: ejecting" << bsdNameBytes.constData();
    
    // Use the combined unmount+eject callback
    return runDiskOperation(bsdNameBytes.constData(), ejectUnmountCallback);
}

} // namespace PlatformQuirks