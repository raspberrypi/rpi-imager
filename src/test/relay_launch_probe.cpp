/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Stands in for rpi-imager.exe so the relay's launch path can be tested.
 *
 * When no Imager is listening, the relay falls back to starting one beside
 * itself and handing it the callback URL -- the Pi Connect sign-in path for
 * anybody whose Imager is not already open. Nothing exercised it, because
 * exercising it meant launching the real binary, which asks for
 * administrator and would put a UAC prompt in front of the suite.
 *
 * Copied into a scratch directory under the name the relay looks for, it
 * records the arguments it was given and exits. A GUI subsystem binary like
 * the one it replaces, so ShellExecuteEx treats it the same way.
 */

#include <windows.h>
#include <shlwapi.h>

#include <cstdio>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR cmdLine, int)
{
    // Where to record what arrived. The relay passes no environment of its
    // own, so this is inherited from whoever started the relay.
    wchar_t out[MAX_PATH];
    if (!GetEnvironmentVariableW(L"RPI_RELAY_PROBE_OUTPUT", out, MAX_PATH))
        return 1;

    HANDLE h = CreateFileW(out, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return 1;

    // UTF-8, so a test reading it back does not have to care about the
    // console code page; the URL is the point and it may not be ASCII.
    char utf8[4096];
    const int n = WideCharToMultiByte(CP_UTF8, 0, cmdLine, -1, utf8,
                                      sizeof(utf8), nullptr, nullptr);
    if (n > 0) {
        DWORD written = 0;
        WriteFile(h, utf8, static_cast<DWORD>(n - 1), &written, nullptr);
    }
    CloseHandle(h);
    return 0;
}
