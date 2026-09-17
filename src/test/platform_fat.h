/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * "Give me a FAT filesystem in this file", however this machine can do it.
 *
 * Ninety-two cases build one to test against, and every one of them called
 * mkfs.vfat directly. That is a dosfstools program: on a Mac it is not there
 * unless somebody installed it, so all ninety-two skipped -- the largest
 * single block of skips in the suite, and they cover the FAT driver, the disk
 * formatter and the boot image writer, which is to say the code that decides
 * what actually lands on a card.
 */
#ifndef RPI_TEST_PLATFORM_FAT_H
#define RPI_TEST_PLATFORM_FAT_H

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "fixture_process.h"
#include "platform_tools.h"

namespace rpi_test {

inline bool haveFatFormatter()
{
    if (haveTool(QStringLiteral("mkfs.vfat")))
        return true;
    // mtools writes a filesystem into a plain file, so it needs no block device
    // and no privilege. It is the only formatter available on Windows, where
    // dosfstools has no build at all, and it is the one a Mac is most likely to
    // have already.
    if (haveTool(QStringLiteral("mformat")))
        return true;
#ifdef Q_OS_MACOS
    return haveTool(QStringLiteral("newfs_msdos")) && haveTool(QStringLiteral("hdiutil"));
#else
    return false;
#endif
}

// What to say when there is none, so every case says the same thing.
inline const char *noFatFormatterReason()
{
    return "no FAT formatter here (mkfs.vfat, mformat from mtools, or "
           "newfs_msdos with hdiutil on macOS)";
}

namespace detail {

inline bool runFixtureTool(const QString &tool, const QStringList &args, QString *error,
                           QString *stdOut = nullptr,
                           const QProcessEnvironment *env = nullptr)
{
    QProcess proc;
    if (env)
        proc.setProcessEnvironment(*env);
    proc.start(tool, args);
    if (!proc.waitForFinished(kFixtureProcessTimeoutMs)) {
        if (error)
            *error = tool + QStringLiteral(" did not finish");
        proc.kill();
        proc.waitForFinished(1000);
        return false;
    }
    if (stdOut)
        *stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        if (error) {
            *error = tool + QStringLiteral(" failed: ") +
                     QString::fromUtf8(proc.readAllStandardError()).trimmed();
        }
        return false;
    }
    return true;
}

} // namespace detail

// Write a FAT filesystem over `imagePath`, which must already exist at the
// size wanted. True on success; `error` says what went wrong if not.
inline bool makeFatFilesystem(const QString &imagePath, int fatBits,
                              const QString &label = QString(), QString *error = nullptr)
{
    const QString mkfs = toolPath(QStringLiteral("mkfs.vfat"));
    if (!mkfs.isEmpty()) {
        QStringList args{QStringLiteral("-F"), QString::number(fatBits)};
        if (!label.isEmpty())
            args << QStringLiteral("-n") << label;
        args << imagePath;
        return detail::runFixtureTool(mkfs, args, error);
    }

    // mformat, from mtools, writes a FAT filesystem straight into an image file
    // without mounting it or needing a block device -- which is the whole of
    // what this helper wants, and the only way to get one on Windows, where
    // there is no mkfs.vfat to install: dosfstools has no Windows build, and
    // the OS's own format(1) works on volumes rather than files.
    //
    // Tried on every platform rather than under an #ifdef, because a host with
    // mtools and no dosfstools is not particular to Windows -- it is the
    // ordinary state of a Mac.
    const QString mformat = toolPath(QStringLiteral("mformat"));
    if (!mformat.isEmpty()) {
        const QFileInfo info(imagePath);
        const qint64 sectors = info.size() / 512;
        if (sectors <= 0) {
            if (error)
                *error = QStringLiteral("the image has no size to format");
            return false;
        }

        // -F asks for FAT32; without it mformat sizes the FAT from the cluster
        // count, which lands on FAT16 for every image this suite builds. There
        // is no flag for "FAT16 exactly", so the result is checked below rather
        // than assumed.
        QStringList args{QStringLiteral("-i"), imagePath,
                         QStringLiteral("-T"), QString::number(sectors)};
        if (fatBits == 32)
            args << QStringLiteral("-F");
        if (!label.isEmpty())
            args << QStringLiteral("-v") << label;
        args << QStringLiteral("::");

        // MTOOLS_SKIP_CHECK: the image is a bare filesystem with no partition
        // table, and mtools otherwise refuses the geometry as non-standard.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("MTOOLS_SKIP_CHECK"), QStringLiteral("1"));
        if (!detail::runFixtureTool(mformat, args, error, nullptr, &env))
            return false;

        // What was asked for is what was made. A silently-FAT12 image would
        // send a case looking for a bug in the driver that was never there.
        const QString minfo = toolPath(QStringLiteral("minfo"));
        if (!minfo.isEmpty()) {
            QString info2;
            if (detail::runFixtureTool(minfo, {QStringLiteral("-i"), imagePath,
                                               QStringLiteral("::")},
                                       nullptr, &info2, &env)) {
                const QString wanted = QStringLiteral("FAT%1").arg(fatBits);
                if (!info2.contains(wanted)) {
                    if (error)
                        *error = QStringLiteral("mformat produced a filesystem that is "
                                                "not %1").arg(wanted);
                    return false;
                }
            }
        }
        return true;
    }

#ifdef Q_OS_MACOS
    const QString hdiutil = toolPath(QStringLiteral("hdiutil"));
    const QString newfs = toolPath(QStringLiteral("newfs_msdos"));
    if (hdiutil.isEmpty() || newfs.isEmpty()) {
        if (error)
            *error = QString::fromLatin1(noFatFormatterReason());
        return false;
    }

    // CRawDiskImage, because the file is a bare filesystem image rather than
    // anything hdiutil would recognise on its own; -nomount, because the
    // filesystem is not there yet and there would be nothing to mount.
    QString attached;
    if (!detail::runFixtureTool(hdiutil,
                                {QStringLiteral("attach"), QStringLiteral("-imagekey"),
                                 QStringLiteral("diskimage-class=CRawDiskImage"),
                                 QStringLiteral("-nomount"), imagePath},
                                error, &attached)) {
        return false;
    }

    const QString device = attached.split(QLatin1Char('\n')).value(0).trimmed()
                               .split(QLatin1Char(' ')).value(0).trimmed();
    if (device.isEmpty()) {
        if (error)
            *error = QStringLiteral("hdiutil attached nothing");
        return false;
    }

    QStringList args{QStringLiteral("-F"), QString::number(fatBits)};
    if (!label.isEmpty())
        args << QStringLiteral("-v") << label;
    args << device;
    QString formatError;
    const bool formatted = detail::runFixtureTool(newfs, args, &formatError);

    // Detach whatever happened: a device left attached outlives the test and
    // the next run finds the machine one /dev/disk busier than it left it.
    QString detachError;
    detail::runFixtureTool(hdiutil, {QStringLiteral("detach"), device}, &detachError);

    if (!formatted && error)
        *error = formatError;
    return formatted;
#else
    if (error)
        *error = QString::fromLatin1(noFatFormatterReason());
    return false;
#endif
}

} // namespace rpi_test

#endif // RPI_TEST_PLATFORM_FAT_H
