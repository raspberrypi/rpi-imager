/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025-2026 Raspberry Pi Ltd
 *
 * boot.img, built in this process on every platform.
 *
 * DiskFormatter writes the filesystem and DeviceWrapperFatPartition fills it,
 * so no external tool, elevation or mount is needed and there is one
 * implementation to test rather than one per platform. Keep it that way: the
 * Debian packaging drops dosfstools and fdisk on the strength of it.
 *
 * The output is a bare FAT32 volume with no partition table, starting at
 * sector nought, which is what the bootloader is given.
 */

#include "bootimgcreator.h"

#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "disk_formatter.h"
#include "file_operations.h"

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
        qDebug() << "BootImgCreator: no files to pack";
        return false;
    }
    if (totalSize <= 0) {
        qDebug() << "BootImgCreator: refusing to build a boot.img of"
                 << totalSize << "bytes";
        return false;
    }

    qDebug() << "BootImgCreator: creating" << totalSize << "byte boot.img";

    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        qDebug() << "BootImgCreator: cannot create"
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
            qDebug() << "BootImgCreator: could not format boot.img, error"
                     << static_cast<int>(formatted.error());
            QFile::remove(outputPath);
            return false;
        }
    }

    auto ops = rpi_imager::FileOperations::Create();
    if (!ops || ops->OpenDevice(outputPath.toStdString()) !=
                    rpi_imager::FileError::kSuccess) {
        qDebug() << "BootImgCreator: could not reopen" << outputPath;
        QFile::remove(outputPath);
        return false;
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
        qDebug() << "BootImgCreator: failed to fill boot.img:" << e.what();
        ops->Close();
        QFile::remove(outputPath);
        return false;
    }

    ops->Close();
    qDebug() << "BootImgCreator: boot.img created successfully";
    return true;
}
