/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Answers the CLI's administrator check inside the harness binary.
 *
 * Cli::run() asks PlatformQuirks::hasElevatedPrivileges() before it looks at
 * a single argument, so an unelevated harness would stop there and none of
 * the refusals a script author actually meets would be reached. The check
 * ends in CheckTokenMembership, a DLL import, so a linker wrap answers it
 * without the product carrying a test seam.
 *
 * Driven by the environment rather than a switch, because the harness is
 * started as a subprocess: RPI_IMAGER_TEST_FAKE_ADMIN=1 reports membership,
 * anything else passes through to the real answer -- which is how the
 * "not running as Administrator" refusal is reached as well.
 */

#include <windows.h>

#include <cstdlib>

extern "C" {

using PfnCheckTokenMembership = BOOL(WINAPI *)(HANDLE, PSID, PBOOL);

extern PfnCheckTokenMembership __real___imp_CheckTokenMembership;

PfnCheckTokenMembership __wrap___imp_CheckTokenMembership = nullptr;

static BOOL WINAPI interposedCheckTokenMembership(HANDLE token, PSID group,
                                                  PBOOL isMember)
{
    // Read every call: a test drives this through the environment of a
    // process it starts, and caching it here would answer for the wrong run
    // if the harness were ever reused.
    const char *fake = std::getenv("RPI_IMAGER_TEST_FAKE_ADMIN");
    if (fake && fake[0] == '1') {
        if (isMember)
            *isMember = TRUE;
        return TRUE;
    }
    return __real___imp_CheckTokenMembership(token, group, isMember);
}

namespace {
struct Installer {
    Installer() { __wrap___imp_CheckTokenMembership = interposedCheckTokenMembership; }
};
Installer g_installer;
}  // namespace

}  // extern "C"
