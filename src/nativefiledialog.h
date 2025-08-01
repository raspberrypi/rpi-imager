/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef NATIVEFILEDIALOG_H
#define NATIVEFILEDIALOG_H

#include <QString>
#include <QStringList>
#include <QWidget>

/**
 * @brief Native file dialog interface that falls back to Qt dialogs when needed
 * 
 * This class provides native file dialogs for Windows, macOS, and Linux desktop environments,
 * while automatically falling back to QFileDialog in embedded environments or when
 * native dialogs are not available.
 */
class NativeFileDialog
{
public:
    /**
     * @brief Shows a native open file dialog
     * @param parent Parent widget for the dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @return Selected file path, or empty string if cancelled
     */
    static QString getOpenFileName(QWidget *parent = nullptr,
                                   const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Shows a native save file dialog
     * @param parent Parent widget for the dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @return Selected file path, or empty string if cancelled
     */
    static QString getSaveFileName(QWidget *parent = nullptr,
                                   const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Check if native dialogs are available
     * @return true if native dialogs can be used, false if fallback to Qt is needed
     */
    static bool areNativeDialogsAvailable();

private:
    // Platform-specific implementations (implemented in platform-specific files)
    static QString getFileNameNative(QWidget *parent, const QString &title,
                                     const QString &initialDir, const QString &filter,
                                     bool saveDialog);
    static bool areNativeDialogsAvailablePlatform();

    // Qt fallback implementation
    static QString getFileNameQt(QWidget *parent, const QString &title,
                                 const QString &initialDir, const QString &filter,
                                 bool saveDialog);

    // Helper functions
    static bool isEmbeddedMode();
};

#endif // NATIVEFILEDIALOG_H