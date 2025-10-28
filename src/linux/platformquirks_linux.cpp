/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>

namespace PlatformQuirks {

void applyQuirks() {
    // Currently no platform-specific quirks needed for Linux
    // This is a placeholder for future Linux-specific workarounds
    
    // Example of how to set environment variables without Qt:
    // setenv("VARIABLE_NAME", "value", 1);
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

} // namespace PlatformQuirks