/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Paths that mean the same on every platform the tests run on.
 *
 * A test wanting somewhere unwritable has until now used a Unix absolute path.
 * Windows resolves a leading slash against the current drive, and a volume
 * root's default ACL lets Authenticated Users create directories. Code calling
 * create_directories() then succeeds, the CHECK_FALSE fails inexplicably, and
 * the run leaves files at the root of the system drive -- one was an RSA
 * private key.
 *
 * unwritablePath() answers with something the host refuses outright: an unused
 * drive letter on Windows, which holds when elevated too, and the root-owned /
 * on POSIX.
 */

#ifndef RPI_TEST_PLATFORM_PATHS_H
#define RPI_TEST_PLATFORM_PATHS_H

#include <QDir>
#include <QString>
#include <QStringLiteral>

#ifdef _WIN32
#include <windows.h>
#endif

namespace rpi_test {

// The directory half, without a trailing slash.
inline QString unwritableDir()
{
#ifdef _WIN32
    // Highest unused letter, so a test machine with many mapped drives is
    // still served. Z: first because it is the conventional spare, and a
    // machine that has mapped every letter from D to Z is one we would want to
    // hear about rather than quietly work around.
    static const QString dir = [] {
        const DWORD mask = GetLogicalDrives();
        for (int letter = 'Z'; letter >= 'D'; --letter) {
            if (!(mask & (1u << (letter - 'A'))))
                return QStringLiteral("%1:/rpi-imager-nowhere")
                    .arg(QChar::fromLatin1(char(letter)));
        }
        // Every letter is taken. A reserved device name cannot be a directory
        // either, so creation below it still fails -- just with a different
        // error.
        return QStringLiteral("C:/NUL/rpi-imager-nowhere");
    }();
    return dir;
#else
    return QStringLiteral("/nonexistent-rpi-imager-dir/deeper");
#endif
}

// A file below it. `leaf` may itself contain slashes.
inline QString unwritablePath(const QString &leaf)
{
    return unwritableDir() + QLatin1Char('/') + leaf;
}

// True when the directory really is absent, which is what the cases above
// depend on. Somewhere for a test to assert rather than assume.
inline bool unwritableDirIsAbsent()
{
    return !QDir(unwritableDir()).exists();
}

} // namespace rpi_test

#endif // RPI_TEST_PLATFORM_PATHS_H
