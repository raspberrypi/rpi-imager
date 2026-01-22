/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <cstdio>
#include <QProcess>
#include <QDebug>
#import <AppKit/AppKit.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <DiskArbitration/DiskArbitration.h>

namespace {
    // Network monitoring state
    SCNetworkReachabilityRef g_reachabilityRef = nullptr;
    PlatformQuirks::NetworkStatusCallback g_networkCallback = nullptr;
    
    void reachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void* info) {
        (void)target;
        (void)info;
        
        if (!g_networkCallback) return;
        
        bool isReachable = (flags & kSCNetworkReachabilityFlagsReachable) != 0;
        bool needsConnection = (flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0;
        bool isAvailable = isReachable && !needsConnection;
        
        fprintf(stderr, "Network status changed: reachable=%d needsConnection=%d\n", isReachable, needsConnection);
        g_networkCallback(isAvailable);
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
        if (status == kDAReturnBadArgument || status == kDAReturnNotFound) {
            return PlatformQuirks::DiskResult::InvalidDrive;
        } else if (status == kDAReturnNotPermitted || status == kDAReturnNotPrivileged) {
            return PlatformQuirks::DiskResult::AccessDenied;
        } else if (status == kDAReturnBusy) {
            return PlatformQuirks::DiskResult::Busy;
        }
        return PlatformQuirks::DiskResult::Error;
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
    
    PlatformQuirks::DiskResult runDiskOperation(const char* device, DADiskUnmountCallback callback, int maxRetries = 5) {
        DiskOpContext context;
        
        // Create DiskArbitration session
        DASessionRef session = DASessionCreate(kCFAllocatorDefault);
        if (!session) {
            qDebug() << "runDiskOperation: failed to create DA session";
            return PlatformQuirks::DiskResult::Error;
        }
        
        // Get disk object from BSD name
        DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, device);
        if (!disk) {
            qDebug() << "runDiskOperation: failed to create disk object for" << device;
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
            qDebug() << "runDiskOperation: timeout waiting for operation";
            return PlatformQuirks::DiskResult::Busy;
        }
        
        return context.result;
    }
}

namespace PlatformQuirks {

void applyQuirks() {
    // Currently no platform-specific quirks needed for macOS
    // macOS has a sensible permissions model that operates as expected
    
    // Example of how to set environment variables without Qt:
    // setenv("VARIABLE_NAME", "value", 1);
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
    SCNetworkReachabilityRef reachability = SCNetworkReachabilityCreateWithName(NULL, "www.raspberrypi.com");
    if (!reachability) {
        return false;
    }
    
    SCNetworkReachabilityFlags flags;
    bool success = SCNetworkReachabilityGetFlags(reachability, &flags);
    CFRelease(reachability);
    
    if (!success) {
        return false;
    }
    
    // Check if network is reachable
    bool isReachable = (flags & kSCNetworkReachabilityFlagsReachable) != 0;
    // Check if connection is required (e.g., captive portal)
    bool needsConnection = (flags & kSCNetworkReachabilityFlagsConnectionRequired) != 0;
    
    return isReachable && !needsConnection;
}

bool isNetworkReady() {
    // On macOS, no special time sync check needed - system time is reliable
    return hasNetworkConnectivity();
}

void startNetworkMonitoring(NetworkStatusCallback callback) {
    // Stop any existing monitoring
    stopNetworkMonitoring();
    
    g_networkCallback = callback;
    
    // Create reachability reference for a known host
    g_reachabilityRef = SCNetworkReachabilityCreateWithName(NULL, "www.raspberrypi.com");
    if (!g_reachabilityRef) {
        fprintf(stderr, "Failed to create network reachability reference\n");
        return;
    }
    
    // Set up callback context
    SCNetworkReachabilityContext context = {0, nullptr, nullptr, nullptr, nullptr};
    
    if (!SCNetworkReachabilitySetCallback(g_reachabilityRef, reachabilityCallback, &context)) {
        fprintf(stderr, "Failed to set network reachability callback\n");
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        return;
    }
    
    // Schedule on main run loop
    if (!SCNetworkReachabilityScheduleWithRunLoop(g_reachabilityRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode)) {
        fprintf(stderr, "Failed to schedule network reachability on run loop\n");
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        return;
    }
    
    fprintf(stderr, "Network monitoring started\n");
}

void stopNetworkMonitoring() {
    if (g_reachabilityRef) {
        SCNetworkReachabilityUnscheduleFromRunLoop(g_reachabilityRef, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        CFRelease(g_reachabilityRef);
        g_reachabilityRef = nullptr;
        fprintf(stderr, "Network monitoring stopped\n");
    }
    g_networkCallback = nullptr;
}

void bringWindowToForeground(void* windowHandle) {
    // No-op on macOS - not implemented
    // macOS handles window activation differently and has restrictions on
    // applications bringing themselves to the foreground
    (void)windowHandle;
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