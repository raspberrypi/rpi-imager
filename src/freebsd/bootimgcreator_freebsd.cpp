/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootimgcreator.h"
#include "unix/bootimgcreator_unix.h"
#include <QProcess>
#include <QDebug>

BootImgCreatorUnix::setPlatformName("FreeBSD");

bool BootImgCreatorUnix::attachDiskImage(const QString& imagePath, QString& device) {
    QProcess mdconfigCreate;
    mdconfigCreate.start("mdconfig", QStringList() << "-f" << imagePath);
    if (!mdconfigCreate.waitForFinished(10000) || mdconfigCreate.exitCode() != 0) {
        qDebug() << "BootImgCreator (FreeBSD): mdconfig failed";
        return false;
    }
    device = QString(mdconfigCreate.readAllStandardOutput()).trimmed();
    return true;
}

bool BootImgCreatorUnix::detachDiskImage(const QString& device) {
    QProcess::execute("mdconfig", QStringList() << "-d" << "-u" << device);
    return true;
}

bool BootImgCreatorUnix::mountFilesystem(const QString& device, const QString& mountPoint) {
    QProcess mountProc;
    mountProc.start("mount", QStringList() << "-t" << "msdosfs" << device << mountPoint);
    if (!mountProc.waitForFinished(10000) || mountProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (FreeBSD): mount failed:" << mountProc.readAllStandardError();
        return false;
    }
    return true;
}

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray>& files,
                                   const QString& outputPath,
				   qint64 totalSize) {
    return BootImgCreatorUnix::createBootImg(files, outputPath, totalSize);
}
