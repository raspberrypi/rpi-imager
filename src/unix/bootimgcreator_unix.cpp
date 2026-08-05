/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * This file contains shared methods used by both FreeBSD and OSX
 * to implement boot image creation.
 */

#include "bootimgcreator_unix.h"
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>
#include <QSet>

bool BootImgCreatorUnix::createBootImg(const QMap<QString, QByteArray> &files,
                                             const QString &outputPath,
                                             qint64 totalSize)
{
    if (files.isEmpty()) {
        qDebug() << "BootImgCreator (" << platformName << "): no files to pack";
        return false;
    }

    qDebug() << "BootImgCreator (" << platformName << "): creating" << totalSize << "byte boot.img";

    // Ensure parent directory exists
    QFileInfo outputInfo(outputPath);
    QDir().mkpath(outputInfo.absolutePath());

    // Create empty disk image (without filesystem - we'll format it below)
    QFile imgFile(outputPath);
    if (!imgFile.open(QIODevice::WriteOnly)) {
        qDebug() << "BootImgCreator (" << platformName << "): failed to create" << outputPath;
        return false;
    }
    if (!imgFile.resize(totalSize)) {
        qDebug() << "BootImgCreator (" << platformName << "): failed to resize to" << totalSize;
        imgFile.close();
        return false;
    }
    imgFile.close();

    // Attach the disk image
    QString device;
    if (!attachDiskImage(outputPath, device)) {
        qDebug() << "BootImgCreator (" << platformName << "): failed to attach disk image";
        return false;
    }

    qDebug() << "BootImgCreator (" << platformName << "): attached boot.img as" << device;

    // Format with newfs_msdos (now on the device, not the file)
    QProcess newfsProc;
    newfsProc.start("newfs_msdos", QStringList() << "-F" << "32" << device);
    if (!newfsProc.waitForFinished(30000) || newfsProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (" << platformName << "): newfs_msdos failed:"
                 << newfsProc.readAllStandardError();
        detachDiskImage(device);
        return false;
    }

    // Mount the filesystem
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "BootImgCreator (" << platformName << "): failed to create temp directory";
        detachDiskImage(device);
        return false;
    }

    QString mountPoint = tempDir.path() + "/mnt";
    QDir().mkpath(mountPoint);

    if (!mountFilesystem(device, mountPoint)) {
        qDebug() << "BootImgCreator (" << platformName << "): mount failed";
        detachDiskImage(device);
        return false;
    }

    // Copy all files to the mounted filesystem
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        QString filePath = mountPoint + "/" + it.key();

        // Create parent directory if needed
        QFileInfo fileInfo(filePath);
        QDir().mkpath(fileInfo.absolutePath());

        QFile outFile(filePath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            qDebug() << "BootImgCreator (" << platformName << "): failed to create" << filePath;
            continue;
        }
        outFile.write(it.value());
        outFile.close();
    }

    // Unmount and detach
    QProcess::execute("umount", QStringList() << mountPoint);
    detachDiskImage(device);

    qDebug() << "BootImgCreator (" << platformName << "): boot.img created successfully";
    return true;
}
