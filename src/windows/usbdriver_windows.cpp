/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "usbdriver_windows.h"

#include <windows.h>
#include <shellapi.h>

namespace UsbDriverWindows {

InstallResult installRpibootDriver(const std::wstring &infPath, unsigned long &detail)
{
    detail = 0;

    wchar_t sysDir[MAX_PATH] = {0};
    const UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        detail = GetLastError();
        return InstallResult::Failed;
    }
    const std::wstring pnputil = std::wstring(sysDir) + L"\\pnputil.exe";
    const std::wstring params = L"/add-driver \"" + infPath + L"\" /install";

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = pnputil.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        detail = GetLastError();
        return detail == ERROR_CANCELLED ? InstallResult::Declined : InstallResult::Failed;
    }
    if (!sei.hProcess) {
        detail = ERROR_INVALID_HANDLE;
        return InstallResult::Failed;
    }

    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);

    // 3010: installed, reboot needed. 259: staged, but no matching device is
    // connected now; it binds when the board is next plugged in.
    if (code == ERROR_SUCCESS || code == ERROR_SUCCESS_REBOOT_REQUIRED || code == ERROR_NO_MORE_ITEMS)
        return InstallResult::Installed;
    detail = code;
    return InstallResult::Failed;
}

}
