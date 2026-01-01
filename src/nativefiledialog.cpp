/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "nativefiledialog.h"
#include "embedded_config.h"
#include <QStandardPaths>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>
// QML-side fallback is handled at callsites; no QML engine usage here

// Initialize static members
bool NativeFileDialog::s_forceQmlDialogs = false;
NativeFileDialog::TimingInfo NativeFileDialog::s_lastTimingInfo;

NativeFileDialog::TimingInfo NativeFileDialog::lastTimingInfo()
{
    return s_lastTimingInfo;
}

QString fileDialogTimingToString(const NativeFileDialog::TimingInfo &info)
{
    // Note: directory is intentionally omitted to avoid capturing PII (paths may contain usernames)
    return QString("pathParsing=%1ms, filterParsing=%2ms, panelCreation=%3ms, "
                   "setDirectory=%4ms, panelSetup=%5ms, totalBeforeShow=%6ms, "
                   "userInteraction=%7ms, isSave=%8")
        .arg(info.pathParsingMs)
        .arg(info.filterParsingMs)
        .arg(info.panelCreationMs)
        .arg(info.setDirectoryMs)
        .arg(info.panelSetupMs)
        .arg(info.totalBeforeShowMs)
        .arg(info.userInteractionMs)
        .arg(info.isSaveDialog ? "true" : "false");
}

QString NativeFileDialog::getOpenFileName(const QString &title,
                                          const QString &initialDir, const QString &filter,
                                          void *parentWindow)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        // No C++ fallback; QML callsites handle fallback
        return QString();
    }

    return getFileNameNative(title, initialDir, filter, false, parentWindow);
}

QString NativeFileDialog::getSaveFileName(const QString &title,
                                          const QString &initialDir, const QString &filter,
                                          void *parentWindow)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        // No C++ fallback; QML callsites handle fallback
        return QString();
    }

    return getFileNameNative(title, initialDir, filter, true, parentWindow);
}

bool NativeFileDialog::areNativeDialogsAvailable()
{
    // Force QML dialogs if requested via command-line flag
    if (s_forceQmlDialogs) {
        return false;
    }

    // Always use Qt dialogs in embedded mode
    if (::isEmbeddedMode()) {
        return false;
    }

    return areNativeDialogsAvailablePlatform();
}

void NativeFileDialog::setForceQmlDialogs(bool force)
{
    s_forceQmlDialogs = force;
}

// No extra helper function; return empty to signal QML fallback