/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef NATIVEFILEDIALOG_H
#define NATIVEFILEDIALOG_H

#include <QString>
#include <QStringList>

/**
 * @brief Native file dialog interface for all desktop platforms
 * 
 * This class provides native file dialogs for Windows, macOS, and Linux desktop environments.
 * QtWidgets dependency has been removed - only native dialogs are supported.
 */
class NativeFileDialog
{
public:
    /**
     * @brief Shows a native open file dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @return Selected file path, or empty string if cancelled
     */
    static QString getOpenFileName(const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Shows a native save file dialog
     * @param title Dialog title
     * @param initialDir Initial directory to show
     * @param filter File type filters (Qt format: "Images (*.png *.jpg);;All files (*)")
     * @return Selected file path, or empty string if cancelled
     */
    static QString getSaveFileName(const QString &title = QString(),
                                   const QString &initialDir = QString(),
                                   const QString &filter = QString());

    /**
     * @brief Check if native dialogs are available
     * @return true if native dialogs can be used
     */
    static bool areNativeDialogsAvailable();

private:
    // Platform-specific implementations (implemented in platform-specific files)
    static QString getFileNameNative(const QString &title,
                                     const QString &initialDir, const QString &filter,
                                     bool saveDialog);
    static bool areNativeDialogsAvailablePlatform();

    // Qt fallback implementation (no-op - always returns empty string)
    static QString getFileNameQt(const QString &title,
                                 const QString &initialDir, const QString &filter,
                                 bool saveDialog);

    // Helper functions
    static bool isEmbeddedMode();
};

#endif // NATIVEFILEDIALOG_H