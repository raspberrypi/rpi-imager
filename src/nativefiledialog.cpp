/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "nativefiledialog.h"
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
    if (isEmbeddedMode()) {
        return false;
    }

    return areNativeDialogsAvailablePlatform();
}

bool NativeFileDialog::isEmbeddedMode()
{
    // Check if we're running in an embedded environment
    QString platform;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        platform = QGuiApplication::platformName();
    } else {
        platform = "cli";
    }

    // Only Linux typically runs in embedded mode for this application
    return (platform == "linuxfb");
}

// No extra helper function; return empty to signal QML fallback