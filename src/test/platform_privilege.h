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
 * Asked here rather than as ::geteuid() == 0, which Windows cannot answer:
 * there are no euids, and the equivalent authority is carried by the process
 * token.
 */
#ifndef RPI_TEST_PLATFORM_PRIVILEGE_H
#define RPI_TEST_PLATFORM_PRIVILEGE_H

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "platform_tools.h"

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

// What a boot.img needs, which differs by platform.
//
// Windows builds it in this process now -- DiskFormatter lays down the FAT32
// and DeviceWrapperFatPartition writes the files into it -- so it needs
// nothing at all. It used to drive diskpart, which needed elevation and
// never worked anyway.
//
// Linux and macOS still format with mkfs.vfat, which lives in sbin and is
// not always installed.
#ifdef _WIN32
#define REQUIRE_BOOT_IMG_SUPPORT() ((void)0)
#else
#define REQUIRE_BOOT_IMG_SUPPORT() \
    if (!rpi_test::haveTool(QStringLiteral("mkfs.vfat"))) \
    SKIP("mkfs.vfat is not installed, so no boot.img can be built")
#endif

// A boot image carrying a directory needs no more than the above: the FAT
// writer creates the directory and places the file in it, cross-checked
// against mtools in fat_subdirectory_test.cpp.
#define REQUIRE_NESTED_BOOT_IMG_SUPPORT() REQUIRE_BOOT_IMG_SUPPORT()

#endif // RPI_TEST_PLATFORM_PRIVILEGE_H
