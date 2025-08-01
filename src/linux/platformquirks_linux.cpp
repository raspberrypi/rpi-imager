/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <QDebug>
#include <QProcess>

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

} // namespace PlatformQuirks