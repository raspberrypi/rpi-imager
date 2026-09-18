/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A whole-disk image: an MBR with one FAT32 partition in it.
 *
 * What a card looks like to the code that writes one, and what a fixture has
 * to produce before any host can mount it. The bytes are the same whether a
 * loop device or a virtual disk carries them afterwards, so they are built in
 * one place rather than once per platform.
 */
#ifndef RPI_TEST_FAT_DISK_IMAGE_H
#define RPI_TEST_FAT_DISK_IMAGE_H

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QUuid>

#include "platform_tools.h"

namespace rpi_test {

// Where the partition starts, in sectors. 2048 is what every partitioning
// tool has used since 4K-sector drives arrived, and what a card formatted by
// Imager itself uses.
inline constexpr qint64 kFatPartitionStartSector = 2048;
inline constexpr qint64 kDiskSectorSize = 512;

// An MBR with one bootable FAT32 LBA partition, followed by the filesystem.
//
// Returns an empty array where no FAT formatter is available, which the
// caller should treat as a reason to skip. `scratchDir` must exist; the
// filesystem is built in a file there and read back, because the formatters
// work on a file rather than on a buffer.
inline QByteArray buildFat32DiskImage(const QString &scratchDir, int megabytes,
                                      const QString &label = QStringLiteral("bootfs"))
{
    const qint64 partOffset = kFatPartitionStartSector * kDiskSectorSize;
    const qint64 fatBytes = static_cast<qint64>(megabytes) * 1024 * 1024;

    const QString fatPath = QDir(scratchDir).filePath(
        QStringLiteral("fat-%1.img")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    {
        QFile f(fatPath);
        if (!f.open(QIODevice::WriteOnly) || !f.resize(fatBytes))
            return {};
    }
    if (!makeFatFilesystem(fatPath, 32, label)) {
        QFile::remove(fatPath);
        return {};
    }

    QFile fat(fatPath);
    if (!fat.open(QIODevice::ReadOnly)) {
        QFile::remove(fatPath);
        return {};
    }
    const QByteArray filesystem = fat.readAll();
    fat.close();
    QFile::remove(fatPath);
    if (filesystem.size() != fatBytes)
        return {};

    QByteArray disk(partOffset, '\0');
    auto put32 = [&disk](int off, quint32 v) {
        disk[off]     = char(v & 0xFF);
        disk[off + 1] = char((v >> 8) & 0xFF);
        disk[off + 2] = char((v >> 16) & 0xFF);
        disk[off + 3] = char((v >> 24) & 0xFF);
    };
    disk[0x1BE] = char(0x80);            // bootable
    disk[0x1C2] = char(0x0C);            // FAT32 LBA
    put32(0x1C6, quint32(kFatPartitionStartSector));
    put32(0x1CA, quint32(fatBytes / kDiskSectorSize));
    disk[0x1FE] = char(0x55);
    disk[0x1FF] = char(0xAA);
    disk.append(filesystem);
    return disk;
}

} // namespace rpi_test

#endif // RPI_TEST_FAT_DISK_IMAGE_H
