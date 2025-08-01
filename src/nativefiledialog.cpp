/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "nativefiledialog.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDebug>

QString NativeFileDialog::getOpenFileName(QWidget *parent, const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        return getFileNameQt(parent, title, initialDir, filter, false);
    }

    return getFileNameNative(parent, title, initialDir, filter, false);
}

QString NativeFileDialog::getSaveFileName(QWidget *parent, const QString &title,
                                          const QString &initialDir, const QString &filter)
{
    // Check if we should use native dialogs
    if (!areNativeDialogsAvailable()) {
        return getFileNameQt(parent, title, initialDir, filter, true);
    }

    return getFileNameNative(parent, title, initialDir, filter, true);
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

QString NativeFileDialog::getFileNameQt(QWidget *parent, const QString &title,
                                        const QString &initialDir, const QString &filter,
                                        bool saveDialog)
{
    QString dir = initialDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }

    if (saveDialog) {
        return QFileDialog::getSaveFileName(parent, title, dir, filter);
    } else {
        return QFileDialog::getOpenFileName(parent, title, dir, filter);
    }
}