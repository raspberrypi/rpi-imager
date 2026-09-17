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
#include <QTextStream>
#include <QDebug>
#include <QThread>

#include <windows.h>

namespace {

// A drive letter nothing is using, or nought when every one is taken.
//
// The letter used to be Z, unconditionally. Z is a popular choice for a mapped
// network drive, and diskpart reports a failed `assign` in its output while
// still exiting zero -- so where Z was already taken the assign failed, the
// exit code said otherwise, and the boot files were written to whatever Z
// already was. Two harms from the one line: somebody's share gains a
// config.txt, and the image they should have gone into is handed back
// formatted, empty, and reported as a success.
//
// Searched from Z downwards, because the low letters are where real volumes
// live. A and B are left alone whatever the mask says: they are the floppy
// letters, and Windows still treats them differently.
//
// Takes the mask rather than reading it, so the choosing can be tested against
// a machine laid out any way at all rather than only against this one.
char freeDriveLetterFrom(DWORD mask)
{
    for (int letter = 'Z'; letter >= 'D'; --letter) {
        if (!(mask & (1u << (letter - 'A'))))
            return static_cast<char>(letter);
    }
    return 0;
}

bool driveLetterPresentIn(DWORD mask, char letter)
{
    if (letter < 'A' || letter > 'Z')
        return false;
    return (mask & (1u << (letter - 'A'))) != 0;
}

char freeDriveLetter() { return freeDriveLetterFrom(GetLogicalDrives()); }

// Whether a volume answers to this letter now. Asked after the assign, so a
// letter that has not appeared means the assign failed however diskpart chose
// to exit.
bool driveLetterPresent(char letter)
{
    return driveLetterPresentIn(GetLogicalDrives(), letter);
}

} // namespace

#ifdef BOOTIMG_ENABLE_TEST_API
// Which letter the image is mounted on, and whether it turned up, are the two
// decisions here that do not need diskpart -- and the two that put the boot
// files somewhere other than the image when they were wrong.
namespace BootImgCreatorTesting {

char chooseFreeDriveLetter(unsigned long mask)
{
    return freeDriveLetterFrom(static_cast<DWORD>(mask));
}

bool letterPresentIn(unsigned long mask, char letter)
{
    return driveLetterPresentIn(static_cast<DWORD>(mask), letter);
}

} // namespace BootImgCreatorTesting
#endif

bool BootImgCreator::createBootImg(const QMap<QString, QByteArray> &files,
                                   const QString &outputPath,
                                   qint64 totalSize)
{
    if (files.isEmpty()) {
        qDebug() << "BootImgCreator (Windows): no files to pack";
        return false;
    }

    qDebug() << "BootImgCreator (Windows): creating" << totalSize << "byte boot.img";
    
    // Ensure parent directory exists
    QFileInfo outputInfo(outputPath);
    QDir().mkpath(outputInfo.absolutePath());
    
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qDebug() << "BootImgCreator (Windows): failed to create temp directory";
        return false;
    }
    
    // Create empty file
    QFile imgFile(outputPath);
    if (!imgFile.open(QIODevice::WriteOnly)) {
        qDebug() << "BootImgCreator (Windows): failed to create" << outputPath;
        return false;
    }
    imgFile.resize(totalSize);
    imgFile.close();
    
    // Create a diskpart script to format the image
    QString diskpartScript = tempDir.path() + "/format_boot.txt";
    QFile scriptFile(diskpartScript);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "BootImgCreator (Windows): failed to create diskpart script";
        return false;
    }
    
    const char mountLetter = freeDriveLetter();
    if (mountLetter == 0) {
        qDebug() << "BootImgCreator (Windows): every drive letter is in use, so the"
                    " image cannot be mounted to fill it";
        return false;
    }

    QTextStream script(&scriptFile);
    QString winPath = QDir::toNativeSeparators(outputPath);
    script << "select vdisk file=\"" << winPath << "\"\r\n";
    script << "attach vdisk\r\n";
    script << "create partition primary\r\n";
    script << "format fs=fat32 quick\r\n";
    script << "assign letter=" << mountLetter << "\r\n";
    scriptFile.close();
    
    // Run diskpart
    QProcess diskpartProc;
    diskpartProc.start("diskpart", QStringList() << "/s" << diskpartScript);
    if (!diskpartProc.waitForFinished(60000) || diskpartProc.exitCode() != 0) {
        qDebug() << "BootImgCreator (Windows): diskpart failed:" 
                 << diskpartProc.readAllStandardError();
        return false;
    }
    
    // Wait for the drive to be ready
    QThread::msleep(2000);

    // Checked, not assumed. diskpart exits zero having reported a failed
    // assign in its output, and writing to a letter this image did not get is
    // writing into whatever else holds it.
    if (!driveLetterPresent(mountLetter)) {
        qDebug() << "BootImgCreator (Windows): drive letter" << mountLetter
                 << "did not appear after attaching the image";
        return false;
    }

    const QString mountRoot = QString(QLatin1Char(mountLetter)) + QStringLiteral(":\\");

    // Copy files in
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        QString destPath = mountRoot + QString(it.key()).replace("/", "\\");
        
        // Create parent directory if needed
        QFileInfo fileInfo(destPath);
        QString parentDir = fileInfo.absolutePath();
        if (!QDir(parentDir).exists()) {
            QDir().mkpath(parentDir);
        }
        
        // Write file
        QFile outFile(destPath);
        if (!outFile.open(QIODevice::WriteOnly)) {
            qDebug() << "BootImgCreator (Windows): failed to create" << destPath;
            continue;
        }
        outFile.write(it.value());
        outFile.close();
    }
    
    // Detach the virtual disk
    QString detachScript = tempDir.path() + "/detach.txt";
    QFile detachFile(detachScript);
    if (detachFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ds(&detachFile);
        ds << "select vdisk file=\"" << winPath << "\"\r\n";
        ds << "detach vdisk\r\n";
        detachFile.close();
        
        QProcess detachProc;
        detachProc.start("diskpart", QStringList() << "/s" << detachScript);
        detachProc.waitForFinished(10000);
    }
    
    qDebug() << "BootImgCreator (Windows): boot.img created successfully";
    return true;
}

