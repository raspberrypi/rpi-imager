/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * boot.img, built in this process.
 *
 * This used to create the output with QFile::resize -- a raw file of the
 * right length -- and then ask diskpart to `select vdisk file=` it, attach
 * it, partition it, format it and hand it a drive letter. That cannot work:
 * `select vdisk` wants a real VHD, with the footer that makes it one, and a
 * raw file is not one. diskpart refused every time, and reported it only as
 * an empty error message, because the code read its standard error and
 * diskpart writes to standard output. Secure boot could not build a boot.img
 * on Windows at all, and had not been able to for as long as the code has
 * been there.
 *
 * Everything needed to do it properly was already in the tree: DiskFormatter
 * writes a FAT32 filesystem, and DeviceWrapperFatPartition writes files into
 * one, subdirectories included. Neither needs diskpart, a virtual disk, a
 * drive letter or an elevated process -- so this now works from an ordinary
 * run, and the cases that cover it no longer need one either.
 *
 * The output is a bare FAT32 filesystem with no partition table, which is
 * what `mkfs.vfat` over a whole file produces on the POSIX hosts and what
 * the bootloader is given.
 */

#include "bootimgcreator.h"

#include "../devicewrapper.h"
#include "../devicewrapperfatpartition.h"
#include "../disk_formatter.h"
#include "../file_operations.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <exception>
#include <memory>

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray> &files,
                                   const QString &outputPath,
                                   qint64 totalSize)
{
    if (files.isEmpty()) {
        qDebug() << "BootImgCreator (Windows): no files to pack";
        return false;
    }
    if (totalSize <= 0) {
        qDebug() << "BootImgCreator (Windows): refusing to build a boot.img of"
                 << totalSize << "bytes";
        return false;
    }

    qDebug() << "BootImgCreator (Windows): creating" << totalSize << "byte boot.img";

    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        qDebug() << "BootImgCreator (Windows): cannot create"
                 << outputInfo.absolutePath();
        return false;
    }

    // Anything already there is ours to replace, and a stale image left
    // behind would be signed and served as though it were this one.
    QFile::remove(outputPath);

    {
        rpi_imager::DiskFormatter formatter;
        const auto formatted = formatter.FormatFilesystemOnly(
            outputPath.toStdString(), static_cast<std::uint64_t>(totalSize));
        if (!formatted) {
            qDebug() << "BootImgCreator (Windows): could not format boot.img, error"
                     << static_cast<int>(formatted.error());
            QFile::remove(outputPath);
            return false;
        }
    }

    auto ops = rpi_imager::FileOperations::Create();
    if (!ops || ops->OpenDevice(outputPath.toStdString()) !=
                    rpi_imager::FileError::kSuccess) {
        qDebug() << "BootImgCreator (Windows): could not reopen" << outputPath;
        QFile::remove(outputPath);
        return false;
    }

    // Checked before anything is written, so a set we cannot place whole
    // leaves no half-filled image behind to be signed and served.
    //
    // DeviceWrapperFatPartition::writeFile refuses a path with a directory in
    // it -- getDirEntry() starts by seeking back to the root, so an entry
    // meant for a subdirectory ends up at the top level instead, which is
    // worse than refusing. Until it can, a boot image needing overlays/ is
    // one this cannot build.
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key().contains(QLatin1Char('/')) || it.key().contains(QLatin1Char('\\'))) {
            qDebug() << "BootImgCreator (Windows): cannot place" << it.key()
                     << "-- the FAT writer does not create subdirectories yet";
            QFile::remove(outputPath);
            return false;
        }
    }

    try {
        DeviceWrapper dw(ops.get());
        // From nought, over the whole file: no partition table in front of it.
        DeviceWrapperFatPartition fat(&dw, 0, static_cast<quint64>(totalSize));

        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            // Written with forward slashes whatever the caller used, because
            // that is what a path inside a FAT directory is.
            QString name = it.key();
            name.replace(QLatin1Char('\\'), QLatin1Char('/'));
            while (name.startsWith(QLatin1Char('/')))
                name.remove(0, 1);
            if (name.isEmpty())
                continue;

            fat.writeFile(name, it.value());
        }
        dw.sync();
    } catch (const std::exception &e) {
        // The FAT writer reports a full or malformed filesystem by throwing,
        // and the callers here only check for false. A half-filled image left
        // on disk would be signed and served as though it were whole.
        qDebug() << "BootImgCreator (Windows): failed to fill boot.img:" << e.what();
        ops->Close();
        QFile::remove(outputPath);
        return false;
    }

    ops->Close();
    qDebug() << "BootImgCreator (Windows): boot.img created successfully";
    return true;
}
