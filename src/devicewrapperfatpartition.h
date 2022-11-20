#ifndef DEVICEWRAPPERFATPARTITION_H
#define DEVICEWRAPPERFATPARTITION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperpartition.h"
#include <QObject>
#include <QDate>
#include <QTime>

enum fatType { FAT12, FAT16, FAT32, EXFAT };
struct dir_entry;

class DeviceWrapperFatPartition : public DeviceWrapperPartition
{
    Q_OBJECT
public:
    DeviceWrapperFatPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent = nullptr);

    QByteArray readFile(const QString &filename);
    void writeFile(const QString &filename, const QByteArray &contents);
    bool fileExists(const QString &filename);

protected:
    enum fatType _type;
    uint32_t _firstFatStartOffset, _fatSize, _bytesPerCluster, _clusterOffset;
    uint32_t _fat16_rootDirSectors, _fat16_firstRootDirSector;
    uint32_t _fat32_firstRootDirCluster, _fat32_currentRootDirCluster;
    uint16_t _bytesPerSector, _fat32_fsinfoSector;
    QList<uint32_t> _fatStartOffset;
    QList<uint32_t> _currentDirClusters;

    QList<uint32_t> getClusterChain(uint32_t firstCluster);
    void setFAT16(uint16_t cluster, uint16_t value);
    void setFAT32(uint32_t cluster, uint32_t value);
    void setFAT(uint32_t cluster, uint32_t value);
    uint32_t getFAT(uint32_t cluster);
    void seekCluster(uint32_t cluster);
    uint32_t allocateCluster();
    uint32_t allocateCluster(uint32_t previousCluster);
    bool getDirEntry(const QString &longFilename, struct dir_entry *entry, bool createIfNotExist = false);
    bool dirNameExists(const QByteArray dirname);
    void updateDirEntry(struct dir_entry *dirEntry);
    void writeDirEntryAtCurrentPos(struct dir_entry *dirEntry);
    void openDir();
    bool readDir(struct dir_entry *result);
    void updateFSinfo(int deltaClusters, uint32_t nextFreeClusterHint);
    uint16_t QTimeToFATtime(const QTime &time);
    uint16_t QDateToFATdate(const QDate &date);
};

#endif // DEVICEWRAPPERFATPARTITION_H
