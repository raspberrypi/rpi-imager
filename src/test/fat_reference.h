/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A reference filesystem, and what it takes to trust our own.
 *
 * A case that writes with the FAT driver and reads back with it agrees with
 * itself however wrong the on-disk result is. mtools is the second
 * implementation, and the two check each other both ways round:
 *
 *   ours read by theirs   what we wrote, listed and extracted by mtools
 *   theirs read by ours   what mtools wrote, listed and read by the driver
 *
 * A file in the wrong directory fails the first; a directory we cannot walk
 * fails the second.
 */

#ifndef RPI_TEST_FAT_REFERENCE_H
#define RPI_TEST_FAT_REFERENCE_H

#include <catch2/catch_test_macros.hpp>

#include "platform_tools.h"
#include "fixture_process.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace rpi_test {

// mtools refuses an image with no partition table unless told the image is
// the filesystem. Every call here is made the same way.
// `workingDir` matters more than it looks: mtools reads a leading "C:" as
// one of its own drive letters, not as a Windows path, and answers "Drive
// 'C:' not supported". So local files are named relative to a directory it
// is started in rather than by absolute path.
inline bool runMtool(const QString &tool, const QStringList &args,
                     QByteArray *stdOut = nullptr,
                     const QString &workingDir = QString())
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MTOOLS_SKIP_CHECK"), QStringLiteral("1"));
    proc.setProcessEnvironment(env);
    if (!workingDir.isEmpty())
        proc.setWorkingDirectory(workingDir);

    const QString path = toolPath(tool);
    proc.start(path.isEmpty() ? tool : path, args);
    if (!proc.waitForFinished(kFixtureProcessTimeoutMs))
        return false;
    if (stdOut)
        *stdOut = proc.readAllStandardOutput();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

inline bool haveMtoolsReference()
{
    static const bool found = haveTool(QStringLiteral("mmd")) &&
                              haveTool(QStringLiteral("mcopy")) &&
                              haveTool(QStringLiteral("mdir"));
    return found;
}

// Every path mtools can see in the image, as "dir/name", sorted.
//
// `mdir -/ -b` walks the whole tree and prints one full path per line, which
// is the one thing needed here: where each file actually is.
inline QStringList mtoolsPaths(const QString &imagePath)
{
    QByteArray out;
    if (!runMtool(QStringLiteral("mdir"),
                  {QStringLiteral("-i"), imagePath, QStringLiteral("-/"),
                   QStringLiteral("-b"), QStringLiteral("::/")},
                  &out))
        return {};

    QStringList paths;
    for (const QByteArray &line : out.split('\n')) {
        QString entry = QString::fromUtf8(line).trimmed();
        if (entry.isEmpty())
            continue;
        // "::/overlays/one.dtbo" -> "overlays/one.dtbo"; directories end in /.
        if (entry.startsWith(QStringLiteral("::/")))
            entry = entry.mid(3);
        if (entry.isEmpty() || entry.endsWith(QLatin1Char('/')))
            continue;
        paths << entry;
    }
    paths.sort();
    return paths;
}

// One file's contents, as mtools reads them out.
inline QByteArray mtoolsRead(const QString &imagePath, const QString &name,
                             const QString &scratchDir)
{
    const QString leaf = QStringLiteral("mtools-read.tmp");
    const QString out = QDir(scratchDir).filePath(leaf);
    QFile::remove(out);
    // Named relative to the working directory, because an absolute Windows
    // path starts with a drive letter mtools claims for itself.
    if (!runMtool(QStringLiteral("mcopy"),
                  {QStringLiteral("-i"), imagePath, QStringLiteral("-n"),
                   QStringLiteral("::/") + name, leaf},
                  nullptr, scratchDir))
        return {};

    QFile f(out);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = f.readAll();
    f.close();
    QFile::remove(out);
    return bytes;
}

// Build the reference: a FAT32 filesystem holding exactly `files`, made
// entirely by mtools. Directories are created for any path that needs them.
inline bool buildMtoolsReference(const QString &imagePath, int megabytes,
                                 const QMap<QString, QByteArray> &files,
                                 const QString &scratchDir)
{
    {
        QFile f(imagePath);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        if (!f.resize(qint64(megabytes) * 1024 * 1024))
            return false;
    }
    if (!runMtool(QStringLiteral("mformat"),
                  {QStringLiteral("-i"), imagePath, QStringLiteral("-F"),
                   QStringLiteral("::")}))
        return false;

    // Directories first, outermost in, because mmd will not make a parent.
    QStringList made;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QStringList parts = it.key().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString sofar;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            sofar += (i ? QStringLiteral("/") : QString()) + parts[i];
            if (made.contains(sofar))
                continue;
            if (!runMtool(QStringLiteral("mmd"),
                          {QStringLiteral("-i"), imagePath,
                           QStringLiteral("::/") + sofar}))
                return false;
            made << sofar;
        }
    }

    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString stagedLeaf =
            QStringLiteral("stage-%1.bin").arg(it.key().size() * 31 + made.size());
        const QString staged = QDir(scratchDir).filePath(stagedLeaf);
        {
            QFile f(staged);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            if (f.write(it.value()) != it.value().size())
                return false;
        }
        const bool ok = runMtool(QStringLiteral("mcopy"),
                                 {QStringLiteral("-i"), imagePath, stagedLeaf,
                                  QStringLiteral("::/") + it.key()},
                                 nullptr, scratchDir);
        QFile::remove(staged);
        if (!ok)
            return false;
    }
    return true;
}

} // namespace rpi_test

#endif // RPI_TEST_FAT_REFERENCE_H
