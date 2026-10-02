/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "../userfiles.h"

#include <QFile>
#include <QStandardPaths>

#include <vector>
#include <windows.h>

namespace PlatformQuirks {

namespace {

bool processIsElevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = ::GetTokenInformation(token, TokenElevation, &elevation,
                                                sizeof(elevation), &size)
                          && elevation.TokenIsElevated;
    ::CloseHandle(token);
    return elevated;
}

std::vector<BYTE> tokenUser(HANDLE token)
{
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size == 0 || !::GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
        buffer.clear();
    return buffer;
}

bool sameUserAsProcess(HANDLE token)
{
    HANDLE own = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &own))
        return false;
    const std::vector<BYTE> a = tokenUser(own);
    const std::vector<BYTE> b = tokenUser(token);
    ::CloseHandle(own);
    return !a.empty() && !b.empty() &&
           ::EqualSid(reinterpret_cast<const TOKEN_USER*>(a.data())->User.Sid,
                      reinterpret_cast<const TOKEN_USER*>(b.data())->User.Sid);
}

// The shell runs unelevated as the same account: its token is the one this
// process had before UAC raised it.
HANDLE unelevatedShellToken()
{
    DWORD pid = 0;
    if (HWND shell = ::GetShellWindow())
        ::GetWindowThreadProcessId(shell, &pid);
    if (pid == 0)
        return nullptr;

    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return nullptr;
    HANDLE token = nullptr;
    HANDLE impersonation = nullptr;
    if (::OpenProcessToken(process, TOKEN_DUPLICATE | TOKEN_QUERY, &token)) {
        if (sameUserAsProcess(token))
            ::DuplicateTokenEx(token, TOKEN_IMPERSONATE | TOKEN_QUERY, nullptr,
                               SecurityImpersonation, TokenImpersonation, &impersonation);
        ::CloseHandle(token);
    }
    ::CloseHandle(process);
    return impersonation;
}

} // namespace

InvokingUserFsScope::InvokingUserFsScope()
{
    if (!processIsElevated())
        return;
    HANDLE shell = unelevatedShellToken();
    if (!shell)
        return;

    HANDLE previous = nullptr;
    if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &previous))
        previous = nullptr;
    if (::SetThreadToken(nullptr, shell)) {
        _savedToken = previous;
        _active = true;
    } else if (previous) {
        ::CloseHandle(previous);
    }
    ::CloseHandle(shell);
}

InvokingUserFsScope::~InvokingUserFsScope()
{
    if (!_active)
        return;
    ::SetThreadToken(nullptr, static_cast<HANDLE>(_savedToken));
    if (_savedToken)
        ::CloseHandle(static_cast<HANDLE>(_savedToken));
}

bool invokingUser(int* uid, int* gid)
{
    *uid = -1;
    *gid = -1;
    return false;
}

void runAsInvokingUser(QString&, QStringList&) {}

bool openFreshFile(QFile& file)
{
    // Removing a link removes the link, and CREATE_NEW refuses anything left.
    QFile::remove(file.fileName());
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
}

FILE* openLogFile(const char* path)
{
    return std::fopen(path, "a");
}

int reclaimOwnership(const QString&, int, int, bool) { return 0; }

QString trustedCacheLocation()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

} // namespace PlatformQuirks
