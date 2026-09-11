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
#ifdef Q_OS_MACOS
    return haveTool(QStringLiteral("newfs_msdos")) && haveTool(QStringLiteral("hdiutil"));
#else
    return false;
#endif
}

// What to say when there is none, so every case says the same thing.
inline const char *noFatFormatterReason()
{
    return "no FAT formatter here (mkfs.vfat, or newfs_msdos with hdiutil on macOS)";
}

namespace detail {

inline bool runFixtureTool(const QString &tool, const QStringList &args, QString *error,
                           QString *stdOut = nullptr)
{
    QProcess proc;
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
