/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>

namespace PlatformQuirks {

void applyQuirks() {
    // When running as root via sudo, ensure cache and settings directories
    // are tied to the original user who ran sudo, not root
    if (::geteuid() == 0) {
        // Check if we're running via sudo
        const char* sudoUid = ::getenv("SUDO_UID");
        const char* sudoGid = ::getenv("SUDO_GID");
        const char* sudoUser = ::getenv("SUDO_USER");
        
        if (sudoUid && sudoGid && sudoUser) {
            // We're running under sudo - get the original user's home directory
            uid_t originalUid = static_cast<uid_t>(::atoi(sudoUid));
            struct passwd* pw = ::getpwuid(originalUid);
            
            if (pw && pw->pw_dir) {
                // Use C-style strings since this happens before Qt is fully initialized
                char xdgCacheHome[512];
                char xdgConfigHome[512];
                char xdgDataHome[512];
                char xdgRuntimeDir[512];
                
                std::snprintf(xdgCacheHome, sizeof(xdgCacheHome), "%s/.cache", pw->pw_dir);
                std::snprintf(xdgConfigHome, sizeof(xdgConfigHome), "%s/.config", pw->pw_dir);
                std::snprintf(xdgDataHome, sizeof(xdgDataHome), "%s/.local/share", pw->pw_dir);
                std::snprintf(xdgRuntimeDir, sizeof(xdgRuntimeDir), "/run/user/%s", sudoUid);
                
                std::fprintf(stderr, "Running as root via sudo\n");
                std::fprintf(stderr, "Original user: %s\n", sudoUser);
                std::fprintf(stderr, "Original UID: %s\n", sudoUid);
                std::fprintf(stderr, "Original home directory: %s\n", pw->pw_dir);
                
                // Override HOME to point to the original user's home directory
                // This ensures QStandardPaths and QSettings use the correct user's directories
                ::setenv("HOME", pw->pw_dir, 1);
                
                // Set XDG environment variables to ensure proper directory usage
                // Qt respects XDG Base Directory specification on Linux
                ::setenv("XDG_CACHE_HOME", xdgCacheHome, 1);
                ::setenv("XDG_CONFIG_HOME", xdgConfigHome, 1);
                ::setenv("XDG_DATA_HOME", xdgDataHome, 1);
                
                // Set XDG_RUNTIME_DIR to point to the original user's runtime directory
                // This is crucial for D-Bus session bus communication
                ::setenv("XDG_RUNTIME_DIR", xdgRuntimeDir, 1);
                
                // Try to construct the D-Bus session bus address from the original user's runtime directory
                // This is needed for suspend inhibitor and other D-Bus services
                char dbusSessionAddress[1024];
                std::snprintf(dbusSessionAddress, sizeof(dbusSessionAddress), 
                             "unix:path=%s/bus", xdgRuntimeDir);
                ::setenv("DBUS_SESSION_BUS_ADDRESS", dbusSessionAddress, 1);
                
                std::fprintf(stderr, "Set HOME to: %s\n", pw->pw_dir);
                std::fprintf(stderr, "Set XDG_CACHE_HOME to: %s\n", xdgCacheHome);
                std::fprintf(stderr, "Set XDG_CONFIG_HOME to: %s\n", xdgConfigHome);
                std::fprintf(stderr, "Set XDG_DATA_HOME to: %s\n", xdgDataHome);
                std::fprintf(stderr, "Set XDG_RUNTIME_DIR to: %s\n", xdgRuntimeDir);
                std::fprintf(stderr, "Set DBUS_SESSION_BUS_ADDRESS to: %s\n", dbusSessionAddress);
            } else {
                std::fprintf(stderr, "WARNING: Could not retrieve original user information for UID: %s\n", sudoUid);
            }
        }
    }
}

void beep() {
    // Try multiple Linux beep mechanisms in order of preference
    
    // 1. Try pactl (PulseAudio) beep - most common on modern Linux desktop systems
    if (QProcess::execute("pactl", QStringList() << "upload-sample" << "/usr/share/sounds/alsa/Front_Left.wav" << "beep") == 0) {
        QProcess::execute("pactl", QStringList() << "play-sample" << "beep");
        return;
    }
    
    // 2. Try system bell via echo (works on most terminals)
    if (QProcess::execute("echo", QStringList() << "-e" << "\\a") == 0) {
        return;
    }
    
    // 3. Try beep command if available
    if (QProcess::execute("beep", QStringList()) == 0) {
        return;
    }
    
    // 4. Fallback: just log that beep was requested
    qDebug() << "Beep requested but no suitable audio mechanism found on this Linux system";
}

bool hasNetworkConnectivity() {
    // Check multiple indicators of network connectivity on Linux
    
    // Method 1: Check if any network interface (other than loopback) is up
    QDir sysNet("/sys/class/net");
    if (sysNet.exists()) {
        QStringList interfaces = sysNet.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &iface : interfaces) {
            if (iface == "lo") continue; // Skip loopback
            
            // Check if interface is up
            QFile operstate(QString("/sys/class/net/%1/operstate").arg(iface));
            if (operstate.open(QIODevice::ReadOnly)) {
                QString state = QString::fromLatin1(operstate.readAll()).trimmed();
                operstate.close();
                if (state == "up") {
                    // Interface is up - likely have connectivity
                    return true;
                }
            }
        }
    }
    
    // Method 2: Check NetworkManager (if available)
    QProcess nmcli;
    nmcli.start("nmcli", QStringList() << "networking" << "connectivity" << "check");
    if (nmcli.waitForFinished(1000)) {
        QString output = QString::fromLatin1(nmcli.readAllStandardOutput()).trimmed();
        if (output == "full" || output == "limited") {
            return true;
        }
    }
    
    // If we can't determine connectivity, assume offline for safety
    return false;
}

bool isNetworkReady() {
    // First check basic connectivity
    if (!hasNetworkConnectivity()) {
        return false;
    }
    
    // Check if systemd-timesyncd has synchronized time
    // This is important for embedded systems where the RTC might not be set
    // systemd-timesyncd updates the timestamp of /var/lib/systemd/timesync/clock when synced
    QFile clockFile("/var/lib/systemd/timesync/clock");
    QFile timesyncBinary("/lib/systemd/systemd-timesyncd");
    
    // If systemd-timesyncd is not present, assume time is reliable
    if (!timesyncBinary.exists()) {
        return true;
    }
    
    // If clock file doesn't exist yet, time hasn't been synced
    if (!clockFile.exists()) {
        qDebug() << "systemd-timesyncd clock file does not exist - time not yet synchronized";
        return false;
    }
    
    // Check if clock file has been updated after the timesync binary was installed
    // This indicates that time synchronization has occurred
    QFileInfo clockInfo(clockFile);
    QFileInfo binaryInfo(timesyncBinary);
    
    bool timeIsSynced = clockInfo.lastModified() > binaryInfo.lastModified();
    if (!timeIsSynced) {
        qDebug() << "systemd-timesyncd has not yet synchronized time";
    }
    
    return timeIsSynced;
}

void bringWindowToForeground(void* windowHandle) {
    // No-op on Linux - window management is handled by the window manager
    // and applications cannot force themselves to the foreground
    Q_UNUSED(windowHandle);
}

bool hasElevatedPrivileges() {
    // Check if running as root (UID 0)
    return ::geteuid() == 0;
}

} // namespace PlatformQuirks