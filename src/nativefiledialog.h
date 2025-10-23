/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef NATIVEFILEDIALOG_H
#define NATIVEFILEDIALOG_H

#include <QString>
#include <QStringList>

/**
 * @brief File dialog interface for all platforms
 * 
 * Provides native file dialogs for Windows, macOS, and Linux when available.
 * Falls back to a QML-based FileDialog (QtQuick.Dialogs) without requiring QtWidgets.
 * See: https://doc.qt.io/qt-6/qml-qtquick-dialogs-filedialog.html
 */
class NativeFileDialog
{
public:
    /**
     * @brief Shows a native open file dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @param parentWindow Parent window for modal behavior (optional)
     * @return Selected file path, or empty string if cancelled
     */
    static QString getOpenFileName(const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString(),
                                   void *parentWindow = nullptr);

    /**
     * @brief Shows a native save file dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @param parentWindow Parent window for modal behavior (optional)
     * @return Selected file path, or empty string if cancelled
     */
    static QString getSaveFileName(const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString(),
                                   void *parentWindow = nullptr);

    /**
     * @brief Check if native dialogs are available
     * @return true if native dialogs can be used
     */
    static bool areNativeDialogsAvailable();

    /**
     * @brief Force the use of QML file dialogs instead of native ones
     * @param force If true, always use QML dialogs regardless of platform support
     */
    static void setForceQmlDialogs(bool force);

private:
    // Platform-specific implementations (implemented in platform-specific files)
    static QString getFileNameNative(const QString &title,
                                     const QString &initialDir, const QString &filter,
                                     bool saveDialog, void *parentWindow);
    static bool areNativeDialogsAvailablePlatform();

    // Flag to force QML dialogs
    static bool s_forceQmlDialogs;

};

#endif // NATIVEFILEDIALOG_H