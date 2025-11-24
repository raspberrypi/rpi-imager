/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QWindow>
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <vector>

namespace {
// Convert Qt filter format to COMDLG_FILTERSPEC array for IFileDialog
std::vector<COMDLG_FILTERSPEC> convertQtFilterToFileDialog(const QString &qtFilter, 
                                                            std::vector<std::wstring> &storage)
{
    std::vector<COMDLG_FILTERSPEC> filters;
    
    if (qtFilter.isEmpty()) {
        return filters;
    }
    
    // Qt filter format: "Description (*.ext1 *.ext2);;Another (*.ext3)"
    QStringList filterList = qtFilter.split(";;");
    
    for (const QString &filter : filterList) {
        QString description = filter.section('(', 0, 0).trimmed();
        QString extensions = filter.section('(', 1, 1).section(')', 0, 0);
        
        // IFileDialog expects semicolons between extensions (already space-separated in Qt format)
        extensions = extensions.replace(" ", ";");
        
        // Store strings in vector to keep them alive
        storage.push_back(description.toStdWString());
        storage.push_back(extensions.toStdWString());
        
        COMDLG_FILTERSPEC spec;
        spec.pszName = storage[storage.size() - 2].c_str();
        spec.pszSpec = storage[storage.size() - 1].c_str();
        filters.push_back(spec);
    }
    
    return filters;
}

// Helper class to ensure COM is initialized and cleaned up properly
class ComInitializer
{
public:
    ComInitializer() : m_initialized(false)
    {
        // Initialize COM with apartment threading model
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            m_initialized = true;
        } else if (hr == RPC_E_CHANGED_MODE) {
            // COM already initialized with different threading model - that's OK
            m_initialized = false;
        }
    }
    
    ~ComInitializer()
    {
        if (m_initialized) {
            CoUninitialize();
        }
    }
    
    bool isValid() const { return m_initialized || SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); }
    
private:
    bool m_initialized;
};
} // anonymous namespace

QString NativeFileDialog::getFileNameNative(const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog, void *parentWindow)
{
    // Initialize COM for this thread
    ComInitializer com;
    
    QString result;
    IFileDialog *pfd = nullptr;
    HRESULT hr;
    
    // Create the appropriate file dialog
    if (saveDialog) {
        hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, 
                            IID_PPV_ARGS(&pfd));
    } else {
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, 
                            IID_PPV_ARGS(&pfd));
    }
    
    if (FAILED(hr) || !pfd) {
        qDebug() << "Failed to create IFileDialog:" << QString::number(hr, 16);
        return QString();
    }
    
    // Set dialog options
    DWORD dwFlags;
    hr = pfd->GetOptions(&dwFlags);
    if (SUCCEEDED(hr)) {
        // Add flags for better UX:
        // - FOS_FORCEFILESYSTEM: Only allow file system items
        // - FOS_PATHMUSTEXIST: Path must exist
        // - FOS_FILEMUSTEXIST: File must exist (for open dialog)
        // - FOS_NOCHANGEDIR: Don't change current directory
        // - FOS_DONTADDTORECENT: Don't add to recent files
        dwFlags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        
        if (!saveDialog) {
            dwFlags |= FOS_FILEMUSTEXIST;
        }
        
        pfd->SetOptions(dwFlags);
    }
    
    // Set title if provided
    if (!title.isEmpty()) {
        pfd->SetTitle(reinterpret_cast<const wchar_t*>(title.utf16()));
    }
    
    // Set file type filters
    if (!filter.isEmpty()) {
        std::vector<std::wstring> filterStorage;
        std::vector<COMDLG_FILTERSPEC> filters = convertQtFilterToFileDialog(filter, filterStorage);
        if (!filters.empty()) {
            pfd->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
            pfd->SetFileTypeIndex(1); // Select first filter by default
        }
    }
    
    // Set initial directory
    QString dir = initialDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    
    if (!dir.isEmpty()) {
        IShellItem *psiFolder = nullptr;
        std::wstring dirStr = QDir::toNativeSeparators(dir).toStdWString();
        hr = SHCreateItemFromParsingName(dirStr.c_str(), nullptr, IID_PPV_ARGS(&psiFolder));
        if (SUCCEEDED(hr) && psiFolder) {
            pfd->SetFolder(psiFolder);
            psiFolder->Release();
        }
    }
    
    // Get parent window handle
    HWND hwndOwner = nullptr;
    if (parentWindow) {
        QWindow *window = static_cast<QWindow*>(parentWindow);
        hwndOwner = reinterpret_cast<HWND>(window->winId());
    }
    
    // Show the dialog
    hr = pfd->Show(hwndOwner);
    
    if (SUCCEEDED(hr)) {
        // Get the result
        IShellItem *psiResult = nullptr;
        hr = pfd->GetResult(&psiResult);
        if (SUCCEEDED(hr) && psiResult) {
            PWSTR pszFilePath = nullptr;
            hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
            if (SUCCEEDED(hr) && pszFilePath) {
                result = QString::fromWCharArray(pszFilePath);
                CoTaskMemFree(pszFilePath);
            }
            psiResult->Release();
        }
    } else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        // User cancelled - not an error
    } else {
        qDebug() << "IFileDialog::Show failed:" << QString::number(hr, 16);
    }
    
    pfd->Release();
    return result;
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Native dialogs are always available on Windows
    return true;
}

