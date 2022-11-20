#include "devicewrapperfatpartition.h"
#include "devicewrapperstructs.h"
#include "qdebug.h"

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

DeviceWrapperFatPartition::DeviceWrapperFatPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent)
    : DeviceWrapperPartition(dw, partStart, partLen, parent)
{
    union fat_bpb bpb;

    read((char *) &bpb, sizeof(bpb));

    if (bpb.fat16.Signature[0] != 0x55 || bpb.fat16.Signature[1] != 0xAA)
        throw std::runtime_error("Partition does not have a FAT file system");

    /* Determine FAT type as per p. 14 https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf */
    _bytesPerSector = bpb.fat16.BPB_BytsPerSec;
    uint32_t totalSectors, dataSectors, countOfClusters;
    _fat16_rootDirSectors = ((bpb.fat16.BPB_RootEntCnt * 32) + (_bytesPerSector - 1)) / _bytesPerSector;

    if (bpb.fat16.BPB_FATSz16)
        _fatSize = bpb.fat16.BPB_FATSz16;
    else
        _fatSize = bpb.fat32.BPB_FATSz32;

    if (bpb.fat16.BPB_TotSec16)
        totalSectors = bpb.fat16.BPB_TotSec16;
    else
        totalSectors = bpb.fat32.BPB_TotSec32;

    dataSectors = totalSectors - (bpb.fat16.BPB_RsvdSecCnt + (bpb.fat16.BPB_NumFATs * _fatSize) + _fat16_rootDirSectors);
    countOfClusters = dataSectors / bpb.fat16.BPB_SecPerClus;
    _bytesPerCluster = bpb.fat16.BPB_SecPerClus * _bytesPerSector;
    _fat16_firstRootDirSector = bpb.fat16.BPB_RsvdSecCnt + (bpb.fat16.BPB_NumFATs * bpb.fat16.BPB_FATSz16);
    _fat32_firstRootDirCluster = bpb.fat32.BPB_RootClus;

    if (!_bytesPerSector)
        _type = EXFAT;
    else if (countOfClusters < 4085)
        _type = FAT12;
    else if (countOfClusters < 65525)
        _type = FAT16;
    else
        _type = FAT32;

    if (_type == FAT12)
        throw std::runtime_error("FAT12 file system not supported");
    if (_type == EXFAT)
        throw std::runtime_error("exFAT file system not supported");
    if (_bytesPerSector % 4)
        throw std::runtime_error("FAT file system: invalid bytes per sector");

    _firstFatStartOffset = bpb.fat16.BPB_RsvdSecCnt * _bytesPerSector;
    for (int i = 0; i < bpb.fat16.BPB_NumFATs; i++)
    {
        _fatStartOffset.append(_firstFatStartOffset + (i * _fatSize * _bytesPerSector));
    }

    if (_type == FAT16)
    {
        _fat32_fsinfoSector = 0;
        _clusterOffset = (_fat16_firstRootDirSector+_fat16_rootDirSectors) * _bytesPerSector;
    }
    else
    {
        _fat32_fsinfoSector = bpb.fat32.BPB_FSInfo;
        _clusterOffset = _firstFatStartOffset + (bpb.fat16.BPB_NumFATs * _fatSize * _bytesPerSector);
    }
}

uint32_t DeviceWrapperFatPartition::allocateCluster()
{
    char sector[_bytesPerSector];
    int bytesPerEntry = (_type == FAT16 ? 2 : 4);
    int entriesPerSector = _bytesPerSector/bytesPerEntry;
    uint32_t cluster;
    uint16_t *f16 = (uint16_t *) &sector;
    uint32_t *f32 = (uint32_t *) &sector;

    seek(_firstFatStartOffset);

    for (int i = 0; i < _fatSize; i++)
    {
        read(sector, sizeof(sector));

        for (int j=0; j < entriesPerSector; j++)
        {
            if (_type == FAT16)
            {
                if (f16[j] == 0)
                {
                    /* Found available FAT16 cluster, mark it used/EOF */
                    cluster = j+i*entriesPerSector;
                    setFAT16(cluster, 0xFFFF);
                    return cluster;
                }
            }
            else
            {
                if ( (f32[j] & 0x0FFFFFFF) == 0)
                {
                    /* Found available FAT32 cluster, mark it used/EOF */
                    cluster = j+i*entriesPerSector;
                    setFAT32(cluster, 0xFFFFFFF);
                    updateFSinfo(-1, cluster);
                    return cluster;
                }
            }
        }
    }

    throw std::runtime_error("Out of disk space on FAT partition");
}

uint32_t DeviceWrapperFatPartition::allocateCluster(uint32_t previousCluster)
{
    uint32_t newCluster = allocateCluster();

    if (previousCluster)
    {
        if (_type == FAT16)
            setFAT16(previousCluster, newCluster);
        else
            setFAT32(previousCluster, newCluster);
    }

    return newCluster;
}

void DeviceWrapperFatPartition::setFAT16(uint16_t cluster, uint16_t value)
{
    /* Modify all FATs (usually 2) */
    for (auto fatStart : qAsConst(_fatStartOffset))
    {
        seek(fatStart + cluster * 2);
        write((char *) &value, 2);
    }
}

void DeviceWrapperFatPartition::setFAT32(uint32_t cluster, uint32_t value)
{
    uint32_t prev_value, reserved_bits;

    /* Modify all FATs (usually 2) */
    for (auto fatStart : qAsConst(_fatStartOffset))
    {
        /* Spec (p. 16) mentions we must preserve high 4 bits of FAT32 FAT entry when modifiying */
        seek(fatStart + cluster * 4);
        read( (char *) &prev_value, 4);
        reserved_bits = prev_value & 0xF0000000;
        value |= reserved_bits;

        seek(fatStart + cluster * 4);
        write((char *) &value, 4);
    }
}

void DeviceWrapperFatPartition::setFAT(uint32_t cluster, uint32_t value)
{
    if (_type == FAT16)
        setFAT16(cluster, value);
    else
        setFAT32(cluster, value);
}

uint32_t DeviceWrapperFatPartition::getFAT(uint32_t cluster)
{
    if (_type == FAT16)
    {
        uint16_t result;
        seek(_firstFatStartOffset + cluster * 2);
        read((char *) &result, 2);
        return result;
    }
    else
    {
        uint32_t result;
        seek(_firstFatStartOffset + cluster * 4);
        read((char *) &result, 4);
        return result & 0x0FFFFFFF;
    }
}

QList<uint32_t> DeviceWrapperFatPartition::getClusterChain(uint32_t firstCluster)
{
    QList<uint32_t> list;
    uint32_t cluster = firstCluster;

    while (true)
    {
        if ( (_type == FAT16 && cluster > 0xFFF7)
             || (_type == FAT32 && cluster > 0xFFFFFF7))
        {
            /* Reached EOF */
            break;
        }

        if (list.contains(cluster))
            throw std::runtime_error("Corrupt file system. Circular references in FAT table");

        list.append(cluster);
        cluster = getFAT(cluster);
    }

    return list;
}

void DeviceWrapperFatPartition::seekCluster(uint32_t cluster)
{
    seek(_clusterOffset + (cluster-2)*_bytesPerCluster);
}

bool DeviceWrapperFatPartition::fileExists(const QString &filename)
{
    struct dir_entry entry;
    return getDirEntry(filename, &entry);
}

QByteArray DeviceWrapperFatPartition::readFile(const QString &filename)
{
    struct dir_entry entry;

    if (!getDirEntry(filename, &entry))
        return QByteArray(); /* File not found */

    uint32_t firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);
    QList<uint32_t> clusterList = getClusterChain(firstCluster);
    uint32_t len = entry.DIR_FileSize, pos = 0;
    QByteArray result(len, 0);

    for (uint32_t cluster : qAsConst(clusterList))
    {
        seekCluster(cluster);
        read(result.data()+pos, qMin(_bytesPerCluster, len-pos));

        pos += _bytesPerCluster;
        if (pos >= len)
            break;
    }

    return result;
}

void DeviceWrapperFatPartition::writeFile(const QString &filename, const QByteArray &contents)
{
    QList<uint32_t> clusterList;
    uint32_t pos = 0;
    uint32_t firstCluster;
    int clustersNeeded = (contents.length() + _bytesPerCluster - 1) / _bytesPerCluster;
    struct dir_entry entry;

    getDirEntry(filename, &entry, true);
    firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);

    if (firstCluster)
        clusterList = getClusterChain(firstCluster);

    if (clusterList.length() < clustersNeeded)
    {
        /* We need to allocate more clusters */
        uint32_t lastCluster = 0;
        int extraClustersNeeded = clustersNeeded - clusterList.length();

        if (!clusterList.isEmpty())
            lastCluster = clusterList.last();

        for (int i = 0; i < extraClustersNeeded; i++)
        {
            lastCluster = allocateCluster(lastCluster);
            clusterList.append(lastCluster);
        }
    }
    else if (clusterList.length() > clustersNeeded)
    {
        /* We need to remove excess clusters */
        int clustersToRemove = clusterList.length() - clustersNeeded;
        uint32_t clusterToRemove = 0;
        QByteArray zeroes(_bytesPerCluster, 0);

        for (int i=0; i < clustersToRemove; i++)
        {
            clusterToRemove = clusterList.takeLast();

            /* Zero out previous data in excess clusters,
               just in case someone wants to take a disk image later */
            seekCluster(clusterToRemove);
            write(zeroes.data(), zeroes.length());

            /* Mark cluster available again in FAT */
            setFAT(clusterToRemove, 0);
        }
        updateFSinfo(clustersToRemove, clusterToRemove);

        if (!clusterList.isEmpty())
        {
            if (_type == FAT16)
                setFAT16(clusterList.last(), 0xFFFF);
            else
                setFAT32(clusterList.last(), 0xFFFFFFF);
        }
    }

    //qDebug() << "First cluster:" << firstCluster << "Clusters:" << clusterList;

    /* Write file data */
    for (uint32_t cluster : qAsConst(clusterList))
    {
        seekCluster(cluster);
        write(contents.data()+pos, qMin(_bytesPerCluster, contents.length()-pos));

        pos += _bytesPerCluster;
        if (pos >= contents.length())
            break;
    }

    if (clustersNeeded)
    {
        /* Zero out last cluster tip */
        uint32_t extraBytesAtEndOfCluster = _bytesPerCluster - (contents.length() % _bytesPerCluster);
        if (extraBytesAtEndOfCluster)
        {
            QByteArray zeroes(extraBytesAtEndOfCluster, 0);
            write(zeroes.data(), zeroes.length());
        }
    }

    /* Update directory entry */
    if (clusterList.isEmpty())
        firstCluster = (_type == FAT16 ? 0xFFFF : 0xFFFFFFF);
    else
        firstCluster = clusterList.first();

    entry.DIR_FstClusLO = (firstCluster & 0xFFFF);
    entry.DIR_FstClusHI = (firstCluster >> 16);
    entry.DIR_WrtDate = QDateToFATdate( QDate::currentDate() );
    entry.DIR_WrtTime = QTimeToFATtime( QTime::currentTime() );
    entry.DIR_LstAccDate = entry.DIR_WrtDate;
    entry.DIR_FileSize = contents.length();
    updateDirEntry(&entry);
}

inline QByteArray _dirEntryToShortName(struct dir_entry *entry)
{
    QByteArray base = QByteArray((char *) entry->DIR_Name, 8).trimmed().toLower();
    QByteArray ext = QByteArray((char *) entry->DIR_Name+8, 3).trimmed().toLower();

    if (ext.isEmpty())
        return base;
    else
        return base+"."+ext;
}

bool DeviceWrapperFatPartition::getDirEntry(const QString &longFilename, struct dir_entry *entry, bool createIfNotExist)
{
    QString filenameRead, longFilenameLower = longFilename.toLower();

    if (longFilename.isEmpty())
        throw std::runtime_error("Filename cannot not be empty");

    openDir();
    while (readDir(entry))
    {
        if (entry->DIR_Attr & ATTR_LONG_NAME)
        {
            struct longfn_entry *l = (struct longfn_entry *) entry;
            /* A part can have 13 UTF-16 characters */
            QString lnamePart(13, QChar::Null);
            char *lnamePartStr = (char *) lnamePart.data();
             /* Using memcpy() because it has no problems accessing unaligned struct members */
            memcpy(lnamePartStr, l->LDIR_Name1, 10);
            memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
            memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
            filenameRead = lnamePart + filenameRead;
        }
        else
        {
            if (entry->DIR_Name[0] != 0xE5)
            {
                filenameRead.truncate(filenameRead.indexOf(QChar::Null));

                //qDebug() << "Long filename:" << filenameRead << "DIR_Name:" << QByteArray((char *) entry->DIR_Name, sizeof(entry->DIR_Name)) << "Short:" << _dirEntryToShortName(entry);

                if (filenameRead.toLower() == longFilenameLower || (filenameRead.isEmpty() && _dirEntryToShortName(entry) == longFilenameLower))
                {
                    return true;
                }
            }

            filenameRead.clear();
        }
    }

    if (createIfNotExist)
    {
        QByteArray shortFilename;
        uint8_t shortFileNameChecksum = 0;
        struct longfn_entry longEntry;

        if (longFilename.count(".") == 1)
        {
            QList<QByteArray> fnParts = longFilename.toLatin1().toUpper().split('.');
            shortFilename = fnParts[0].leftJustified(8, ' ', true)+fnParts[1].leftJustified(3, ' ', true);
        }
        else
        {
            shortFilename = longFilename.toLatin1().leftJustified(11, ' ', true);
        }

        /* Verify short file name has not been taken yet, and if not try inserting numbers into the name */
        if (dirNameExists(shortFilename))
        {
            for (int i=0; i<100; i++)
            {
                shortFilename = shortFilename.left( (i < 10 ? 7 : 6) )+QByteArray::number(i)+shortFilename.right(3);

                if (!dirNameExists(shortFilename))
                {
                    break;
                }
                else if (i == 99)
                {
                    throw std::runtime_error("Error finding available short filename");
                }
            }
        }

        for(int i = 0; i < shortFilename.length(); i++)
        {
            shortFileNameChecksum = ((shortFileNameChecksum & 1) ? 0x80 : 0) + (shortFileNameChecksum >> 1) + shortFilename[i];
        }

        QString longFilenameWithNull = longFilename + QChar::Null;
        char *longFilenameStr = (char *) longFilenameWithNull.data();
        int lfnFragments = (longFilenameWithNull.length()+12)/13;
        int lenBytes = longFilenameWithNull.length()*2;

        /* long file name directory entries are added in reverse order before the 8.3 entry */
        for (int i = lfnFragments; i > 0; i--)
        {
            memset(&longEntry, 0xff, sizeof(longEntry));
            longEntry.LDIR_Attr = ATTR_LONG_NAME;
            longEntry.LDIR_Chksum = shortFileNameChecksum;
            longEntry.LDIR_Ord = (i == lfnFragments) ? lfnFragments | LAST_LONG_ENTRY : lfnFragments;
            longEntry.LDIR_FstClusLO = 0;
            longEntry.LDIR_Type = 0;

            size_t start = (i-1) * 26;
            memcpy(longEntry.LDIR_Name1, longFilenameStr+start, qMin(lenBytes-start, sizeof(longEntry.LDIR_Name1)));
            start += sizeof(longEntry.LDIR_Name1);
            if (start < lenBytes)
            {
                memcpy(longEntry.LDIR_Name2, longFilenameStr+start, qMin(lenBytes-start, sizeof(longEntry.LDIR_Name2)));
                start += sizeof(longEntry.LDIR_Name2);
                if (start < lenBytes)
                {
                    memcpy(longEntry.LDIR_Name3, longFilenameStr+start, qMin(lenBytes-start, sizeof(longEntry.LDIR_Name3)));
                }
            }

            writeDirEntryAtCurrentPos((struct dir_entry *) &longEntry);
        }

        memset(entry, 0, sizeof(*entry));
        memcpy(entry->DIR_Name, shortFilename.data(), sizeof(entry->DIR_Name));
        entry->DIR_Attr = ATTR_ARCHIVE;
        entry->DIR_CrtDate = QDateToFATdate( QDate::currentDate() );
        entry->DIR_CrtTime = QTimeToFATtime( QTime::currentTime() );

        writeDirEntryAtCurrentPos(entry);

        /* Add an end-of-directory marker after our newly appended file */
        struct dir_entry endOfDir = {0};
        writeDirEntryAtCurrentPos(&endOfDir);
    }

    return false;
}

bool DeviceWrapperFatPartition::dirNameExists(const QByteArray dirname)
{
    struct dir_entry entry;

    openDir();
    while (readDir(&entry))
    {
        if (!(entry.DIR_Attr & ATTR_LONG_NAME)
                && dirname == QByteArray((char *) entry.DIR_Name, sizeof(entry.DIR_Name)))
        {
            return true;
        }
    }

    return false;
}

void DeviceWrapperFatPartition::updateDirEntry(struct dir_entry *dirEntry)
{
    struct dir_entry iterEntry;

    openDir();
    quint64 oldOffset = _offset;

    while (readDir(&iterEntry))
    {
        /* Look for existing entry with same short filename */
        if (!(iterEntry.DIR_Attr & ATTR_LONG_NAME)
                && memcmp(dirEntry->DIR_Name, iterEntry.DIR_Name, sizeof(iterEntry.DIR_Name)) == 0)
        {
            /* seek() back and write out new entry */
            _offset = oldOffset;
            write((char *) dirEntry, sizeof(*dirEntry));
            return;
        }

        oldOffset = _offset;
    }

    throw std::runtime_error("Error locating existing directory entry");
}

void DeviceWrapperFatPartition::writeDirEntryAtCurrentPos(struct dir_entry *dirEntry)
{
    //qDebug() << "Write new entry" << QByteArray((char *) dirEntry->DIR_Name, 11);
    write((char *) dirEntry, sizeof(*dirEntry));

    if (_type == FAT32)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, allocate/seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (nextCluster > 0xFFFFFF7)
            {
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
            }

            if (_currentDirClusters.contains(nextCluster))
                throw std::runtime_error("Circular cluster references in FAT32 directory detected");
            _currentDirClusters.append(nextCluster);

            _fat32_currentRootDirCluster = nextCluster;
            seekCluster(_fat32_currentRootDirCluster);

            /* Zero out entire new cluster, as fsck.fat does not stop reading entries at end-of-directory marker */
            QByteArray zeroes(_bytesPerCluster, 0);
            write(zeroes.data(), zeroes.length() );
            seekCluster(_fat32_currentRootDirCluster);
        }
    }
    else if (pos() > (_fat16_firstRootDirSector+_fat16_rootDirSectors)*_bytesPerSector)
    {
        throw std::runtime_error("FAT16: ran out of root directory entry space");
    }
}

void DeviceWrapperFatPartition::openDir()
{
    /* Seek to start of root directory */
    if (_type == FAT16)
    {
        seek(_fat16_firstRootDirSector * _bytesPerSector);
    }
    else
    {
        _fat32_currentRootDirCluster = _fat32_firstRootDirCluster;
        seekCluster(_fat32_currentRootDirCluster);
        /* Keep track of directory clusters we seeked to, to be able
           to detect circular references */
        _currentDirClusters.clear();
        _currentDirClusters.append(_fat32_currentRootDirCluster);
    }
}

bool DeviceWrapperFatPartition::readDir(struct dir_entry *result)
{
    quint64 oldOffset = _offset;
    read((char *) result, sizeof(*result));

    if (result->DIR_Name[0] == 0)
    {
        /* seek() back to start of the entry marking end of directory */
        _offset = oldOffset;
        return false;
    }

    if (_type == FAT32)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (nextCluster > 0xFFFFFF7)
                throw std::runtime_error("Reached end of FAT32 root directory, but no end-of-directory marker found");

            if (_currentDirClusters.contains(nextCluster))
                throw std::runtime_error("Circular cluster references in FAT32 directory detected");
            _currentDirClusters.append(nextCluster);
            _fat32_currentRootDirCluster = nextCluster;
            seekCluster(_fat32_currentRootDirCluster);
        }
    }
    else if (pos() > (_fat16_firstRootDirSector+_fat16_rootDirSectors)*_bytesPerSector)
    {
        throw std::runtime_error("Reached end of FAT16 root directory section, but no end-of-directory marker found");
    }

    return true;
}

void DeviceWrapperFatPartition::updateFSinfo(int deltaClusters, uint32_t nextFreeClusterHint)
{
    struct FSInfo fsinfo;

    if (!_fat32_fsinfoSector)
        return;

    seek(_fat32_fsinfoSector * _bytesPerSector);
    read((char *) &fsinfo, sizeof(fsinfo));

    if (fsinfo.FSI_LeadSig[0] != 0x52 || fsinfo.FSI_LeadSig[1] != 0x52
            || fsinfo.FSI_LeadSig[2] != 0x61 || fsinfo.FSI_LeadSig[3] != 0x41
            || fsinfo.FSI_StrucSig[0] != 0x72 || fsinfo.FSI_StrucSig[1] != 0x72
            || fsinfo.FSI_StrucSig[2] != 0x41 || fsinfo.FSI_StrucSig[3] != 0x61
            || fsinfo.FSI_TrailSig[0] != 0x00 || fsinfo.FSI_TrailSig[1] != 0x00
            || fsinfo.FSI_TrailSig[2] != 0x55 || fsinfo.FSI_TrailSig[3] != 0xAA)
    {
        throw std::runtime_error("FAT32 FSinfo structure corrupt. Signature does not match.");
    }

    if (deltaClusters != 0 && fsinfo.FSI_Free_Count != 0xFFFFFFFF)
    {
        fsinfo.FSI_Free_Count += deltaClusters;
    }

    if (nextFreeClusterHint)
    {
        fsinfo.FSI_Nxt_Free = nextFreeClusterHint;
    }

    seek(_fat32_fsinfoSector * _bytesPerSector);
    write((char *) &fsinfo, sizeof(fsinfo));
}

uint16_t DeviceWrapperFatPartition::QTimeToFATtime(const QTime &time)
{
    return (time.hour() << 11) | (time.minute() << 5) | (time.second() >> 1) ;
}

uint16_t DeviceWrapperFatPartition::QDateToFATdate(const QDate &date)
{
    return ((date.year() - 1980) << 9) | (date.month() << 5) | date.day();
}
