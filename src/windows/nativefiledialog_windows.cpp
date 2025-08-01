/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <windows.h>
#include <commdlg.h>

namespace {
QString convertQtFilterToWindows(const QString &qtFilter)
{
    if (qtFilter.isEmpty()) {
        return QString();
    }
    
    // Qt filter format: "Description (*.ext1 *.ext2);;Another (*.ext3)"
    // Windows format: "Description\0*.ext1;*.ext2\0Another\0*.ext3\0\0"
    QString windowsFilter;
    QStringList filters = qtFilter.split(";;");
    
    for (const QString &filter : filters) {
        QString description = filter.section('(', 0, 0).trimmed();
        QString extensions = filter.section('(', 1, 1).section(')', 0, 0);
        extensions = extensions.replace(" ", ";");
        
        if (!windowsFilter.isEmpty()) {
            windowsFilter += QChar('\0');
        }
        windowsFilter += description + QChar('\0') + extensions;
    }
    windowsFilter += QChar('\0');
    
    return windowsFilter;
}
} // anonymous namespace

QString NativeFileDialog::getFileNameNative(const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog)
{  // Windows dialogs will be modal to the application
    
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    
    // Buffer for the selected file name
    wchar_t szFile[MAX_PATH] = {0};
    
    // Convert filter from Qt format to Windows format
    QString windowsFilter = convertQtFilterToWindows(filter);
    std::wstring filterStr = windowsFilter.toStdWString();
    
    // Convert title to wide string
    std::wstring titleStr = title.toStdWString();
    
    // Convert initial directory to wide string
    QString dir = initialDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    std::wstring initialDirStr = QDir::toNativeSeparators(dir).toStdWString();
    
    // Setup OPENFILENAME structure
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;  // Could get actual window handle if needed
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = filterStr.empty() ? nullptr : filterStr.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = initialDirStr.c_str();
    ofn.lpstrTitle = titleStr.empty() ? nullptr : titleStr.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (saveDialog) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    }
    
    BOOL result;
    if (saveDialog) {
        result = GetSaveFileNameW(&ofn);
    } else {
        result = GetOpenFileNameW(&ofn);
    }
    
    if (result) {
        return QString::fromWCharArray(szFile);
    }
    
    return QString();  // User cancelled or error occurred
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Native dialogs are always available on Windows
    return true;
}

