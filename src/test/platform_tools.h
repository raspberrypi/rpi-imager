/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Where a fixture tool is, whoever installed it.
 *
 * The cases that build a filesystem, an archive or a key shell out to
 * mkfs.vfat, mtools, xz and friends, and each decided for itself where those
 * live -- /usr/bin and /bin in one file, /sbin and /usr/sbin in the next.
 * That is true of a distribution and of nowhere else. On a Mac the tools
 * arrive under /opt/homebrew or /usr/local, so a machine with all of them
 * installed still skipped 108 of the 209 cases it skipped, xz among them
 * while /opt/homebrew/bin/xz sat on the PATH the whole time.
 *
 * PATH is the answer to "is this installed", so ask it. The extra directories
 * are the ones a tool can be in while not being on an ordinary user's PATH:
 * the sbin pair on any Unix, and the Homebrew prefixes for both architectures
 * on macOS.
 */
#ifndef RPI_TEST_PLATFORM_TOOLS_H
#define RPI_TEST_PLATFORM_TOOLS_H

#include <QString>
#include <QStringList>
#include <QStandardPaths>

namespace rpi_test {

inline QStringList extraToolDirectories()
{
    return {
        QStringLiteral("/sbin"),
        QStringLiteral("/usr/sbin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/homebrew/sbin"),
        QStringLiteral("/opt/homebrew/bin"),
    };
}

// The tool's full path, or an empty string if it is not installed.
inline QString toolPath(const QString &name)
{
    const QString onPath = QStandardPaths::findExecutable(name);
    if (!onPath.isEmpty())
        return onPath;
    return QStandardPaths::findExecutable(name, extraToolDirectories());
}

inline bool haveTool(const QString &name)
{
    return !toolPath(name).isEmpty();
}

} // namespace rpi_test

#endif // RPI_TEST_PLATFORM_TOOLS_H
