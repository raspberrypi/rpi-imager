/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Whether this process can walk past the permissions on a file.
 *
 * A good many cases here set a file unreadable or unwritable and then check
 * that the code under test says so rather than carrying on. Run with enough
 * privilege none of that holds -- the open succeeds and the case proves
 * nothing -- so each one asks first and skips itself.
 *
 * Every one of them asked as ::geteuid() == 0, which is a question Windows
 * cannot answer: there are no euids, and the equivalent authority is carried
 * by the process token. Asked in one place instead, in the terms the cases
 * actually mean.
 */
#ifndef RPI_TEST_PLATFORM_PRIVILEGE_H
#define RPI_TEST_PLATFORM_PRIVILEGE_H

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rpi_test {

// True when the caller's permissions are not the limiting factor: root on
// POSIX, an elevated token on Windows.
inline bool isPrivileged()
{
#ifdef _WIN32
    // TokenElevation is what UAC leaves behind, and it is the thing that
    // decides whether this process may write where an ordinary one may not.
    // Membership of Administrators is not the same question: a filtered token
    // on an admin account is still an ordinary process until it is elevated.
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    const bool ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &returned) != 0;
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
#else
    return ::geteuid() == 0;
#endif
}

} // namespace rpi_test

// Building a boot.img on Windows drives diskpart: it attaches the image as a
// VDisk, partitions it, formats FAT32 and assigns a drive letter, all of which
// need an elevated process. Unelevated the call fails before it has built
// anything, and a case that goes on to inspect the image -- or to check which
// step reported the error -- is looking at the wrong failure. Elsewhere the
// image is assembled in-process with no privilege involved.
#ifdef _WIN32
#define REQUIRE_BOOT_IMG_SUPPORT()                                                 if (!rpi_test::isPrivileged())                                                 SKIP("building a boot.img here drives diskpart, which needs elevation")
#else
#define REQUIRE_BOOT_IMG_SUPPORT() ((void)0)
#endif

#endif // RPI_TEST_PLATFORM_PRIVILEGE_H
