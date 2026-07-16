/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootimgcreator.h"
#include "unix/bootimgcreator_unix.h"
#include <QProcess>
#include <QDebug>

BootImgCreatorUnix::setPlatformName("MacOS");

bool BootImgCreatorUnix::attachDiskImage(const QString& imagePath, QString& device) {
    QProcess hdiutilAttach;
    hdiutilAttach.start("hdiutil", QStringList() << "attach" << "-nomount" << imagePath);
    if (!hdiutilAttach.waitForFinished(10000) || hdiutilAttach.exitCode() != 0) {
        qDebug() << "BootImgCreator (macOS): hdiutil attach failed";
        return false;
    }
    device = QString(hdiutilAttach.readAllStandardOutput()).trimmed();
    return true;
}

bool BootImgCreatorUnix::detachDiskImage(const QString& device) {
    QProcess::execute("hdiutil", QStringList() << "detach" << device);
    return true;
}

bool BootImgCreatorUnix::mountFilesystem(const QString& device, const QString& mountPoint) {
    QProcess mountProc;
    mountProc.start("mount", QStringList() << "-t" << "msdos" << device << mountPoint);
    if (!mountProc.waitForFinished(10000) || mountProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (macOS): mount failed:" << mountProc.readAllStandardError();
        return false;
    }
    return true;
}

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray>& files,
                                   const QString& outputPath,
				   qint64 totalSize) {
    return BootImgCreatorUnix::createBootImg(files, outputPath, totalSize);
}
