#ifndef BOOTIMGCREATOR_H
#define BOOTIMGCREATOR_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QString>
#include <QMap>
#include <QByteArray>

/**
 * @brief Platform-specific boot.img creation helper
 * 
 * This class provides platform-specific implementations for creating
 * FAT32 disk images and copying files with directory hierarchy preservation.
 * 
 * Platform-specific implementations are in:
 * - mac/bootimgcreator_macos.cpp
 * - linux/bootimgcreator_linux.cpp
 * - windows/bootimgcreator_windows.cpp
 */
class BootImgCreator
{
public:
    /**
     * @brief Create a FAT32 boot.img with all files and directory structure
     * @param files Map of file paths to contents (paths may include subdirectories)
     * @param outputPath Path where boot.img should be created
     * @param totalSize Size in bytes for the boot.img file
     * @return true if successful, false otherwise
     */
    static bool createBootImg(const QMap<QString, QByteArray> &files, 
                             const QString &outputPath, 
                             qint64 totalSize);
};

#endif // BOOTIMGCREATOR_H

