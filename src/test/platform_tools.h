/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Where a fixture tool is, whoever installed it.
 *
 * The cases that build a filesystem, an archive or a key shell out to
 * mkfs.vfat, mtools, xz and friends, and each decided for itself where those
 * live -- /usr/bin and /bin in one file, /sbin and /usr/sbin in the next.
 */
#ifndef RPI_TEST_PLATFORM_TOOLS_H
#define RPI_TEST_PLATFORM_TOOLS_H

#include <QString>
#include <QStringList>
#include <QProcess>
#include <QStandardPaths>

#include <QtGlobal>

namespace rpi_test {

inline QStringList extraToolDirectories()
{
#ifdef Q_OS_WIN
    // Windows has none of these tools by default, but a developer machine
    // almost always carries two copies already: Git for Windows ships a
    // POSIX userland (gzip, bzip2, tar, openssl, dd, truncate) and MSYS2
    // adds the rest (xz, zstd, and mtools/dosfstools once installed).
    //
    // Named here rather than left to PATH because neither installer puts its
    // usr/bin on PATH -- doing so would shadow Windows' own find.exe and
    // sort.exe -- so the tools are present and invisible. Without this the
    // fixtures that build an archive or a filesystem all skip, which is most
    // of what the compression and FAT cases are for.
    QStringList dirs;
    for (const QString &root : {QStringLiteral("C:/Program Files/Git"),
                                QStringLiteral("C:/Program Files (x86)/Git"),
                                QStringLiteral("C:/msys64"),
                                QStringLiteral("C:/msys32")}) {
        dirs << root + QStringLiteral("/usr/bin");
        dirs << root + QStringLiteral("/mingw64/bin");
    }
    return dirs;
#else
    return {
        QStringLiteral("/sbin"),
        QStringLiteral("/usr/sbin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/opt/homebrew/sbin"),
        QStringLiteral("/opt/homebrew/bin"),
    };
#endif
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

// A working Python 3, or empty if there is none.
//
// Named "python3" on the Unixes and "python" on Windows, and several cases
// hardcoded /usr/bin/python3 -- a path no Windows machine has, so the local
// HTTP servers those fixtures stand up never started and every case behind
// them skipped.
//
// Each candidate is run before it is believed. Windows ships an App Execution
// Alias called python.exe that opens the Store rather than an interpreter, and
// it answers findExecutable() exactly as a real one would.
inline QString pythonPath()
{
    static const QString found = [] {
        for (const QString &name : {QStringLiteral("python3"),
                                    QStringLiteral("python")}) {
            const QString candidate = toolPath(name);
            if (candidate.isEmpty())
                continue;
            QProcess probe;
            probe.start(candidate, {QStringLiteral("-c"),
                                    QStringLiteral("print(1)")});
            if (!probe.waitForFinished(10000))
                continue;
            if (probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0
                && probe.readAllStandardOutput().trimmed() == "1")
                return candidate;
        }
        return QString();
    }();
    return found;
}

inline bool havePython() { return !pythonPath().isEmpty(); }

// A POSIX shell to run a fixture script with, or empty if there is none.
//
// The fixture scripts are sh, and a good few tests hardcoded "/bin/sh" to run
// them -- a path that does not exist on Windows, so every one of those cases
// failed rather than skipping. Git for Windows and MSYS2 both ship a real sh,
// and toolPath() knows where to look for it.
inline QString shellPath()
{
#ifdef Q_OS_WIN
    const QString sh = toolPath(QStringLiteral("sh"));
    return sh.isEmpty() ? toolPath(QStringLiteral("bash")) : sh;
#else
    return QStringLiteral("/bin/sh");
#endif
}

inline bool haveShell() { return !shellPath().isEmpty(); }

} // namespace rpi_test

#endif // RPI_TEST_PLATFORM_TOOLS_H
