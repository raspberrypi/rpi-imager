/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "nativefiledialog.h"
#include <QStandardPaths>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>

QString NativeFileDialog::getOpenFileName(const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        return getFileNameQt(title, initialDir, filter, false);
    }

    return getFileNameNative(title, initialDir, filter, false);
}

QString NativeFileDialog::getSaveFileName(const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        return getFileNameQt(title, initialDir, filter, true);
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
    return (platform == "eglfs" || platform == "linuxfb");
}

QString NativeFileDialog::getFileNameQt(const QString &title,
                                        const QString &initialDir, const QString &filter,
                                        bool saveDialog)
{
    Q_UNUSED(title)
    Q_UNUSED(initialDir)
    Q_UNUSED(filter)
    Q_UNUSED(saveDialog)
    
    // QtWidgets dependency removed - no fallback dialog available
    // Native dialogs should be used on all desktop platforms
    qWarning() << "NativeFileDialog: Qt fallback called but QtWidgets not available";
    return QString(); // Return empty string (user cancelled)
}