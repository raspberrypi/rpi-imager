// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// What the callback relay accepts from the shell.
//
// The relay is registered as the handler for rpi-imager:// links, so the
// string it is started with comes from whatever a browser or another
// application asked Windows to open. Everything it does afterwards -- a TCP
// send to the running instance, or launching one with the URL as an argument
// -- follows from these two decisions, and neither was reachable from a test
// while both lived inside wWinMain.

#ifndef RPI_IMAGER_CALLBACK_RELAY_URL_H
#define RPI_IMAGER_CALLBACK_RELAY_URL_H

#include <windows.h>
#include <strsafe.h>

namespace rpi_relay {

// The scheme the relay is registered for, and the bounds either side of it.
inline constexpr wchar_t kScheme[] = L"rpi-imager://";
inline constexpr size_t kSchemeLen = 13;  // wcslen(kScheme)
inline constexpr size_t kMinUrlLen = 14;  // rpi-imager:// plus one character
inline constexpr size_t kMaxUrlLen = 2000;

// The URL argument, unquoted, copied into `out`.
//
// `cmdLine` is what wWinMain was handed, which for a ShellExecute-based call
// is the argument alone. When it is empty the whole command line is parsed
// instead, and the executable path -- quoted or not -- has to be stepped over
// first.
//
// Returns false when there is no argument, or it is longer than `out` holds,
// or it opens a quote it never closes.
inline bool extractUrl(const wchar_t *cmdLine, const wchar_t *fullCommandLine,
                       wchar_t *out, size_t outLen)
{
    if (!out || outLen == 0)
        return false;
    out[0] = L'\0';

    const wchar_t *url = cmdLine;
    if (!url || !*url) {
        url = fullCommandLine;
        if (url && *url == L'"') {
            url = wcschr(url + 1, L'"');
            if (url)
                ++url;
        } else if (url) {
            while (*url && *url != L' ')
                ++url;
        }
        while (url && *url == L' ')
            ++url;
    }
    if (!url || !*url)
        return false;

    if (*url == L'"') {
        const wchar_t *start = url + 1;
        const wchar_t *end = wcsrchr(start, L'"');
        if (!end)
            return false;
        const size_t len = static_cast<size_t>(end - start);
        if (len == 0 || len >= outLen)
            return false;
        wcsncpy_s(out, outLen, start, len);
        out[len] = L'\0';
        return true;
    }

    const size_t len = wcsnlen_s(url, outLen);
    if (len == 0 || len >= outLen)
        return false;
    wcsncpy_s(out, outLen, url, len);
    out[len] = L'\0';
    return true;
}

// Whether the relay will act on this URL at all.
//
// A control character is refused outright rather than escaped: the string is
// about to be handed to ShellExecuteExW as a parameter, and there is no
// legitimate rpi-imager:// link that carries one.
inline bool isAcceptableUrl(const wchar_t *url)
{
    if (!url)
        return false;
    if (wcsncmp(url, kScheme, kSchemeLen) != 0)
        return false;

    const size_t len = wcslen(url);
    if (len < kMinUrlLen || len > kMaxUrlLen)
        return false;

    for (size_t i = 0; i < len; ++i) {
        const wchar_t c = url[i];
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

} // namespace rpi_relay

#endif // RPI_IMAGER_CALLBACK_RELAY_URL_H
