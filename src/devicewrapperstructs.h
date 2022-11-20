#ifndef DEVICEWRAPPERSTRUCTS_H
#define DEVICEWRAPPERSTRUCTS_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <stdint.h>

#pragma pack(push, 1)

/* MBR on-disk structures */

struct mbr_partition_entry {
    unsigned char bootable;
    char begin_hsc[3];
    unsigned char id;
    char end_hsc[3];
    uint32_t starting_sector;
    uint32_t nr_of_sectors;
};

struct mbr_table {
    char bootcode[440];
    unsigned char diskid[4];
    unsigned char flags[2];
    mbr_partition_entry part[4];
    unsigned char signature[2];
};

/* GUID on-disk structures
 * https://www.intel.com/content/dam/www/public/us/en/zip/efi-110.zip (p. 369)
 */

struct gpt_header {
    char     Signature[8];
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t HeaderCRC32;
    uint32_t Reserved;
    uint64_t MyLBA;
    uint64_t AlternateLBA;
    uint64_t FirstUsableLBA;
    uint64_t LastUsableLBA;
    unsigned char DiskGUID[16];
    uint64_t PartitionEntryLBA;
    uint32_t NumberOfPartitionEntries;
    uint32_t SizeOfPartitionEntry;
    uint32_t PartitionEntryArrayCRC32;
    char     Reserved2[420]; /* Assuming 512 byte sectors */
};

struct gpt_partition {
    unsigned char PartitionTypeGuid[16];
    unsigned char UniquePartitionGuid[16];
    uint64_t StartingLBA;
    uint64_t EndingLBA;
    uint64_t Attributes;
    char     PartitionName[72];
};

/* File Allocation Table
 * https://academy.cba.mit.edu/classes/networking_communications/SD/FAT.pdf
 */

struct fat16_bpb {
    uint8_t  BS_jmpBoot[3];
    char     BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t  BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t  BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t  BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;

    uint8_t  BS_DrvNum;
    uint8_t  BS_Reserved1;
    uint8_t  BS_BootSig;
    uint32_t BS_VolID;
    char     BS_VolLab[11];
    char     BS_FilSysType[8];

    uint8_t  Zeroes[448];
    uint8_t  Signature[2]; /* 0x55aa */
};

struct fat32_bpb {
    uint8_t  BS_jmpBoot[3];
    char     BS_OEMName[8];
    uint16_t BPB_BytsPerSec;
    uint8_t  BPB_SecPerClus;
    uint16_t BPB_RsvdSecCnt;
    uint8_t  BPB_NumFATs;
    uint16_t BPB_RootEntCnt;
    uint16_t BPB_TotSec16;
    uint8_t  BPB_Media;
    uint16_t BPB_FATSz16;
    uint16_t BPB_SecPerTrk;
    uint16_t BPB_NumHeads;
    uint32_t BPB_HiddSec;
    uint32_t BPB_TotSec32;

    uint32_t BPB_FATSz32;
    uint16_t BPB_ExtFlags;
    uint16_t BPB_FSVer;
    uint32_t BPB_RootClus;
    uint16_t BPB_FSInfo;
    uint16_t BPB_BkBootSec;
    uint8_t  BPB_Reserved[12];
    uint8_t  BS_DrvNum;
    uint8_t  BS_Reserved1;
    uint8_t  BS_BootSig;
    uint32_t BS_VolID;
    char     BS_VolLab[11];
    char     BS_FilSysType[8];

    uint8_t  Zeroes[420];
    uint8_t  Signature[2]; /* 0x55aa */
};

union fat_bpb {
    struct fat16_bpb fat16;
    struct fat32_bpb fat32;
};

struct dir_entry {
    unsigned char DIR_Name[11];
    uint8_t  DIR_Attr;
    uint8_t  DIR_NTRes;
    uint8_t  DIR_CrtTimeTenth;
    uint16_t DIR_CrtTime;
    uint16_t DIR_CrtDate;
    uint16_t DIR_LstAccDate;
    uint16_t DIR_FstClusHI;
    uint16_t DIR_WrtTime;
    uint16_t DIR_WrtDate;
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
};

struct longfn_entry {
    uint8_t  LDIR_Ord;
    char     LDIR_Name1[10];
    uint8_t  LDIR_Attr;
    uint8_t  LDIR_Type;
    uint8_t  LDIR_Chksum;
    char     LDIR_Name2[12];
    uint16_t LDIR_FstClusLO;
    char     LDIR_Name3[4];
};

#define LAST_LONG_ENTRY 0x40

#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

struct FSInfo {
    uint8_t  FSI_LeadSig[4];  /* 0x52 0x52 0x61 0x41 */
    uint8_t  FSI_Reserved1[480];
    uint8_t  FSI_StrucSig[4]; /* 0x72 0x72 0x41 0x61 */
    uint32_t FSI_Free_Count;
    uint32_t FSI_Nxt_Free;
    uint8_t  FSI_Reserved2[12];
    uint8_t  FSI_TrailSig[4]; /* 0x00 0x00 0x55 0xAA */
};

#pragma pack(pop)

#endif // DEVICEWRAPPERSTRUCTS_H
