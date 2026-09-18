#ifndef DEVICEWRAPPERFATPARTITION_H
#define DEVICEWRAPPERFATPARTITION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperpartition.h"
#include <QObject>
#include <QDate>
#include <QSet>
#include <QTime>

#include <stdexcept>

enum fatType { FAT12, FAT16, FAT32, EXFAT };
struct dir_entry;

// A directory whose cluster chain turns back on itself.
//
// Distinct from the other corruption the driver refuses, because it is the
// only one a search may give up on and carry on from: the answer is that the
// file is not findable. Anything else keeps throwing plain runtime_error.
class DirectoryLoopError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class DeviceWrapperFatPartition : public DeviceWrapperPartition
{
    Q_OBJECT
public:
    DeviceWrapperFatPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent = nullptr);

    QByteArray readFile(const QString &filename);
    void writeFile(const QString &filename, const QByteArray &contents);
    bool fileExists(const QString &filename);
    /* The length recorded in the directory entry, without reading the file.
       -1 when there is no such file. Lets a caller check a large file's size
       without paying for a block-by-block read-back of its contents. */
    qint64 fileSize(const QString &filename);
    bool deleteFile(const QString &filename);
    QStringList listAllFiles(); // List all files recursively
    QStringList listAllFilesRecursive(); // List all files including subdirectories

protected:
    enum fatType _type;
    uint32_t _firstFatStartOffset, _fatSize, _bytesPerCluster, _clusterOffset;
    uint32_t _fat16_rootDirSectors, _fat16_firstRootDirSector;
    uint32_t _fat32_firstRootDirCluster, _fat32_currentRootDirCluster;
    uint16_t _bytesPerSector, _fat32_fsinfoSector;
    QList<uint32_t> _fatStartOffset;
    QList<uint32_t> _currentDirClusters;
    // Whether the walk in progress is over the FAT12/16 root directory,
    // which is a fixed region of sectors rather than a cluster chain. Every
    // other directory, on every FAT type, is a chain, so this and not the
    // FAT type decides whether to follow one.
    bool _inFat16RootDir = false;

    QList<uint32_t> getClusterChain(uint32_t firstCluster);
    // Whether a FAT entry marks the end of a chain. The sentinel is only as
    // wide as the table, so the FAT32 value would test true for every FAT16
    // cluster.
    bool isEndOfChain(uint32_t cluster) const;
    void setFAT16(uint16_t cluster, uint16_t value);
    void setFAT32(uint32_t cluster, uint32_t value);
    void setFAT(uint32_t cluster, uint32_t value);
    uint32_t getFAT(uint32_t cluster);
    void seekCluster(uint32_t cluster);
    uint32_t allocateCluster();
    uint32_t allocateCluster(uint32_t previousCluster);
    // Find, or create, an entry in a directory. `dirCluster` of nought is
    // the root.
    bool getDirEntry(const QString &longFilename, struct dir_entry *entry,
                     bool createIfNotExist = false, uint32_t dirCluster = 0);
    // Make a directory inside `parentCluster` and return its first cluster.
    uint32_t createDirectory(const QString &name, uint32_t parentCluster);
    // Split a path into the directory holding it and the name within.
    //
    // `dirCluster` comes back as the cluster to look in, nought for the
    // root, and `leaf` as the last component. With `createMissing` set, any
    // directory along the way that is not there is made; without it, a
    // missing one returns false and nothing is written.
    bool resolveDirectoryPath(const QString &path, QString &leaf,
                              uint32_t &dirCluster, bool createMissing);
    // Look up a file by path, creating nothing. A looping directory is
    // reported as a file that is not there rather than by throwing.
    // `dirClusterOut` takes the directory it was found in, for a caller that
    // must write the entry back.
    bool findFileEntry(const QString &filename, struct dir_entry *entry,
                       uint32_t *dirClusterOut = nullptr);
    bool dirNameExists(const QByteArray dirname, uint32_t dirCluster = 0);
    void updateDirEntry(struct dir_entry *dirEntry, uint32_t dirCluster = 0);
    void writeDirEntryAtCurrentPos(struct dir_entry *dirEntry);
    void openDir();
    // Walk a directory other than the root. Nought means the root.
    void openDirAt(uint32_t cluster);
    bool readDir(struct dir_entry *result);
    // Helper for recursive listing.
    //
    // visitedDirClusters carries the directories already on the walk. A
    // subdirectory entry names the cluster its contents begin at, and
    // nothing on the card stops that naming one an ancestor already used:
    // the chain inside a single directory is guarded, the tree was not, so
    // a card claiming a loop was walked forever.
    void listFilesInDirectory(const QString &dirPath, uint32_t dirCluster, QStringList &fileList,
                              QSet<uint32_t> &visitedDirClusters, int depth = 0);
    void updateFSinfo(int deltaClusters, uint32_t nextFreeClusterHint);
    uint16_t QTimeToFATtime(const QTime &time);
    uint16_t QDateToFATdate(const QDate &date);
};

#endif // DEVICEWRAPPERFATPARTITION_H
