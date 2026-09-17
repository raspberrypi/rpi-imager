/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What the callback relay accepts from the shell.
 *
 * The relay is what Windows starts when something opens an rpi-imager:// link,
 * so its argument comes from a browser, a mail client, or anything else that
 * can ask the shell to open a URL. It had no test of any kind: the whole file
 * is a WIN32 executable that nothing links, so it sat at nought per cent while
 * being the one place untrusted input enters the product on this platform.
 */

#include <catch2/catch_test_macros.hpp>

#include "windows/callback_relay_url.h"

#include <string>

namespace {

// The URL the relay would act on, or an empty string if it would not.
std::wstring accepted(const wchar_t *cmdLine,
                      const wchar_t *fullCommandLine = L"relay.exe")
{
    wchar_t buf[2048];
    if (!rpi_relay::extractUrl(cmdLine, fullCommandLine, buf, _countof(buf)))
        return {};
    if (!rpi_relay::isAcceptableUrl(buf))
        return {};
    return buf;
}

} // namespace

TEST_CASE("A plain callback URL is taken as it stands", "[relay]")
{
    CHECK(accepted(L"rpi-imager://open?token=abc") == L"rpi-imager://open?token=abc");
}

TEST_CASE("A quoted callback URL has its quotes taken off", "[relay]")
{
    // What the shell hands over when the URL contains a space or an ampersand.
    CHECK(accepted(L"\"rpi-imager://open?a=1&b=2\"") == L"rpi-imager://open?a=1&b=2");
}

TEST_CASE("An unclosed quote is refused rather than guessed at", "[relay]")
{
    CHECK(accepted(L"\"rpi-imager://open").empty());
}

TEST_CASE("Nothing at all is refused", "[relay]")
{
    CHECK(accepted(L"").empty());
    CHECK(accepted(nullptr, L"relay.exe").empty());
}

TEST_CASE("A URL of another scheme is refused", "[relay]")
{
    // The relay forwards whatever it accepts to the running instance, so the
    // scheme check is what keeps it from becoming a general-purpose courier.
    CHECK(accepted(L"http://example.com/").empty());
    CHECK(accepted(L"file:///C:/Windows/System32/calc.exe").empty());
    CHECK(accepted(L"rpi-imager:/open").empty());
    CHECK(accepted(L"RPI-IMAGER://open").empty());
}

TEST_CASE("A scheme with nothing after it is refused", "[relay]")
{
    CHECK(accepted(L"rpi-imager://").empty());
}

TEST_CASE("A control character anywhere in the URL is refused", "[relay]")
{
    // The accepted string is handed to ShellExecuteExW as a parameter. A
    // newline or a carriage return in it is not something a genuine link
    // carries, and this is the last place either can be rejected cheaply.
    CHECK(accepted(L"rpi-imager://open\nmalicious").empty());
    CHECK(accepted(L"rpi-imager://open\rmalicious").empty());
    CHECK(accepted(L"rpi-imager://open\ttab").empty());
    CHECK(accepted(L"rpi-imager://open\x01").empty());
    CHECK(accepted(L"rpi-imager://open\x7F").empty());
}

TEST_CASE("A URL longer than the relay will carry is refused", "[relay]")
{
    const std::wstring tooLong =
        std::wstring(L"rpi-imager://") + std::wstring(2100, L'a');
    CHECK(accepted(tooLong.c_str()).empty());

    // And one just inside the limit is not.
    const std::wstring allowed =
        std::wstring(L"rpi-imager://") + std::wstring(100, L'a');
    CHECK(accepted(allowed.c_str()) == allowed);
}

TEST_CASE("A URL too long for the buffer is refused, not truncated", "[relay]")
{
    // Truncating would hand on a different URL from the one that was asked
    // for, which is worse than refusing.
    wchar_t small[16];
    CHECK_FALSE(rpi_relay::extractUrl(L"rpi-imager://open?token=abcdef", L"relay.exe",
                                      small, _countof(small)));
}

TEST_CASE("An empty command line falls back to parsing the whole one",
          "[relay]")
{
    // wWinMain is handed the argument alone for a ShellExecute-based call, but
    // not for every way the relay can be started, so the executable path has
    // to be stepped over -- quoted, as the shell writes it, or bare.
    CHECK(accepted(L"", L"\"C:/Program Files/rpi-imager/relay.exe\" rpi-imager://open")
          == L"rpi-imager://open");
    CHECK(accepted(L"", L"relay.exe rpi-imager://open") == L"rpi-imager://open");
    CHECK(accepted(L"", L"relay.exe    rpi-imager://open") == L"rpi-imager://open");
}

TEST_CASE("A command line with no argument after the executable is refused",
          "[relay]")
{
    CHECK(accepted(L"", L"relay.exe").empty());
    CHECK(accepted(L"", L"\"C:/rpi-imager/relay.exe\"").empty());
    CHECK(accepted(L"", L"\"C:/rpi-imager/relay.exe\" ").empty());
}

TEST_CASE("A path outside Latin-1 in the URL is carried through", "[relay][i18n]")
{
    // Non-ASCII above the control range is left alone: a callback can name a
    // resource in any language, and the relay converts to UTF-8 when it sends.
    const std::wstring url = L"rpi-imager://open?name=\u6e2c\u8a66";
    CHECK(accepted(url.c_str()) == url);
}
