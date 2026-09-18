/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Makes SetupDiGetClassDevsW fail, for the one case that needs it to.
 *
 * Windows will not refuse that call on demand, so the branch behind it --
 * the sentinel that puts "Drive enumeration failed" in front of the user
 * instead of an empty list reading as "no drives found" -- had no way to be
 * reached.
 *
 * The interception is a linker flag on this target alone
 * (--wrap=__imp_SetupDiGetClassDevsW); the drive list is compiled unchanged
 * and carries no seam for it. A call into a DLL is an indirect call through
 * __imp_<name>, which is a data symbol holding the address, so what --wrap
 * redirects is a *pointer*, not a function.
 *
 * Every call in this binary is intercepted, which is why it is a target of
 * its own with one case in it.
 */

#include <windows.h>
#include <setupapi.h>

using PfnGetClassDevsW = HDEVINFO(WINAPI *)(const GUID *, PCWSTR, HWND, DWORD);

static HDEVINFO WINAPI failingGetClassDevsW(const GUID *, PCWSTR, HWND, DWORD)
{
    // The error a caller would plausibly meet, and one the message carries
    // out by number so a bug report can say which it was.
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return INVALID_HANDLE_VALUE;
}

extern "C" {
PfnGetClassDevsW __wrap___imp_SetupDiGetClassDevsW = failingGetClassDevsW;
}
