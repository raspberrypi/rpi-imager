/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootimgcreator.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray> &files, 
                                   const QString &outputPath, 
                                   qint64 totalSize)
{
    if (files.isEmpty()) {
        qDebug() << "BootImgCreator (macOS): no files to pack";
        return false;
    }

    qDebug() << "BootImgCreator (macOS): creating" << totalSize << "byte boot.img";
    
    // Ensure parent directory exists
    QFileInfo outputInfo(outputPath);
    QDir().mkpath(outputInfo.absolutePath());
    
    // Create empty disk image (without filesystem - we'll format it below)
    QFile imgFile(outputPath);
    if (!imgFile.open(QIODevice::WriteOnly)) {
        qDebug() << "BootImgCreator (macOS): failed to create" << outputPath;
        return false;
    }
    if (!imgFile.resize(totalSize)) {
        qDebug() << "BootImgCreator (macOS): failed to resize to" << totalSize;
        imgFile.close();
        return false;
    }
    imgFile.close();
    
    // Attach the disk image
    QProcess hdiutilAttach;
    hdiutilAttach.start("hdiutil", QStringList() << "attach" << "-nomount" << outputPath);
    if (!hdiutilAttach.waitForFinished(10000) || hdiutilAttach.exitCode() != 0) {
        qDebug() << "BootImgCreator (macOS): hdiutil attach failed";
        return false;
    }
    
    QString device = QString(hdiutilAttach.readAllStandardOutput()).trimmed();
    qDebug() << "BootImgCreator (macOS): attached boot.img as" << device;
    
    // Format with newfs_msdos (now on the device, not the file)
    QProcess newfsProc;
    newfsProc.start("newfs_msdos", QStringList() << "-F" << "32" << device);
    if (!newfsProc.waitForFinished(30000) || newfsProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (macOS): newfs_msdos failed:" 
                 << newfsProc.readAllStandardError();
        QProcess::execute("hdiutil", QStringList() << "detach" << device);
        return false;
    }
    
    // Mount the filesystem
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "BootImgCreator (macOS): failed to create temp directory";
        QProcess::execute("hdiutil", QStringList() << "detach" << device);
        return false;
    }
    
    QString mountPoint = tempDir.path() + "/mnt";
    QDir().mkpath(mountPoint);
    
    QProcess mountProc;
    mountProc.start("mount", QStringList() << "-t" << "msdos" << device << mountPoint);
    if (!mountProc.waitForFinished(10000) || mountProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (macOS): mount failed:" << mountProc.readAllStandardError();
        QProcess::execute("hdiutil", QStringList() << "detach" << device);
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
            qDebug() << "BootImgCreator (macOS): failed to create" << filePath;
            continue;
        }
        outFile.write(it.value());
        outFile.close();
    }
    
    // Unmount and detach
    QProcess::execute("umount", QStringList() << mountPoint);
    QProcess::execute("hdiutil", QStringList() << "detach" << device);
    
    qDebug() << "BootImgCreator (macOS): boot.img created successfully";
    return true;
}

