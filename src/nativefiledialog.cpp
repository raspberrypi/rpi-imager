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

QString NativeFileDialog::getOpenFileName(const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        // No C++ fallback; QML callsites handle fallback
        return QString();
    }

    return getFileNameNative(title, initialDir, filter, false);
}

QString NativeFileDialog::getSaveFileName(const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        // No C++ fallback; QML callsites handle fallback
        return QString();
    }

    return getFileNameNative(title, initialDir, filter, true);
}

bool NativeFileDialog::areNativeDialogsAvailable()
{
    // Always use Qt dialogs in embedded mode
    if (::isEmbeddedMode()) {
        return false;
    }

    return areNativeDialogsAvailablePlatform();
}

// No extra helper function; return empty to signal QML fallback