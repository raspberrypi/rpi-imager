#include "devicewrapperfatpartition.h"
#include "devicewrapperstructs.h"
#include <QDebug>
#include <QStringList>

namespace {
// Largest sector the FAT specification permits, and so the largest a
// conforming boot sector may ask for.
constexpr uint16_t kMaxBytesPerSector = 4096;

// The 8.3 short name, assembled once instead of in ten places.
//
// Ten copies is how one came to stop at a NUL and nothing narrower, and how
// two came to disagree with the other three about case. DIR_Name is a fixed
// 11-byte field, space-padded with no separator stored, so the dot is
// inserted rather than read.
//
// Lowered, because FAT stores 8.3 upper-cased: as stored, one file was
// "CONFIG.TXT" through one entry point and "config.txt" through another. A
// long filename keeps its case; that one is the user's.
QString shortNameFromEntry(const struct dir_entry &entry)
{
    QString name;
    for (int i = 0; i < 8 && entry.DIR_Name[i] != ' ' && entry.DIR_Name[i] >= 0x20; i++)
        name += QChar(entry.DIR_Name[i]).toLower();
    if (entry.DIR_Name[8] != ' ')
    {
        name += '.';
        for (int i = 8; i < 11 && entry.DIR_Name[i] != ' ' && entry.DIR_Name[i] >= 0x20; i++)
            name += QChar(entry.DIR_Name[i]).toLower();
    }
    return name;
}

// One part of a long filename: 13 UTF-16 code units the entry splits across
// three fields, which is the on-disk layout rather than a choice. memcpy
// because those fields are unaligned inside the packed entry.
//
// Six identical copies of this is why a control character a directory puts in
// a long name is still passed through -- deciding what to do about it meant
// finding all six first. One place now.
QString longNamePartFromEntry(const struct longfn_entry *l)
{
    char part[26] = {0};
    memcpy(part, l->LDIR_Name1, 10);
    memcpy(part + 10, l->LDIR_Name2, 12);
    memcpy(part + 22, l->LDIR_Name3, 4);
    const QString raw((QChar *) part, 13);

    // Control characters dropped, the name kept. A directory can put them
    // in, and this name is what SecureBoot lists through and then reads
    // back -- and what a caller may put in a path. The short name already
    // stops at every byte below 0x20.
    //
    // NUL is the exception and must survive: it terminates the name, the
    // caller truncates there, and the 0xFFFF padding after it goes with it.
    QString cleaned;
    cleaned.reserve(raw.size());
    for (const QChar c : raw) {
        if (c.unicode() >= 0x20 || c.unicode() == 0)
            cleaned.append(c);
    }
    return cleaned;
}
} // namespace

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

    // Both divisors below are fields read off the disk, so they are refused
    // before they are divided by rather than after. exFAT stores zero in
    // BPB_BytsPerSec by definition, and a corrupt image can store zero in
    // either. x86 raises SIGFPE on an integer division by zero and takes the
    // application down mid-write; AArch64 quietly yields zero, which is why
    // the exFAT case still reached its error message and looked correct.
    if (!_bytesPerSector)
    {
        _type = EXFAT;
        throw std::runtime_error("exFAT file system not supported");
    }
    // The format allows four sector sizes and no others; the old test was "a
    // multiple of four", which believed anything up to 65532. It has to hold
    // here rather than after the type is decided, because allocateCluster()
    // reads a whole sector into a buffer fixed at the largest of the four.
    if (_bytesPerSector != 512 && _bytesPerSector != 1024
        && _bytesPerSector != 2048 && _bytesPerSector != kMaxBytesPerSector)
        throw std::runtime_error("FAT file system: invalid bytes per sector");
    if (!bpb.fat16.BPB_SecPerClus)
        throw std::runtime_error("FAT file system: invalid sectors per cluster");

    /* Every figure below is a sector count read off the disk, and every
     * offset is one of them multiplied by the sector size. In uint32_t that
     * product wraps a little over four gigabytes -- and a wrapped offset is
     * not a failure, it is a seek to the wrong place on somebody's card.
     * Worked out in 64 bits and held to the partition, so a boot sector that
     * does not describe this partition is refused instead.
     */
    const quint64 partitionBytes = _partLen;
    const auto within = [partitionBytes](quint64 value, const char *what) -> quint64 {
        if (value > partitionBytes)
            throw std::runtime_error(std::string("FAT file system: ") + what
                                     + " lies outside the partition");
        return value;
    };

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

    // Taken in 64 bits: the three terms are disk figures and their sum can
    // pass what the partition holds, at which point the subtraction wraps
    // and a tiny file system reads as an enormous one.
    const quint64 metaSectors = quint64(bpb.fat16.BPB_RsvdSecCnt)
                                + (quint64(bpb.fat16.BPB_NumFATs) * _fatSize)
                                + _fat16_rootDirSectors;
    if (metaSectors > totalSectors)
        throw std::runtime_error("FAT file system: more reserved sectors than sectors");
    dataSectors = uint32_t(quint64(totalSectors) - metaSectors);
    countOfClusters = dataSectors / bpb.fat16.BPB_SecPerClus;
    _bytesPerCluster = uint32_t(within(quint64(bpb.fat16.BPB_SecPerClus) * _bytesPerSector,
                                       "a cluster"));
    _fat16_firstRootDirSector = uint32_t(quint64(bpb.fat16.BPB_RsvdSecCnt)
                                         + (quint64(bpb.fat16.BPB_NumFATs) * bpb.fat16.BPB_FATSz16));
    _fat32_firstRootDirCluster = bpb.fat32.BPB_RootClus;
    // The 8.3 name stops at any byte below 0x20, not only at a NUL. FAT
    // forbids all of them, and a corrupt entry produced names like a single
    // 0x01 -- which reach SecureBoot's extraction, keyed into a map, and
    // qDebug lines that may land in a terminal. No exception is made for the
    // 0x05 that FAT substitutes for a leading 0xE5: such a name is non-ASCII
    // and therefore always carries a long filename entry, which every one of
    // these callers prefers when it is present.

    // "Current" starts where "first" is. It was the one member the
    // constructor never set, so a freshly built partition read it
    // uninitialised. Valgrind reports a branch on an uninitialised value 380
    // times over the image tests; ASan cannot see this class of fault.
    _fat32_currentRootDirCluster = _fat32_firstRootDirCluster;

    if (countOfClusters < 4085)
        _type = FAT12;
    else if (countOfClusters < 65525)
        _type = FAT16;
    else
        _type = FAT32;

    if (_type == FAT12)
        throw std::runtime_error("FAT12 file system not supported");

    _firstFatStartOffset = uint32_t(within(quint64(bpb.fat16.BPB_RsvdSecCnt) * _bytesPerSector,
                                           "the first file allocation table"));
    for (int i = 0; i < bpb.fat16.BPB_NumFATs; i++)
    {
        const quint64 start = quint64(_firstFatStartOffset)
                              + (quint64(i) * _fatSize * _bytesPerSector);
        _fatStartOffset.append(uint32_t(within(start, "a file allocation table")));
    }

    if (_type == FAT16)
    {
        _fat32_fsinfoSector = 0;
        _clusterOffset = uint32_t(within((quint64(_fat16_firstRootDirSector)
                                          + _fat16_rootDirSectors) * _bytesPerSector,
                                         "the first cluster"));
    }
    else
    {
        _fat32_fsinfoSector = bpb.fat32.BPB_FSInfo;
        _clusterOffset = uint32_t(within(quint64(_firstFatStartOffset)
                                         + (quint64(bpb.fat16.BPB_NumFATs) * _fatSize
                                            * _bytesPerSector),
                                         "the first cluster"));
    }
}

uint32_t DeviceWrapperFatPartition::allocateCluster()
{
    // Fixed at the format's maximum rather than sized from the disk: this
    // is a stack buffer, and _bytesPerSector arrives from the boot sector.
    // The constructor refuses anything larger, so only the first
    // _bytesPerSector bytes are ever read or examined.
    char sector[kMaxBytesPerSector];
    int bytesPerEntry = (_type == FAT16 ? 2 : 4);
    int entriesPerSector = _bytesPerSector/bytesPerEntry;
    uint32_t cluster;
    uint16_t *f16 = (uint16_t *) &sector;
    uint32_t *f32 = (uint32_t *) &sector;

    seek(_firstFatStartOffset);

    for (int i = 0; i < _fatSize; i++)
    {
        read(sector, _bytesPerSector);

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

bool DeviceWrapperFatPartition::isEndOfChain(uint32_t cluster) const
{
    return (_type == FAT32) ? (cluster > 0xFFFFFF7) : (cluster > 0xFFF7);
}

QList<uint32_t> DeviceWrapperFatPartition::getClusterChain(uint32_t firstCluster)
{
    QList<uint32_t> list;
    uint32_t cluster = firstCluster;

    while (true)
    {
        if (isEndOfChain(cluster))
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
    /* Clusters 0 and 1 are reserved and name no data, so a chain arriving at
     * one is corrupt -- and subtracting two from it wraps, putting the seek
     * somewhere arbitrary on a card that is being written. The offset is
     * taken in 64 bits for the same reason: a cluster number times a cluster
     * size passes four gigabytes on any card worth writing to.
     */
    if (cluster < 2)
        throw std::runtime_error("Corrupt file system. Cluster number below the first");

    const quint64 offset = quint64(_clusterOffset)
                           + (quint64(cluster - 2) * _bytesPerCluster);
    if (offset > _partLen)
        throw std::runtime_error("Corrupt file system. Cluster outside the partition");

    seek(qint64(offset));
}

/* Find an existing file by path, creating nothing along the way. Every
   accessor goes through this, so all of them agree about where a file is. */
bool DeviceWrapperFatPartition::findFileEntry(const QString &filename, struct dir_entry *entry,
                                              uint32_t *dirClusterOut)
{
    QString leaf;
    uint32_t dirCluster = 0;

    try {
        if (!resolveDirectoryPath(filename, leaf, dirCluster, false))
            return false;
        if (!getDirEntry(leaf, entry, false, dirCluster))
            return false;
    } catch (const DirectoryLoopError &e) {
        /* A loop stops the search, not the application: replacing a
           customisation file deletes the old one first, and a card whose
           overlays/ turns back on itself must not end the write. Only this
           one is caught -- a cluster outside the partition means the numbers
           are nonsense, and reading on would land somewhere arbitrary. */
        qDebug() << "DeviceWrapperFatPartition: cannot search for" << filename
                 << ":" << e.what();
        return false;
    }

    if (dirClusterOut)
        *dirClusterOut = dirCluster;
    return true;
}

bool DeviceWrapperFatPartition::fileExists(const QString &filename)
{
    struct dir_entry entry;
    return findFileEntry(filename, &entry);
}

qint64 DeviceWrapperFatPartition::fileSize(const QString &filename)
{
    struct dir_entry entry;
    if (!findFileEntry(filename, &entry))
        return -1;
    return static_cast<qint64>(entry.DIR_FileSize);
}

bool DeviceWrapperFatPartition::deleteFile(const QString &filename)
{
    struct dir_entry entry;

    uint32_t dirCluster = 0;
    if (!findFileEntry(filename, &entry, &dirCluster)) {
        qDebug() << "DeviceWrapperFatPartition::deleteFile: entry not found:" << filename;
        return false;
    }

    /* A directory is marked deleted like anything else. Whatever it holds is
       the caller's to have removed first. */

    /* Mark the entry as deleted by setting first byte to 0xE5 */
    entry.DIR_Name[0] = 0xE5;
    updateDirEntry(&entry, dirCluster);

    /* TODO: Free the clusters used by the file in the FAT
       For now, just marking as deleted is sufficient for our use case
       since we're about to rewrite the entire partition anyway */

    qDebug() << "DeviceWrapperFatPartition::deleteFile: deleted" << filename;
    return true;
}

QByteArray DeviceWrapperFatPartition::readFile(const QString &filename)
{
    struct dir_entry entry;

    if (!findFileEntry(filename, &entry))
        return QByteArray(); /* File not found */

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

    /* The declared size is a 32-bit field read off the card, and until here
       nothing has weighed it against the clusters the file actually owns. An
       entry claiming 4 GB otherwise allocates 4 GB before a byte is read, and
       returns the shortfall as zeroes. Take the smallest of the declared
       size, the chain's capacity and the partition. */
    const quint64 chainCapacity = static_cast<quint64>(clusterList.size()) * _bytesPerCluster;
    const quint64 cappedLen = qMin(static_cast<quint64>(len), qMin(chainCapacity, _partLen));
    if (cappedLen < len)
    {
        qDebug() << "DeviceWrapperFatPartition::readFile: file" << filename
                 << "declares" << len << "bytes but holds" << cappedLen << "- truncating";
        len = static_cast<uint32_t>(cappedLen);
        if (len == 0)
            return QByteArray();
    }

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
        if (IS_LONG_NAME_ENTRY(entry.DIR_Attr))
        {
            // Long filename entry
            struct longfn_entry *l = (struct longfn_entry *) &entry;
            const QString lnamePart = longNamePartFromEntry(l);
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
                
                // Short filename as fallback. This loop is where the
                // extension used to be lost -- "CONFIG  TXT" came back as
                // "CONFIG" -- and where the case diverged from the other
                // three walkers. Both are the helper's business now.
                const QString shortName = shortNameFromEntry(entry);
                
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
                    else if (!filename.isEmpty())
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

void DeviceWrapperFatPartition::listFilesInDirectory(const QString &dirPath, uint32_t dirCluster,
                                                     QStringList &fileList,
                                                     QSet<uint32_t> &visitedDirClusters, int depth)
{
    /* Refuse a directory already on the walk. A subdirectory entry carries
       the cluster its contents start at, and that is a number off the card:
       nothing stops it naming a directory further up. The chain within one
       directory is guarded further down, the tree was not, so such a card
       recursed until the process ran out of memory -- 88 bytes of table was
       enough to reach 2.2 GB. The depth cap is for a loop long enough to
       pass for a deep tree; no real card is anywhere near it. */
    constexpr int kMaxDirectoryDepth = 64;
    if (depth > kMaxDirectoryDepth) {
        qDebug() << "FAT directory tree deeper than" << kMaxDirectoryDepth
                 << "at" << dirPath << "- not descending further";
        return;
    }
    if (visitedDirClusters.contains(dirCluster)) {
        qDebug() << "FAT directory" << dirPath << "points back at cluster"
                 << dirCluster << "- already walked, not descending";
        return;
    }
    visitedDirClusters.insert(dirCluster);

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
        
        if (IS_LONG_NAME_ENTRY(entry.DIR_Attr)) {
            // Long filename entry
            struct longfn_entry *l = (struct longfn_entry *) &entry;
            const QString lnamePart = longNamePartFromEntry(l);
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
                
                // Short filename as fallback.
                const QString shortName = shortNameFromEntry(entry);
                
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
                // An entry with no usable name is skipped, not listed. A
                // short name is eight bytes of whatever the directory holds,
                // and a corrupt one can be all NULs, which stops the name at
                // nothing; listing that hands the caller an empty string to
                // open. Found by fuzz_fatdir.
                if (!(entry.DIR_Attr & ATTR_VOLUME_ID) && !filename.isEmpty() &&
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
        listFilesInDirectory(subdir.first, subdir.second, fileList,
                             visitedDirClusters, depth + 1);
    }
}

QStringList DeviceWrapperFatPartition::listAllFilesRecursive()
{
    QStringList fileList;
    QSet<uint32_t> visitedDirClusters;
    
    if (_type == FAT32) {
        // Start from root directory cluster
        listFilesInDirectory("", _fat32_firstRootDirCluster, fileList, visitedDirClusters);
    } else if (_type == FAT16) {
        // FAT16 has special root directory handling
        // First, list files in root using the original method
        struct dir_entry entry;
        QString longFilename;
        QList<QPair<QString, uint32_t>> subdirs;
        
        openDir(); // Opens root directory
        while (readDir(&entry)) {
            if (IS_LONG_NAME_ENTRY(entry.DIR_Attr)) {
                struct longfn_entry *l = (struct longfn_entry *) &entry;
                const QString lnamePart = longNamePartFromEntry(l);
                longFilename = lnamePart + longFilename;
            } else {
                if (entry.DIR_Name[0] != 0xE5) {
                    if (longFilename.indexOf(QChar::Null) >= 0)
                        longFilename.truncate(longFilename.indexOf(QChar::Null));
                    
                    // Short filename as fallback. This walk matters most:
                    // SecureBoot's extractFatPartitionFiles() lists through
                    // here and then calls readFile() on each name, so a name
                    // this returns and readFile() cannot resolve is a file
                    // left out of the signed boot image without a word. They
                    // now build the name from the same function.
                    const QString shortName = shortNameFromEntry(entry);
                    
                    QString filename = longFilename.isEmpty() ? shortName : longFilename;
                    
                    if (!(entry.DIR_Attr & ATTR_VOLUME_ID) && !filename.isEmpty() &&
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
            listFilesInDirectory(subdir.first, subdir.second, fileList,
                                 visitedDirClusters);
        }
    }
    
    return fileList;
}

uint32_t DeviceWrapperFatPartition::createDirectory(const QString &name, uint32_t parentCluster)
{
    /* The cluster first, so a failure to allocate leaves no entry in the
       parent pointing at a directory that was never made. */
    const uint32_t cluster = allocateCluster();

    /* Zeroed before anything is put in it. Whatever the cluster held before
       would otherwise read as directory entries, and a stale name in a fresh
       directory is a file appearing from nowhere. */
    QByteArray zeroes(_bytesPerCluster, 0);
    seekCluster(cluster);
    write(zeroes.data(), zeroes.length());

    /* "." and "..", which every directory but the root carries. ".." holds
       nought for the root whatever cluster the root starts at, which is how
       a walk upwards knows it has arrived at the top. */
    struct dir_entry dot;
    memset(&dot, 0, sizeof(dot));
    memset(dot.DIR_Name, ' ', sizeof(dot.DIR_Name));
    dot.DIR_Name[0] = '.';
    dot.DIR_Attr = ATTR_DIRECTORY;
    dot.DIR_FstClusLO = cluster & 0xFFFF;
    dot.DIR_FstClusHI = cluster >> 16;
    dot.DIR_CrtDate = QDateToFATdate( QDate::currentDate() );
    dot.DIR_CrtTime = QTimeToFATtime( QTime::currentTime() );
    dot.DIR_WrtDate = dot.DIR_CrtDate;
    dot.DIR_WrtTime = dot.DIR_CrtTime;
    dot.DIR_LstAccDate = dot.DIR_CrtDate;

    struct dir_entry dotdot = dot;
    dotdot.DIR_Name[1] = '.';
    dotdot.DIR_FstClusLO = parentCluster & 0xFFFF;
    dotdot.DIR_FstClusHI = parentCluster >> 16;

    seekCluster(cluster);
    write((char *) &dot, sizeof(dot));
    write((char *) &dotdot, sizeof(dotdot));

    /* getDirEntry() writes a file entry; what makes it a directory is the
       attribute and the cluster, neither of which it sets. The length stays
       at nought -- a directory with a size reads as a file that happens to
       carry the directory bit. */
    struct dir_entry entry;
    getDirEntry(name, &entry, true, parentCluster);
    entry.DIR_Attr = ATTR_DIRECTORY;
    entry.DIR_FstClusLO = cluster & 0xFFFF;
    entry.DIR_FstClusHI = cluster >> 16;
    entry.DIR_FileSize = 0;
    entry.DIR_WrtDate = dot.DIR_WrtDate;
    entry.DIR_WrtTime = dot.DIR_WrtTime;
    entry.DIR_LstAccDate = dot.DIR_LstAccDate;
    updateDirEntry(&entry, parentCluster);

    return cluster;
}

bool DeviceWrapperFatPartition::resolveDirectoryPath(const QString &path, QString &leaf,
                                                     uint32_t &dirCluster, bool createMissing)
{
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);

    /* Refused rather than thrown, so the read paths can answer "no such
       file" for a path naming none; writeFile() raises it itself. A trailing
       separator names a directory, and taking its last component as the file
       would create "overlays" as a file in the root. */
    if (parts.isEmpty() || path.endsWith(QLatin1Char('/')))
    {
        qDebug() << "DeviceWrapperFatPartition: path names no file:" << path;
        return false;
    }

    leaf = parts.last();
    dirCluster = 0;

    for (int i = 0; i + 1 < parts.size(); i++)
    {
        struct dir_entry entry;

        if (!getDirEntry(parts[i], &entry, false, dirCluster))
        {
            if (!createMissing)
            {
                qDebug() << "DeviceWrapperFatPartition: directory not found:" << parts[i]
                         << "in" << path;
                return false;
            }
            dirCluster = createDirectory(parts[i], dirCluster);
            continue;
        }

        if (!(entry.DIR_Attr & ATTR_DIRECTORY))
        {
            qDebug() << "DeviceWrapperFatPartition:" << parts[i] << "is not a directory";
            return false;
        }

        uint32_t next = entry.DIR_FstClusLO;
        if (_type == FAT32)
            next |= (uint32_t(entry.DIR_FstClusHI) << 16);

        /* An entry pointing at nothing is corrupt, and cluster nought is
           the root -- so walking into it would put the file there under its
           own name, where the caller never looks. */
        if (next < 2)
        {
            qDebug() << "DeviceWrapperFatPartition: directory" << parts[i] << "has no cluster";
            return false;
        }

        dirCluster = next;
    }

    return true;
}

void DeviceWrapperFatPartition::writeFile(const QString &filename, const QByteArray &contents)
{
    QList<uint32_t> clusterList;
    uint32_t pos = 0;
    uint32_t firstCluster;
    int clustersNeeded = (contents.length() + _bytesPerCluster - 1) / _bytesPerCluster;
    struct dir_entry entry;

    /* Any directory named along the way that is not there yet is made. A
       component naming an existing file is refused outright: placing the
       file in the root instead would leave it where no caller looks. */
    QString leaf;
    uint32_t dirCluster = 0;
    if (!resolveDirectoryPath(filename, leaf, dirCluster, true))
        throw std::runtime_error("Cannot write to: " + filename.toStdString());

    qDebug() << "writeFile: writing" << leaf << "to directory cluster" << dirCluster;
    getDirEntry(leaf, &entry, true, dirCluster);
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
    updateDirEntry(&entry, dirCluster);
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

bool DeviceWrapperFatPartition::getDirEntry(const QString &longFilename, struct dir_entry *entry, bool createIfNotExist, uint32_t dirCluster)
{
    QString filenameRead, longFilenameLower = longFilename.toLower();
    uint8_t lfnExpectedChecksum = 0;
    bool haveLfnChecksum = false;

    if (longFilename.isEmpty())
        throw std::runtime_error("Filename cannot not be empty");

    openDirAt(dirCluster);
    while (readDir(entry))
    {
        if (IS_LONG_NAME_ENTRY(entry->DIR_Attr))
        {
            struct longfn_entry *l = (struct longfn_entry *) entry;
            const QString lnamePart = longNamePartFromEntry(l);
            filenameRead = lnamePart + filenameRead;
            
            // Capture checksum from LFN entry
            lfnExpectedChecksum = l->LDIR_Chksum;
            haveLfnChecksum = true;
        }
        else
        {
            if (entry->DIR_Name[0] != 0xE5)
            {
                /* A long name carries a NUL and 0xFFFF padding only when it
                   does not fill its last entry. At an exact multiple of 13
                   characters there is neither, so the search must succeed
                   here rather than truncate to what indexOf() returned. */
                const qsizetype nul = filenameRead.indexOf(QChar::Null);
                if (nul >= 0)
                    filenameRead.truncate(nul);

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
        if (dirNameExists(shortFilename, dirCluster))
        {
            qDebug() << "getDirEntry: short filename already exists, finding alternative";
            for (int i=0; i<100; i++)
            {
                shortFilename = shortFilename.left( (i < 10 ? 7 : 6) )+QByteArray::number(i)+shortFilename.right(3);

                if (!dirNameExists(shortFilename, dirCluster))
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
        size_t lenBytes = static_cast<size_t>(longFilenameWithNull.length()) * 2;
        size_t lfnFragments = (lenBytes + 25) / 26;

        qDebug() << "getDirEntry: writing" << lfnFragments << "long filename fragments";

        /* long file name directory entries are added in reverse order before the 8.3 entry */
        for (size_t i = lfnFragments; i > 0; i--)
        {
            memset(&longEntry, 0xff, sizeof(longEntry));
            longEntry.LDIR_Attr = ATTR_LONG_NAME;
            longEntry.LDIR_Chksum = shortFileNameChecksum;
            longEntry.LDIR_Ord = (i == lfnFragments) ? (LAST_LONG_ENTRY | static_cast<uint8_t>(i)) : static_cast<uint8_t>(i);
            longEntry.LDIR_FstClusLO = 0;
            longEntry.LDIR_Type = 0;

            size_t start = (i-1) * 26;
            if (start < lenBytes) {
                memcpy(longEntry.LDIR_Name1, longFilenameStr+start, qMin(lenBytes-start, sizeof(longEntry.LDIR_Name1)));
            }
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

bool DeviceWrapperFatPartition::dirNameExists(const QByteArray dirname, uint32_t dirCluster)
{
    struct dir_entry entry;

    openDirAt(dirCluster);
    while (readDir(&entry))
    {
        if (!IS_LONG_NAME_ENTRY(entry.DIR_Attr)
                && dirname == QByteArray((char *) entry.DIR_Name, sizeof(entry.DIR_Name)))
        {
            return true;
        }
    }

    return false;
}

void DeviceWrapperFatPartition::updateDirEntry(struct dir_entry *dirEntry, uint32_t dirCluster)
{
    struct dir_entry iterEntry;

    // If the first byte is 0xE5 (deleted marker), we need to search for the original name
    // by temporarily using a non-deleted first byte for comparison
    bool searchingForDeleted = (dirEntry->DIR_Name[0] == 0xE5);

    openDirAt(dirCluster);
    quint64 oldOffset = _offset;

    while (readDir(&iterEntry))
    {
        /* Look for existing entry with same short filename */
        if (!IS_LONG_NAME_ENTRY(iterEntry.DIR_Attr))
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

    if (!_inFat16RootDir)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, allocate/seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (isEndOfChain(nextCluster))
            {
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
            }

            if (_currentDirClusters.contains(nextCluster))
                throw DirectoryLoopError("Circular cluster references in FAT32 directory detected");
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
    openDirAt(0);
}

// Start a walk at `cluster`, or at the root when it is nought.
//
// openDir() used to be the only way in and always went to the root, so
// anything wanting to look inside a subdirectory had to set the traversal
// state itself and then avoid every function that calls openDir() -- which
// is getDirEntry() and updateDirEntry(), the two that find and write
// entries. deleteFile() carries an inline copy of the search for that
// reason, and writeFile() refused subdirectories outright rather than
// silently creating the entry in the root.
void DeviceWrapperFatPartition::openDirAt(uint32_t cluster)
{
    _inFat16RootDir = (_type != FAT32 && cluster == 0);

    if (_inFat16RootDir)
    {
        // Held to 64 bits: the constructor refused a root directory outside
        // the partition, but the product itself wraps in uint32_t.
        seek(qint64(quint64(_fat16_firstRootDirSector) * _bytesPerSector));
        return;
    }

    // On FAT16 a subdirectory is an ordinary cluster chain like any other,
    // so only the root is special there.
    _fat32_currentRootDirCluster =
        (cluster == 0) ? _fat32_firstRootDirCluster : cluster;
    seekCluster(_fat32_currentRootDirCluster);
    /* Keep track of directory clusters we seeked to, to be able
       to detect circular references */
    _currentDirClusters.clear();
    _currentDirClusters.append(_fat32_currentRootDirCluster);
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

    if (!_inFat16RootDir)
    {
        if ((pos()-_clusterOffset) % _bytesPerCluster == 0)
        {
            /* We reached the end of the cluster, seek to next cluster */
            uint32_t nextCluster = getFAT(_fat32_currentRootDirCluster);

            if (isEndOfChain(nextCluster))
            {
                qDebug() << "Reached end of directory cluster chain, but no end-of-directory marker found. Adding one in new cluster.";
                nextCluster = allocateCluster(_fat32_currentRootDirCluster);
                seekCluster(nextCluster);
                QByteArray zeroes(_bytesPerCluster, 0);
                write(zeroes.data(), zeroes.length() );
            }

            if (_currentDirClusters.contains(nextCluster))
                throw DirectoryLoopError("Circular cluster references in FAT32 directory detected");
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
        // Widened before it is added to. The count is a field read off the
        // card and the delta can be negative, so a card claiming three free
        // clusters while ten are released wrapped to about four billion and
        // that was written back. The format has a value for "not known", and
        // saying so is better than a number that cannot be true.
        const qint64 updated = qint64(fsinfo.FSI_Free_Count) + deltaClusters;
        fsinfo.FSI_Free_Count = (updated < 0 || updated > 0xFFFFFFFELL)
                                    ? 0xFFFFFFFFu
                                    : quint32(updated);
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
