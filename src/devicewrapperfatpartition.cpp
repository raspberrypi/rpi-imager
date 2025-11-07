#include "devicewrapperfatpartition.h"
#include "devicewrapperstructs.h"
#include <QDebug>
#include <QStringList>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

// Calculate LFN checksum for a short filename (8.3 format)
// This is used to validate that LFN entries belong to the correct short name entry
// Algorithm from Microsoft FAT specification
static uint8_t lfnChecksum(const unsigned char *shortName) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        // Rotate right by 1 bit, then add the next byte
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + shortName[i];
    }
    return sum;
}

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
    for (auto fatStart : std::as_const(_fatStartOffset))
    {
        seek(fatStart + cluster * 2);
        write((char *) &value, 2);
    }
}

void DeviceWrapperFatPartition::setFAT32(uint32_t cluster, uint32_t value)
{
    uint32_t prev_value, reserved_bits;

    /* Modify all FATs (usually 2) */
    for (auto fatStart : std::as_const(_fatStartOffset))
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

bool DeviceWrapperFatPartition::deleteFile(const QString &filename)
{
    struct dir_entry entry;
    
    // Handle subdirectory paths (e.g., "overlays/file.dtbo")
    if (filename.contains("/")) {
        QStringList parts = filename.split("/", Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            qDebug() << "DeviceWrapperFatPartition::deleteFile: invalid path" << filename;
            return false;
        }
        
        // Save current directory state
        uint32_t savedRootDirCluster = _fat32_currentRootDirCluster;
        QList<uint32_t> savedDirClusters = _currentDirClusters;
        
        // Navigate to the directory
        QString dirName = parts[0];
        struct dir_entry dirEntry;
        
        if (!getDirEntry(dirName, &dirEntry)) {
            qDebug() << "DeviceWrapperFatPartition::deleteFile: directory not found:" << dirName;
            return false;
        }
        
        if (!(dirEntry.DIR_Attr & ATTR_DIRECTORY)) {
            qDebug() << "DeviceWrapperFatPartition::deleteFile:" << dirName << "is not a directory";
            return false;
        }
        
        // Switch to subdirectory
        uint32_t dirCluster = (dirEntry.DIR_FstClusHI << 16) | dirEntry.DIR_FstClusLO;
        qDebug() << "DeviceWrapperFatPartition::deleteFile: switching to directory cluster" << dirCluster;
        _fat32_currentRootDirCluster = dirCluster;
        _currentDirClusters.clear();
        _currentDirClusters.append(dirCluster);
        
        // Get the file entry in subdirectory
        QString fileNameOnly = parts[parts.size() - 1];
        qDebug() << "DeviceWrapperFatPartition::deleteFile: searching for file" << fileNameOnly << "in subdirectory";
        bool found = getDirEntry(fileNameOnly, &entry);
        qDebug() << "DeviceWrapperFatPartition::deleteFile: getDirEntry returned" << found;
        
        // Restore directory state
        _fat32_currentRootDirCluster = savedRootDirCluster;
        _currentDirClusters = savedDirClusters;
        
        if (!found) {
            qDebug() << "DeviceWrapperFatPartition::deleteFile: file not found in directory:" << filename;
            return false;
        }
        
        // Mark as deleted and update
        entry.DIR_Name[0] = 0xE5;
        
        // Switch back to subdirectory to update
        _fat32_currentRootDirCluster = dirCluster;
        _currentDirClusters.clear();
        _currentDirClusters.append(dirCluster);
        
        updateDirEntry(&entry);
        
        // Restore directory state again
        _fat32_currentRootDirCluster = savedRootDirCluster;
        _currentDirClusters = savedDirClusters;
        
        qDebug() << "DeviceWrapperFatPartition::deleteFile: deleted" << filename;
        return true;
    }
    
    // Simple case: file or directory in root directory
    qDebug() << "DeviceWrapperFatPartition::deleteFile: deleting" << filename << "from root";
    if (!getDirEntry(filename, &entry)) {
        qDebug() << "DeviceWrapperFatPartition::deleteFile: entry not found:" << filename;
        return false;
    }
    
    QByteArray originalName((char *) entry.DIR_Name, sizeof(entry.DIR_Name));
    qDebug() << "DeviceWrapperFatPartition::deleteFile: found entry with name:" << originalName.toHex(':');
    
    // Check if it's a directory
    if (entry.DIR_Attr & ATTR_DIRECTORY) {
        qDebug() << "DeviceWrapperFatPartition::deleteFile: entry is a directory";
        // For directories, we just mark them as deleted
        // The caller should ensure all files in the directory are deleted first
    }
    
    // Mark the entry as deleted by setting first byte to 0xE5
    entry.DIR_Name[0] = 0xE5;
    
    qDebug() << "DeviceWrapperFatPartition::deleteFile: marked as deleted, calling updateDirEntry";
    // Update the directory entry
    updateDirEntry(&entry);
    
    // TODO: Free the clusters used by the file in the FAT
    // For now, just marking as deleted is sufficient for our use case
    // since we're about to rewrite the entire partition anyway
    
    qDebug() << "DeviceWrapperFatPartition::deleteFile: deleted" << filename;
    return true;
}

QByteArray DeviceWrapperFatPartition::readFile(const QString &filename)
{
    struct dir_entry entry;
    
    // Handle subdirectory paths (e.g., "overlays/file.dtbo")
    if (filename.contains("/")) {
        QStringList parts = filename.split("/", Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            qDebug() << "DeviceWrapperFatPartition::readFile: invalid path" << filename;
            return QByteArray();
        }
        
        // Navigate to the directory
        QString dirName = parts[0];
        struct dir_entry dirEntry;
        
        // Find the directory entry
        if (!getDirEntry(dirName, &dirEntry)) {
            qDebug() << "DeviceWrapperFatPartition::readFile: directory not found:" << dirName;
            return QByteArray();
        }
        
        if (!(dirEntry.DIR_Attr & ATTR_DIRECTORY)) {
            qDebug() << "DeviceWrapperFatPartition::readFile:" << dirName << "is not a directory";
            return QByteArray();
        }
        
        // Get directory cluster
        uint32_t dirCluster = dirEntry.DIR_FstClusLO;
        if (_type == FAT32) {
            dirCluster |= (dirEntry.DIR_FstClusHI << 16);
        }
        
        // Save current directory state
        uint32_t savedCurrentCluster = _fat32_currentRootDirCluster;
        QList<uint32_t> savedDirClusters = _currentDirClusters;
        
        // Set up to read from subdirectory
        if (_type == FAT32) {
            _fat32_currentRootDirCluster = dirCluster;
            seekCluster(_fat32_currentRootDirCluster);
            _currentDirClusters.clear();
            _currentDirClusters.append(_fat32_currentRootDirCluster);
        } else {
            seekCluster(dirCluster);
        }
        
        // Find the file in the subdirectory
        QString fileName = parts[1];
        QString fileNameLower = fileName.toLower();
        bool found = false;
        QString longFilename;
        int entriesChecked = 0;
        
        while (true) {
            // Read the next directory entry
            quint64 oldOffset = _offset;
            read((char *) &entry, sizeof(entry));
            
            if (entry.DIR_Name[0] == 0) {
                // End of directory
                break;
            }
            
            if (entry.DIR_Name[0] == 0xE5) {
                // Deleted entry, skip
                longFilename.clear();
                // Check for cluster boundary after skipping
                if (_type == FAT32 && (pos() - _clusterOffset) % _bytesPerCluster == 0) {
                    uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);
                    if (nextCluster >= 0xFFFFFF8) break;
                    if (_currentDirClusters.contains(nextCluster)) {
                        qDebug() << "Circular cluster reference in subdirectory" << dirName;
                        break;
                    }
                    _currentDirClusters.append(nextCluster);
                    _fat32_currentRootDirCluster = nextCluster;
                    seekCluster(_fat32_currentRootDirCluster);
                }
                continue;
            }
            
            if (entry.DIR_Attr & ATTR_LONG_NAME) {
                // Process long filename entry
                struct longfn_entry *l = (struct longfn_entry *) &entry;
                char lnamePartStr[26] = {0};
                memcpy(lnamePartStr, l->LDIR_Name1, 10);
                memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
                memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
                QString lnamePart((QChar *) lnamePartStr, 13);
                longFilename = lnamePart + longFilename;
                // Check for cluster boundary after LFN entry
                if (_type == FAT32 && (pos() - _clusterOffset) % _bytesPerCluster == 0) {
                    uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);
                    if (nextCluster >= 0xFFFFFF8) break;
                    if (_currentDirClusters.contains(nextCluster)) {
                        qDebug() << "Circular cluster reference in subdirectory" << dirName;
                        break;
                    }
                    _currentDirClusters.append(nextCluster);
                    _fat32_currentRootDirCluster = nextCluster;
                    seekCluster(_fat32_currentRootDirCluster);
                }
                continue;
            }
            
            // Regular entry - check if it matches
            // Truncate long filename at null char
            if (longFilename.indexOf(QChar::Null) >= 0) {
                longFilename.truncate(longFilename.indexOf(QChar::Null));
            }
            
            // Get short filename as fallback
            QString shortName;
            for (int i = 0; i < 8 && entry.DIR_Name[i] != ' '; i++) {
                shortName += QChar(entry.DIR_Name[i]).toLower();
            }
            if (entry.DIR_Name[8] != ' ') {
                shortName += '.';
                for (int i = 8; i < 11 && entry.DIR_Name[i] != ' '; i++) {
                    shortName += QChar(entry.DIR_Name[i]).toLower();
                }
            }
            
            QString actualFilename = longFilename.isEmpty() ? shortName : longFilename.toLower();
            entriesChecked++;
            
            if (actualFilename == fileNameLower) {
                found = true;
                break;
            }
            
            longFilename.clear();
            
            // Check for cluster boundary after processing regular entry
            if (_type == FAT32 && (pos() - _clusterOffset) % _bytesPerCluster == 0) {
                uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);
                if (nextCluster >= 0xFFFFFF8) break;
                if (_currentDirClusters.contains(nextCluster)) {
                    qDebug() << "Circular cluster reference in subdirectory" << dirName;
                    break;
                }
                _currentDirClusters.append(nextCluster);
                _fat32_currentRootDirCluster = nextCluster;
                seekCluster(_fat32_currentRootDirCluster);
            }
        }
        
        if (!found) {
            qDebug() << "DeviceWrapperFatPartition::readFile: searched" << entriesChecked 
                     << "entries in" << dirName << ", file" << fileName << "not found";
        }
        
        // Restore directory state
        _fat32_currentRootDirCluster = savedCurrentCluster;
        _currentDirClusters = savedDirClusters;
        
        if (!found) {
            qDebug() << "DeviceWrapperFatPartition::readFile: file not found:" << filename;
            return QByteArray();
        }
    } else {
        // Simple filename in root directory
        if (!getDirEntry(filename, &entry)) {
            return QByteArray(); /* File not found */
        }
    }

    uint32_t len = entry.DIR_FileSize;
    
    // Handle zero-length files
    if (len == 0) {
        return QByteArray();
    }

    uint32_t firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);
    
    // If file has data but no cluster, something's wrong
    if (firstCluster == 0) {
        qDebug() << "DeviceWrapperFatPartition::readFile: file" << filename 
                 << "has size" << len << "but no cluster allocation";
        return QByteArray();
    }
    
    QList<uint32_t> clusterList = getClusterChain(firstCluster);
    uint32_t pos = 0;
    QByteArray result(len, 0);

    for (uint32_t cluster : std::as_const(clusterList))
    {
        seekCluster(cluster);
        read(result.data()+pos, qMin(_bytesPerCluster, len-pos));

        pos += _bytesPerCluster;
        if (pos >= len)
            break;
    }

    return result;
}

QStringList DeviceWrapperFatPartition::listAllFiles()
{
    QStringList fileList;
    struct dir_entry entry;
    QString longFilename;
    
    openDir();
    while (readDir(&entry))
    {
        if (entry.DIR_Attr & ATTR_LONG_NAME)
        {
            // Long filename entry
            struct longfn_entry *l = (struct longfn_entry *) &entry;
            char lnamePartStr[26] = {0};
            memcpy(lnamePartStr, l->LDIR_Name1, 10);
            memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
            memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
            QString lnamePart((QChar *) lnamePartStr, 13);
            longFilename = lnamePart + longFilename;
        }
        else
        {
            // Regular directory entry
            if (entry.DIR_Name[0] != 0xE5) // Not deleted
            {
                // Truncate long filename at null char
                if (longFilename.indexOf(QChar::Null) >= 0)
                    longFilename.truncate(longFilename.indexOf(QChar::Null));
                
                // Get short filename as fallback
                QString shortName;
                for (int i = 0; i < 11 && entry.DIR_Name[i] != ' '; i++) {
                    if (i == 8 && entry.DIR_Name[i] != ' ') {
                        shortName += '.';
                    }
                    shortName += QChar(entry.DIR_Name[i]);
                }
                shortName = shortName.trimmed();
                
                QString filename = longFilename.isEmpty() ? shortName : longFilename;
                
                // Skip volume labels and current/parent directory markers
                if (!(entry.DIR_Attr & ATTR_VOLUME_ID) && 
                    filename != "." && filename != "..")
                {
                    if (entry.DIR_Attr & ATTR_DIRECTORY)
                    {
                        // Directory - recursively list its contents
                        // Note: DeviceWrapperFatPartition currently only supports root directory
                        // For full recursive support, we'd need to enhance it to change directories
                        qDebug() << "SecureBoot: found directory" << filename << "(skipping recursion - not yet supported)";
                    }
                    else
                    {
                        // Regular file
                        fileList.append(filename);
                    }
                }
            }
            
            longFilename.clear();
        }
    }
    
    return fileList;
}

void DeviceWrapperFatPartition::listFilesInDirectory(const QString &dirPath, uint32_t dirCluster, QStringList &fileList)
{
    // Save current directory state
    uint32_t savedCurrentCluster = _fat32_currentRootDirCluster;
    QList<uint32_t> savedDirClusters = _currentDirClusters;
    
    // Set up for reading this directory
    if (_type == FAT32) {
        _fat32_currentRootDirCluster = dirCluster;
        seekCluster(_fat32_currentRootDirCluster);
        _currentDirClusters.clear();
        _currentDirClusters.append(_fat32_currentRootDirCluster);
    } else {
        // FAT16/FAT12 subdirectories work differently
        seekCluster(dirCluster);
    }
    
    struct dir_entry entry;
    QString longFilename;
    uint8_t lfnExpectedChecksum = 0;  // Checksum from LFN entries
    bool haveLfnChecksum = false;
    QList<QPair<QString, uint32_t>> subdirs; // Store subdirectories to process after
    
    while (true) {
        quint64 oldOffset = _offset;
        read((char *) &entry, sizeof(entry));
        
        if (entry.DIR_Name[0] == 0) {
            // End of directory
            _offset = oldOffset;
            break;
        }
        
        if (entry.DIR_Attr & ATTR_LONG_NAME) {
            // Long filename entry
            struct longfn_entry *l = (struct longfn_entry *) &entry;
            char lnamePartStr[26] = {0};
            memcpy(lnamePartStr, l->LDIR_Name1, 10);
            memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
            memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
            QString lnamePart((QChar *) lnamePartStr, 13);
            longFilename = lnamePart + longFilename;
            
            // Capture the checksum from the LFN entry
            lfnExpectedChecksum = l->LDIR_Chksum;
            haveLfnChecksum = true;
        } else {
            // Regular directory entry
            if (entry.DIR_Name[0] != 0xE5) { // Not deleted
                // Truncate long filename at null char
                if (longFilename.indexOf(QChar::Null) >= 0)
                    longFilename.truncate(longFilename.indexOf(QChar::Null));
                
                // Get short filename as fallback
                QString shortName;
                int nameLen = 8;
                for (int i = 0; i < nameLen && entry.DIR_Name[i] != ' '; i++) {
                    shortName += QChar(entry.DIR_Name[i]).toLower();  // Convert to lowercase for FAT case-insensitivity
                }
                // Check for extension
                if (entry.DIR_Name[8] != ' ') {
                    shortName += '.';
                    for (int i = 8; i < 11 && entry.DIR_Name[i] != ' '; i++) {
                        shortName += QChar(entry.DIR_Name[i]).toLower();  // Convert to lowercase
                    }
                }
                shortName = shortName.trimmed();
                
                // Choose filename: validate LFN checksum if we have an LFN
                QString filename;
                if (!longFilename.isEmpty() && haveLfnChecksum) {
                    // Calculate actual checksum of the short name
                    uint8_t actualChecksum = lfnChecksum(entry.DIR_Name);
                    
                    if (actualChecksum == lfnExpectedChecksum) {
                        // Checksum matches - LFN is valid
                        filename = longFilename;
                    } else {
                        // Checksum mismatch - LFN is orphaned/corrupt, use short name
                        qDebug() << "LFN checksum mismatch for" << shortName 
                                 << "(expected:" << lfnExpectedChecksum 
                                 << "actual:" << actualChecksum << "), using short name";
                        filename = shortName;
                    }
                } else {
                    // No LFN or no checksum - use short name
                    filename = shortName;
                }
                
                // Skip volume labels and current/parent directory markers
                if (!(entry.DIR_Attr & ATTR_VOLUME_ID) && 
                    filename != "." && filename != "..") {
                    
                    QString fullPath = dirPath.isEmpty() ? filename : dirPath + "/" + filename;
                    
                    if (entry.DIR_Attr & ATTR_DIRECTORY) {
                        // Get directory cluster
                        uint32_t subDirCluster = entry.DIR_FstClusLO;
                        if (_type == FAT32) {
                            subDirCluster |= (entry.DIR_FstClusHI << 16);
                        }
                        // Store for recursive processing
                        subdirs.append(qMakePair(fullPath, subDirCluster));
                    } else {
                        // Regular file - add to list
                        fileList.append(fullPath);
                    }
                }
            }
            
            longFilename.clear();
            haveLfnChecksum = false;
        }
        
        // Handle cluster boundary for FAT32
        if (_type == FAT32) {
            if ((pos()-_clusterOffset) % _bytesPerCluster == 0) {
                uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);
                if (nextCluster > 0xFFFFFF7) {
                    break; // End of cluster chain
                }
                if (_currentDirClusters.contains(nextCluster)) {
                    qDebug() << "Circular cluster reference detected in directory" << dirPath;
                    break;
                }
                _currentDirClusters.append(nextCluster);
                _fat32_currentRootDirCluster = nextCluster;
                seekCluster(_fat32_currentRootDirCluster);
            }
        }
    }
    
    // Restore directory state
    _fat32_currentRootDirCluster = savedCurrentCluster;
    _currentDirClusters = savedDirClusters;
    
    // Now recursively process subdirectories
    for (const auto &subdir : subdirs) {
        listFilesInDirectory(subdir.first, subdir.second, fileList);
    }
}

QStringList DeviceWrapperFatPartition::listAllFilesRecursive()
{
    QStringList fileList;
    
    if (_type == FAT32) {
        // Start from root directory cluster
        listFilesInDirectory("", _fat32_firstRootDirCluster, fileList);
    } else if (_type == FAT16) {
        // FAT16 has special root directory handling
        // First, list files in root using the original method
        struct dir_entry entry;
        QString longFilename;
        QList<QPair<QString, uint32_t>> subdirs;
        
        openDir(); // Opens root directory
        while (readDir(&entry)) {
            if (entry.DIR_Attr & ATTR_LONG_NAME) {
                struct longfn_entry *l = (struct longfn_entry *) &entry;
                char lnamePartStr[26] = {0};
                memcpy(lnamePartStr, l->LDIR_Name1, 10);
                memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
                memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
                QString lnamePart((QChar *) lnamePartStr, 13);
                longFilename = lnamePart + longFilename;
            } else {
                if (entry.DIR_Name[0] != 0xE5) {
                    if (longFilename.indexOf(QChar::Null) >= 0)
                        longFilename.truncate(longFilename.indexOf(QChar::Null));
                    
                    QString shortName;
                    for (int i = 0; i < 11 && entry.DIR_Name[i] != ' '; i++) {
                        if (i == 8 && entry.DIR_Name[i] != ' ') {
                            shortName += '.';
                        }
                        shortName += QChar(entry.DIR_Name[i]);
                    }
                    shortName = shortName.trimmed();
                    
                    QString filename = longFilename.isEmpty() ? shortName : longFilename;
                    
                    if (!(entry.DIR_Attr & ATTR_VOLUME_ID) && 
                        filename != "." && filename != "..") {
                        
                        if (entry.DIR_Attr & ATTR_DIRECTORY) {
                            uint32_t dirCluster = entry.DIR_FstClusLO;
                            subdirs.append(qMakePair(filename, dirCluster));
                        } else {
                            fileList.append(filename);
                        }
                    }
                }
                longFilename.clear();
            }
        }
        
        // Recursively process subdirectories
        for (const auto &subdir : subdirs) {
            listFilesInDirectory(subdir.first, subdir.second, fileList);
        }
    }
    
    return fileList;
}

void DeviceWrapperFatPartition::writeFile(const QString &filename, const QByteArray &contents)
{
    QList<uint32_t> clusterList;
    uint32_t pos = 0;
    uint32_t firstCluster;
    int clustersNeeded = (contents.length() + _bytesPerCluster - 1) / _bytesPerCluster;
    struct dir_entry entry;

    // Handle subdirectory paths (e.g., "EFI/boot/boot.img")
    if (filename.contains("/")) {
        QStringList parts = filename.split("/", Qt::SkipEmptyParts);
        if (parts.size() < 2) {
            qDebug() << "DeviceWrapperFatPartition::writeFile: invalid path" << filename;
            throw std::runtime_error("Invalid file path");
        }
        
        // Navigate to the subdirectory
        QString dirName = parts[0];
        struct dir_entry dirEntry;
        
        if (!getDirEntry(dirName, &dirEntry)) {
            qDebug() << "DeviceWrapperFatPartition::writeFile: directory not found:" << dirName;
            throw std::runtime_error("Directory not found");
        }
        
        if (!(dirEntry.DIR_Attr & ATTR_DIRECTORY)) {
            qDebug() << "DeviceWrapperFatPartition::writeFile:" << dirName << "is not a directory";
            throw std::runtime_error("Path component is not a directory");
        }
        
        // Save current directory context
        uint32_t savedRootDirCluster = _fat32_currentRootDirCluster;
        QList<uint32_t> savedDirClusters = _currentDirClusters;
        
        // Switch to subdirectory
        uint32_t dirCluster = (dirEntry.DIR_FstClusHI << 16) | dirEntry.DIR_FstClusLO;
        _fat32_currentRootDirCluster = dirCluster;
        _currentDirClusters.clear();
        _currentDirClusters.append(dirCluster);
        
        // Get the file entry in subdirectory (create if doesn't exist)
        QString fileNameOnly = parts[parts.size() - 1];
        getDirEntry(fileNameOnly, &entry, true);
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
        for (uint32_t cluster : std::as_const(clusterList))
        {
            seekCluster(cluster);
            write(contents.data()+pos, qMin((qsizetype)_bytesPerCluster, (qsizetype)(contents.length()-pos)));

            pos += _bytesPerCluster;
            if (pos >= contents.length())
                break;
        }

        if (clustersNeeded && contents.length() % _bytesPerCluster)
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
        
        // Restore directory context
        _fat32_currentRootDirCluster = savedRootDirCluster;
        _currentDirClusters = savedDirClusters;
        
        qDebug() << "DeviceWrapperFatPartition::writeFile: wrote" << filename;
        return;
    }

    // Simple case: file in root directory
    qDebug() << "writeFile: writing file" << filename << "to root directory";
    qDebug() << "writeFile: calling getDirEntry with createIfNotExist=true";
    getDirEntry(filename, &entry, true);
    qDebug() << "writeFile: getDirEntry returned, entry name:" << QByteArray((char*)entry.DIR_Name, 11).toHex(':');
    firstCluster = entry.DIR_FstClusLO;
    if (_type == FAT32)
        firstCluster |= (entry.DIR_FstClusHI << 16);

    qDebug() << "writeFile: firstCluster =" << firstCluster;

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
    for (uint32_t cluster : std::as_const(clusterList))
    {
        seekCluster(cluster);
        write(contents.data()+pos, qMin((qsizetype)_bytesPerCluster, (qsizetype)(contents.length()-pos)));

        pos += _bytesPerCluster;
        if (pos >= contents.length())
            break;
    }

    if (clustersNeeded && contents.length() % _bytesPerCluster)
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

    qDebug() << "writeFile: updating directory entry, firstCluster =" << firstCluster << "size =" << contents.length();
    entry.DIR_FstClusLO = (firstCluster & 0xFFFF);
    entry.DIR_FstClusHI = (firstCluster >> 16);
    entry.DIR_WrtDate = QDateToFATdate( QDate::currentDate() );
    entry.DIR_WrtTime = QTimeToFATtime( QTime::currentTime() );
    entry.DIR_LstAccDate = entry.DIR_WrtDate;
    entry.DIR_FileSize = contents.length();
    qDebug() << "writeFile: calling updateDirEntry for" << filename;
    updateDirEntry(&entry);
    qDebug() << "writeFile: updateDirEntry succeeded for" << filename;
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
    uint8_t lfnExpectedChecksum = 0;
    bool haveLfnChecksum = false;

    if (longFilename.isEmpty())
        throw std::runtime_error("Filename cannot not be empty");

    openDir();
    while (readDir(entry))
    {
        if (entry->DIR_Attr & ATTR_LONG_NAME)
        {
            struct longfn_entry *l = (struct longfn_entry *) entry;
            /* A part can have 13 UTF-16 characters */
            char lnamePartStr[26] = {0};
             /* Using memcpy() because it has no problems accessing unaligned struct members */
            memcpy(lnamePartStr, l->LDIR_Name1, 10);
            memcpy(lnamePartStr+10, l->LDIR_Name2, 12);
            memcpy(lnamePartStr+22, l->LDIR_Name3, 4);
            QString lnamePart( (QChar *) lnamePartStr, 13);
            filenameRead = lnamePart + filenameRead;
            
            // Capture checksum from LFN entry
            lfnExpectedChecksum = l->LDIR_Chksum;
            haveLfnChecksum = true;
        }
        else
        {
            if (entry->DIR_Name[0] != 0xE5)
            {
                if (filenameRead.indexOf(QChar::Null))
                    filenameRead.truncate(filenameRead.indexOf(QChar::Null));

                //qDebug() << "Long filename:" << filenameRead << "DIR_Name:" << QByteArray((char *) entry->DIR_Name, sizeof(entry->DIR_Name)) << "Short:" << _dirEntryToShortName(entry);

                // Validate LFN checksum if we have an LFN
                QString actualFilename;
                if (!filenameRead.isEmpty() && haveLfnChecksum) {
                    uint8_t actualChecksum = lfnChecksum(entry->DIR_Name);
                    if (actualChecksum == lfnExpectedChecksum) {
                        // Checksum matches - use LFN
                        actualFilename = filenameRead;
                    } else {
                        // Checksum mismatch - use short name
                        actualFilename = _dirEntryToShortName(entry);
                    }
                } else {
                    actualFilename = _dirEntryToShortName(entry);
                }

                if (actualFilename.toLower() == longFilenameLower)
                {
                    return true;
                }
            }

            filenameRead.clear();
            haveLfnChecksum = false;
        }
    }

    if (createIfNotExist)
    {
        qDebug() << "getDirEntry: creating new entry for" << longFilename;
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

        qDebug() << "getDirEntry: short filename:" << shortFilename.toHex(':');

        /* Verify short file name has not been taken yet, and if not try inserting numbers into the name */
        if (dirNameExists(shortFilename))
        {
            qDebug() << "getDirEntry: short filename already exists, finding alternative";
            for (int i=0; i<100; i++)
            {
                shortFilename = shortFilename.left( (i < 10 ? 7 : 6) )+QByteArray::number(i)+shortFilename.right(3);

                if (!dirNameExists(shortFilename))
                {
                    qDebug() << "getDirEntry: using alternative short filename:" << shortFilename.toHex(':');
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
        char *longFilenameStr = (char *) longFilenameWithNull.utf16();
        int lenBytes = longFilenameWithNull.length() * 2;
        int lfnFragments = (lenBytes + 25) / 26;

        qDebug() << "getDirEntry: writing" << lfnFragments << "long filename fragments";

        /* long file name directory entries are added in reverse order before the 8.3 entry */
        for (int i = lfnFragments; i > 0; i--)
        {
            memset(&longEntry, 0xff, sizeof(longEntry));
            longEntry.LDIR_Attr = ATTR_LONG_NAME;
            longEntry.LDIR_Chksum = shortFileNameChecksum;
            longEntry.LDIR_Ord = (i == lfnFragments) ? (LAST_LONG_ENTRY | i) : i;
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

        qDebug() << "getDirEntry: writing short filename entry";
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->DIR_Name, shortFilename.data(), sizeof(entry->DIR_Name));
        entry->DIR_Attr = ATTR_ARCHIVE;
        entry->DIR_CrtDate = QDateToFATdate( QDate::currentDate() );
        entry->DIR_CrtTime = QTimeToFATtime( QTime::currentTime() );

        writeDirEntryAtCurrentPos(entry);

        qDebug() << "getDirEntry: writing end-of-directory marker";
        /* Add an end-of-directory marker after our newly appended file */
        struct dir_entry endOfDir = {0};
        writeDirEntryAtCurrentPos(&endOfDir);
        
        qDebug() << "getDirEntry: successfully created entry with name:" << QByteArray((char*)entry->DIR_Name, 11).toHex(':');
        qDebug() << "getDirEntry: current root dir cluster:" << _fat32_currentRootDirCluster;
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

    // If the first byte is 0xE5 (deleted marker), we need to search for the original name
    // by temporarily using a non-deleted first byte for comparison
    bool searchingForDeleted = (dirEntry->DIR_Name[0] == 0xE5);

    openDir();
    quint64 oldOffset = _offset;

    while (readDir(&iterEntry))
    {
        /* Look for existing entry with same short filename */
        if (!(iterEntry.DIR_Attr & ATTR_LONG_NAME))
        {
            bool matches = false;
            if (searchingForDeleted)
            {
                // For deleted entries, compare all bytes except the first one
                // (The first byte will be different - 0xE5 vs the original character)
                matches = (memcmp(dirEntry->DIR_Name + 1, iterEntry.DIR_Name + 1, sizeof(iterEntry.DIR_Name) - 1) == 0);
            }
            else
            {
                // Normal comparison for non-deleted entries
                matches = (memcmp(dirEntry->DIR_Name, iterEntry.DIR_Name, sizeof(iterEntry.DIR_Name)) == 0);
            }
            
            if (matches)
            {
                /* seek() back and write out new entry */
                _offset = oldOffset;
                write((char *) dirEntry, sizeof(*dirEntry));
                return;
            }
        }

        oldOffset = _offset;
    }

    QByteArray searchName((char *) dirEntry->DIR_Name, sizeof(dirEntry->DIR_Name));
    qDebug() << "updateDirEntry: ERROR - entry not found, searched for:" << searchName.toHex(':');
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
            {
                qDebug() << "Reached end of FAT32 root directory, but no end-of-directory marker found. Adding one in new cluster.";
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
                seekCluster(nextCluster);
                QByteArray zeroes(_bytesPerCluster, 0);
                write(zeroes.data(), zeroes.length() );
            }

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
