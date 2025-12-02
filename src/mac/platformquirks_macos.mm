/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <QProcess>
#import <AppKit/AppKit.h>
#import <SystemConfiguration/SystemConfiguration.h>

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

} // namespace PlatformQuirks