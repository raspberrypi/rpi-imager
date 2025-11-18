/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootimgcreator.h"
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>
#include <QSet>

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray> &files, 
                                   const QString &outputPath, 
                                   qint64 totalSize)
{
    if (files.isEmpty()) {
        qDebug() << "BootImgCreator (Linux): no files to pack";
        return false;
    }

    qDebug() << "BootImgCreator (Linux): creating" << totalSize << "byte boot.img";
    
    // Ensure parent directory exists
    QFileInfo outputInfo(outputPath);
    QDir().mkpath(outputInfo.absolutePath());
    
    // Create empty file
    QFile imgFile(outputPath);
    if (!imgFile.open(QIODevice::WriteOnly)) {
        qDebug() << "BootImgCreator (Linux): failed to create" << outputPath;
        return false;
    }
    imgFile.resize(totalSize);
    imgFile.close();
    
    // Format as FAT32
    QProcess mkfsProc;
    mkfsProc.start("mkfs.vfat", QStringList() << "-F" << "32" << outputPath);
    if (!mkfsProc.waitForFinished(30000) || mkfsProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (Linux): mkfs.vfat failed:" 
                 << mkfsProc.readAllStandardError();
        return false;
    }
    
    // Create all directories using mmd (mtools mkdir)
    QSet<QString> dirsToCreate;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        QString filePath = it.key();
        if (filePath.contains("/")) {
            // Extract directory path
            QString dirPath = filePath.left(filePath.lastIndexOf("/"));
            
            // Add all parent directories
            QStringList parts = dirPath.split("/", Qt::SkipEmptyParts);
            QString currentPath;
            for (const QString &part : parts) {
                currentPath = currentPath.isEmpty() ? part : currentPath + "/" + part;
                dirsToCreate.insert(currentPath);
            }
        }
    }
    
    for (const QString &dir : dirsToCreate) {
        QString destDir = "::" + dir;
        QProcess mmdProc;
        mmdProc.start("mmd", QStringList() << "-i" << outputPath << destDir);
        mmdProc.waitForFinished(5000);
        
        // Note: mmd may fail if directory already exists, which is fine
        if (mmdProc.exitCode() != 0) {
            QString error = mmdProc.readAllStandardError();
            if (!error.contains("exists")) {
                qDebug() << "BootImgCreator (Linux): mmd warning for" << dir << ":" << error;
            }
        }
    }
    
    // Copy all files using mcopy
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "BootImgCreator (Linux): failed to create temp directory";
        return false;
    }
    
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        // Write file to temp location
        QString tempFile = tempDir.path() + "/tempfile";
        QFile outFile(tempFile);
        if (!outFile.open(QIODevice::WriteOnly)) {
            qDebug() << "BootImgCreator (Linux): failed to create temp file";
            continue;
        }
        outFile.write(it.value());
        outFile.close();
        
        // Copy to boot.img using mcopy
        QString destPath = "::" + it.key();
        QProcess mcopyProc;
        mcopyProc.start("mcopy", QStringList() << "-i" << outputPath << tempFile << destPath);
        if (!mcopyProc.waitForFinished(10000) || mcopyProc.exitCode() != 0) {
            qDebug() << "BootImgCreator (Linux): mcopy failed for" << it.key() 
                     << ":" << mcopyProc.readAllStandardError();
        }
    }
    
    qDebug() << "BootImgCreator (Linux): boot.img created successfully";
    return true;
}

