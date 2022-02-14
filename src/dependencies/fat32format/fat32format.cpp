// Fat32 formatter version 1.05
// (c) Tom Thornhill 2007,2008,2009
// This software is covered by the GPL. 
// By using this tool, you agree to absolve Ridgecrop of an liabilities for lost data.
// Please backup any data you value before using this tool.

// |                      ALIGNING_SIZE * N                      |
// | BPB,FSInfo,Reserved | FAT1              | FAT2              | Cluster0
static const int ALIGNING_SIZE = 1024 * 1024;

#define WIN32_LEAN_AND_MEAN
#define STRICT_GS_ENABLED
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES        1
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_COUNT  1
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES          1
#define _CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES_MEMORY 1
#define _CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES_MEMORY   1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <winioctl.h>
#include <versionhelpers.h>

// EDIT for mingw
#ifndef max
#define max(a,b)  (((a) > (b)) ? (a) : (b))
#define min(a,b)  (((a) < (b)) ? (a) : (b))
#endif
#include <errno.h>
//


#pragma pack(push, 1)
struct FAT_BOOTSECTOR32
{
	// Common fields.
	BYTE sJmpBoot[3];
	CHAR sOEMName[8];
	WORD wBytsPerSec;
	BYTE bSecPerClus;
	WORD wRsvdSecCnt;
	BYTE bNumFATs;
	WORD wRootEntCnt;
	WORD wTotSec16; // if zero, use dTotSec32 instead
	BYTE bMedia;
	WORD wFATSz16;
	WORD wSecPerTrk;
	WORD wNumHeads;
	DWORD dHiddSec;
	DWORD dTotSec32;
	// Fat 32/16 only
	DWORD dFATSz32;
	WORD wExtFlags;
	WORD wFSVer;
	DWORD dRootClus;
	WORD wFSInfo;
	WORD wBkBootSec;
	BYTE Reserved[12];
	BYTE bDrvNum;
	BYTE Reserved1;
	BYTE bBootSig; // == 0x29 if next three fields are ok
	DWORD dBS_VolID;
	BYTE sVolLab[11];
	BYTE sBS_FilSysType[8];
};

struct FAT_FSINFO
{
	DWORD dLeadSig;         // 0x41615252
	BYTE sReserved1[480];   // zeros
	DWORD dStrucSig;        // 0x61417272
	DWORD dFree_Count;      // 0xFFFFFFFF
	DWORD dNxt_Free;        // 0xFFFFFFFF
	BYTE sReserved2[12];    // zeros
	DWORD dTrailSig;        // 0xAA550000
};

struct FAT_DIRECTORY
{
	UINT8  DIR_Name[8 + 3];
	UINT8  DIR_Attr;
	UINT8  DIR_NTRes;
	UINT8  DIR_CrtTimeTenth;
	UINT16 DIR_CrtTime;
	UINT16 DIR_CrtDate;
	UINT16 DIR_LstAccDate;
	UINT16 DIR_FstClusHI;
	UINT16 DIR_WrtTime;
	UINT16 DIR_WrtDate;
	UINT16 DIR_FstClusLO;
	UINT32 DIR_FileSize;
};
static_assert(sizeof(FAT_DIRECTORY) == 32);

#pragma pack(pop)

/*
28.2  CALCULATING THE VOLUME SERIAL NUMBER

For example, say a disk was formatted on 26 Dec 95 at 9:55 PM and 41.94
seconds.  DOS takes the date and time just before it writes it to the
disk.

Low order word is calculated:               Volume Serial Number is:
	Month & Day         12/26   0c1ah
	Sec & Hundrenths    41:94   295eh               3578:1d02
								-----
								3578h

High order word is calculated:
	Hours & Minutes     21:55   1537h
	Year                1995    07cbh
								-----
								1d02h
*/
DWORD get_volume_id()
{
	SYSTEMTIME s;

	GetLocalTime(&s);

	WORD lo = s.wDay + (s.wMonth << 8);
	WORD tmp = (s.wMilliseconds / 10) + (s.wSecond << 8);
	lo += tmp;

	WORD hi = s.wMinute + (s.wHour << 8);
	hi += s.wYear;

	return lo + (hi << 16);
}

struct format_params
{
	int sectors_per_cluster = 0;        // can be zero for default or 1,2,4,8,16,32 or 64
	bool make_protected_autorun = false;
	bool all_yes = false;
	char volume_label[sizeof(FAT_BOOTSECTOR32::sVolLab) + 1] = {};
};

[[noreturn]]
void die(_In_z_ PCSTR error)
{
	DWORD dw = GetLastError();

	if (dw != NO_ERROR)
	{
		PSTR lpMsgBuf;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			0,
			(PSTR)&lpMsgBuf,
			0, NULL);
		fprintf(stderr, "%s\nGetLastError()=%lu: %s\n", error, dw, lpMsgBuf);
		LocalFree(lpMsgBuf);
	}
	else
	{
		fprintf(stderr, "%s\n", error);
	}

	exit(dw);
}

/*
This is the Microsoft calculation from FATGEN

	DWORD RootDirSectors = 0;
	DWORD TmpVal1, TmpVal2, FATSz;

	TmpVal1 = DskSize - ( ReservedSecCnt + RootDirSectors);
	TmpVal2 = (256 * SecPerClus) + NumFATs;
	TmpVal2 = TmpVal2 / 2;
	FATSz = (TmpVal1 + (TmpVal2 - 1)) / TmpVal2;

	return( FatSz );
*/

DWORD get_fat_size_sectors(_In_ DWORD DskSize, _In_ DWORD ReservedSecCnt, _In_ DWORD SecPerClus, _In_ DWORD NumFATs, _In_ DWORD BytesPerSect)
{
	const ULONGLONG FatElementSize = 4;

	// This is based on 
	// http://hjem.get2net.dk/rune_moeller_barnkob/filesystems/fat.html
	// I've made the obvious changes for FAT32
	ULONGLONG Numerator = FatElementSize * (DskSize - ReservedSecCnt);
	ULONGLONG Denominator = (1ULL * SecPerClus * BytesPerSect) + (FatElementSize * NumFATs);
	ULONGLONG FatSz = Numerator / Denominator;
	// round up
	FatSz += 1;
	const ULONG AlignSectorCount = ALIGNING_SIZE / BytesPerSect;
	FatSz = ((FatSz * NumFATs + ReservedSecCnt + AlignSectorCount - 1) / AlignSectorCount * AlignSectorCount - ReservedSecCnt) / NumFATs;

	return (DWORD)FatSz;
}

void seek_to_sect(_In_ HANDLE hDevice, _In_ DWORD Sector, _In_ DWORD BytesPerSect)
{
	LARGE_INTEGER Offset;

	Offset.QuadPart = 1LL * Sector * BytesPerSect;
	BOOL ret = SetFilePointerEx(hDevice, Offset, nullptr, FILE_BEGIN);

	if (!ret)
		die("Failed to seek");
}

void write_sect(_In_ HANDLE hDevice, _In_ DWORD Sector, _In_ DWORD BytesPerSector, _In_ void* Data, _In_ DWORD NumSects)
{
	DWORD dwWritten;

	seek_to_sect(hDevice, Sector, BytesPerSector);
	BOOL ret = WriteFile(hDevice, Data, NumSects * BytesPerSector, &dwWritten, NULL);

	if (!ret)
		die("Failed to write");
}

void zero_sectors(_In_ HANDLE hDevice, _In_ DWORD Sector, _In_  DWORD BytesPerSect, _In_ DWORD NumSects)
{
	LARGE_INTEGER Start, End, Ticks, Frequency;
	DWORD qBytesTotal = NumSects * BytesPerSect;

	DWORD BurstSize = 1024 * 1024 / BytesPerSect;

	PBYTE pZeroSect = (PBYTE)VirtualAlloc(NULL, BytesPerSect * BurstSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	seek_to_sect(hDevice, Sector, BytesPerSect);

	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&Start);
	while (NumSects)
	{
		DWORD WriteSize = min(NumSects, BurstSize);
		DWORD dwWritten;

		BOOL ret = WriteFile(hDevice, pZeroSect, WriteSize * BytesPerSect, &dwWritten, NULL);
		if (!ret)
			die("Failed to write");

		NumSects -= WriteSize;
	}

	QueryPerformanceCounter(&End);
	Ticks.QuadPart = End.QuadPart - Start.QuadPart;
	double fTime = (double)(Ticks.QuadPart) / Frequency.QuadPart;
	double fBytesTotal = (double)qBytesTotal;
	printf("Wrote %lu bytes in %.2f seconds, %.2f Megabytes/sec\n", qBytesTotal, fTime, fBytesTotal / (fTime * 1024.0 * 1024.0));
}

BYTE get_spc(_In_ DWORD ClusterSizeKB, _In_ DWORD BytesPerSect)
{
	DWORD spc = (ClusterSizeKB * 1024) / BytesPerSect;
	return (BYTE)spc;
}
BYTE get_sectors_per_cluster(_In_ LONGLONG DiskSizeBytes, _In_ DWORD BytesPerSect)
{
	LONGLONG DiskSizeMB = DiskSizeBytes / (1024 * 1024);

	// Larger than 32,768 MB 32 KB
	if (DiskSizeMB > 32768)
		return get_spc(32, BytesPerSect);

	// 16,384 MB to 32,767 MB 16 KB
	if (DiskSizeMB > 16384)
		return get_spc(16, BytesPerSect);

	// 8,192 MB to 16,383 MB 8 KB
	if (DiskSizeMB > 8192)
		return get_spc(8, BytesPerSect);

	// 256 MB to 8,191 MB 4 KB
	if (DiskSizeMB > 256)
		return get_spc(4, BytesPerSect);

	// 128 MB to 256 MB 2 KB
	if (DiskSizeMB > 128)
		return get_spc(2, BytesPerSect);

	// 64 MB to 128 MB 1 KB
	if (DiskSizeMB > 64)
		return get_spc(1, BytesPerSect);

	// 32 MB to 64 MB 0.5 KB
	return 1;
}

int format_volume(_In_z_ LPCSTR vol, _In_ const format_params* params)
{
	DWORD cbRet;
	BOOL  bRet;
	DISK_GEOMETRY            dgDrive;
	PARTITION_INFORMATION    piDrive;
	PARTITION_INFORMATION_EX xpiDrive;
	bool bGPTMode = false;
	const WORD ReservedSectCount = 32;
	const BYTE NumFATs = 2;
	const BYTE VolId[12] = "NO NAME    ";
	DWORD FatSize;
	DWORD BytesPerSect;
	BYTE  SectorsPerCluster;
	DWORD TotalSectors;
	DWORD SystemAreaSize;
	DWORD UserAreaSize;
	ULONGLONG qTotalSectors;
	ULONGLONG FatNeeded, ClusterCount;

	if (!IsDebuggerPresent() && !params->all_yes)
	{
		printf("Warning ALL data on drive '%s' will be lost irretrievably, are you sure\n(y/n) :", vol);
		if (toupper(getchar()) != 'Y')
		{
			exit(EXIT_FAILURE);
		}
	}

	HANDLE hDevice = CreateFileA(
		vol,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_NO_BUFFERING,
		NULL
	);

	if (hDevice == INVALID_HANDLE_VALUE)
		die("Failed to open device - close any files before formatting and make sure you have Admin rights when using fat32format\n Are you SURE you're formatting the RIGHT DRIVE!!!");

	bRet = DeviceIoControl(hDevice, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &cbRet, NULL);

	if (!bRet)
		printf("Failed to allow extended DASD on device");

	bRet = DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &cbRet, NULL);

	if (!bRet)
		die("Failed to lock device");

	bRet = DeviceIoControl(hDevice, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dgDrive, sizeof(dgDrive), &cbRet, NULL);

	if (!bRet)
		die("Failed to get device geometry");

	bRet = DeviceIoControl(hDevice, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0, &piDrive, sizeof(piDrive), &cbRet, NULL);

	if (!bRet)
	{
		printf("IOCTL_DISK_GET_PARTITION_INFO failed, trying IOCTL_DISK_GET_PARTITION_INFO_EX\n");
		bRet = DeviceIoControl(hDevice, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &xpiDrive, sizeof(xpiDrive), &cbRet, NULL);

		if (!bRet)
			die("Failed to get partition info (both regular and _ex)");

		memset(&piDrive, 0, sizeof(piDrive));
		piDrive.StartingOffset.QuadPart = xpiDrive.StartingOffset.QuadPart;
		piDrive.PartitionLength.QuadPart = xpiDrive.PartitionLength.QuadPart;
		piDrive.HiddenSectors = (DWORD)(xpiDrive.StartingOffset.QuadPart / dgDrive.BytesPerSector);

		bGPTMode = xpiDrive.PartitionStyle != PARTITION_STYLE_MBR;
		printf("IOCTL_DISK_GET_PARTITION_INFO_EX ok, GPTMode=%d\n", bGPTMode);
	}

	BytesPerSect = dgDrive.BytesPerSector;
	__analysis_assume(BytesPerSect >= 512);

	// Checks on Disk Size
	qTotalSectors = piDrive.PartitionLength.QuadPart / dgDrive.BytesPerSector;
	// low end limit - 65536 sectors
	if (qTotalSectors < 65536)
	{
		// I suspect that most FAT32 implementations would mount this volume just fine, but the
		// spec says that we shouldn't do this, so we won't
		die("This drive is too small for FAT32 - there must be at least 64K clusters\n");
	}

	if (qTotalSectors >= 0xffffffff)
	{
		// This is a more fundamental limitation on FAT32 - the total sector count in the root dir
		// is 32bit. With a bit of creativity, FAT32 could be extended to handle at least 2^28 clusters
		// There would need to be an extra field in the FSInfo sector, and the old sector count could
		// be set to 0xffffffff. This is non standard though, the Windows FAT driver FASTFAT.SYS won't
		// understand this. Perhaps a future version of FAT32 and FASTFAT will handle this.
		die("This drive is too big for FAT32 - max 2TB supported\n");
	}

	if (IsWindowsVistaOrGreater())
	{
		STORAGE_PROPERTY_QUERY Query = { StorageAccessAlignmentProperty, PropertyStandardQuery };
		STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR Alignment = {};
		if (DeviceIoControl(
			hDevice,
			IOCTL_STORAGE_QUERY_PROPERTY,
			&Query,
			sizeof Query,
			&Alignment,
			sizeof Alignment,
			&cbRet,
			NULL
		))
		{
			if (Alignment.BytesOffsetForSectorAlignment)
				puts("Warning This disk has 'alignment offset'");
			if (piDrive.StartingOffset.QuadPart > 0 && piDrive.StartingOffset.QuadPart % Alignment.BytesPerPhysicalSector)
				puts("Warning This partition isn't aligned");
		}
	}

	FAT_BOOTSECTOR32* pFAT32BootSect = (FAT_BOOTSECTOR32*)VirtualAlloc(NULL, BytesPerSect, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	FAT_FSINFO* pFAT32FsInfo = (FAT_FSINFO*)VirtualAlloc(NULL, BytesPerSect, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	DWORD* pFirstSectOfFat = (DWORD*)VirtualAlloc(NULL, BytesPerSect, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	FAT_DIRECTORY* pFAT32Directory = (FAT_DIRECTORY*)VirtualAlloc(NULL, BytesPerSect, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

	if (!pFAT32BootSect || !pFAT32FsInfo || !pFirstSectOfFat || !pFAT32Directory)
		die("Failed to allocate memory");

	// fill out the boot sector and fs info
	pFAT32BootSect->sJmpBoot[0] = 0xEB;
	pFAT32BootSect->sJmpBoot[1] = 0x58; // jmp.s $+0x5a is 0xeb 0x58, not 0xeb 0x5a. Thanks Marco!
	pFAT32BootSect->sJmpBoot[2] = 0x90;
	memcpy(pFAT32BootSect->sOEMName, "MSWIN4.1", sizeof(FAT_BOOTSECTOR32::sOEMName));
	pFAT32BootSect->wBytsPerSec = (WORD)BytesPerSect;

	if (params->sectors_per_cluster)
		SectorsPerCluster = (BYTE)params->sectors_per_cluster;
	else
		SectorsPerCluster = get_sectors_per_cluster(piDrive.PartitionLength.QuadPart, BytesPerSect);

	pFAT32BootSect->bSecPerClus = SectorsPerCluster;
	pFAT32BootSect->wRsvdSecCnt = ReservedSectCount;
	pFAT32BootSect->bNumFATs = NumFATs;
	pFAT32BootSect->wRootEntCnt = 0;
	pFAT32BootSect->wTotSec16 = 0;
	pFAT32BootSect->bMedia = 0xF8;
	pFAT32BootSect->wFATSz16 = 0;
	pFAT32BootSect->wSecPerTrk = (WORD)dgDrive.SectorsPerTrack;
	pFAT32BootSect->wNumHeads = (WORD)dgDrive.TracksPerCylinder;
	pFAT32BootSect->dHiddSec = (DWORD)piDrive.HiddenSectors;
	TotalSectors = (DWORD)(piDrive.PartitionLength.QuadPart / dgDrive.BytesPerSector);
	pFAT32BootSect->dTotSec32 = TotalSectors;

	FatSize = get_fat_size_sectors(pFAT32BootSect->dTotSec32, pFAT32BootSect->wRsvdSecCnt, pFAT32BootSect->bSecPerClus, pFAT32BootSect->bNumFATs, BytesPerSect);

	pFAT32BootSect->dFATSz32 = FatSize;
	pFAT32BootSect->wExtFlags = 0;
	pFAT32BootSect->wFSVer = 0;
	pFAT32BootSect->dRootClus = 2;
	pFAT32BootSect->wFSInfo = 1;
	pFAT32BootSect->wBkBootSec = 6;
	pFAT32BootSect->bDrvNum = 0x80;
	pFAT32BootSect->Reserved1 = 0;
	pFAT32BootSect->bBootSig = 0x29;

	pFAT32BootSect->dBS_VolID = get_volume_id();
	memcpy(pFAT32BootSect->sVolLab, VolId, 11);
	memcpy(pFAT32BootSect->sBS_FilSysType, "FAT32   ", 8);
	((BYTE*)pFAT32BootSect)[510] = 0x55;
	((BYTE*)pFAT32BootSect)[511] = 0xaa;

	/* FATGEN103.DOC says "NOTE: Many FAT documents mistakenly say that this 0xAA55 signature occupies the "last 2 bytes of
	the boot sector". This statement is correct if - and only if - BPB_BytsPerSec is 512. If BPB_BytsPerSec is greater than
	512, the offsets of these signature bytes do not change (although it is perfectly OK for the last two bytes at the end
	of the boot sector to also contain this signature)."

	Windows seems to only check the bytes at offsets 510 and 511. Other OSs might check the ones at the end of the sector,
	so we'll put them there too.
	*/
	if (BytesPerSect != 512)
	{
		((BYTE*)pFAT32BootSect)[BytesPerSect - 2] = 0x55;
		((BYTE*)pFAT32BootSect)[BytesPerSect - 1] = 0xaa;
	}

	// FSInfo sect
	pFAT32FsInfo->dLeadSig = 0x41615252;
	pFAT32FsInfo->dStrucSig = 0x61417272;
	pFAT32FsInfo->dFree_Count = MAXDWORD;
	pFAT32FsInfo->dNxt_Free = MAXDWORD;
	pFAT32FsInfo->dTrailSig = 0xaa550000;

	// First FAT Sector
	pFirstSectOfFat[0] = 0x0ffffff8;  // Reserved cluster 1 media id in low byte
	pFirstSectOfFat[1] = 0x0fffffff;  // Reserved cluster 2 EOC
	pFirstSectOfFat[2] = 0x0fffffff;  // end of cluster chain for root dir

	// Write boot sector, fats
	// Sector 0 Boot Sector
	// Sector 1 FSInfo 
	// Sector 2 More boot code - we write zeros here
	// Sector 3 unused
	// Sector 4 unused
	// Sector 5 unused
	// Sector 6 Backup boot sector
	// Sector 7 Backup FSInfo sector
	// Sector 8 Backup 'more boot code'
	// zero'd sectors upto ReservedSectCount
	// FAT1  ReservedSectCount to ReservedSectCount + FatSize
	// ...
	// FATn  ReservedSectCount to ReservedSectCount + FatSize
	// RootDir - allocated to cluster2

	UserAreaSize = TotalSectors - ReservedSectCount - NumFATs * FatSize;
	ClusterCount = UserAreaSize / SectorsPerCluster;

	// Sanity check for a cluster count of >2^28, since the upper 4 bits of the cluster values in the FAT are reserved.
	// Cluster 0x00000000     meaning unused
	// Cluster 0x00000001     reserved
	// Cluster 0x0FFFFFF7     bad cluster
	// Cluster 0x0FFFFFF[8-F] end of cluster chain
	if (ClusterCount > 0x0FFFFFF4)
	{
		die("This drive has more than 2^28 clusters, try to specify a larger cluster size or use the default (i.e. don't use -cXX)\n");
	}

	// Sanity check - < 64K clusters means that the volume will be misdetected as FAT16
	if (ClusterCount < 65536)
	{
		die("FAT32 must have at least 65536 clusters, try to specify a smaller cluster size or use the default (i.e. don't use -cXX)\n");
	}

	// Sanity check, make sure the fat is big enough
	// Convert the cluster count into a Fat sector count, and check the fat size value we calculated 
	// earlier is OK.
	FatNeeded = ClusterCount * 4;
	FatNeeded = (FatNeeded + BytesPerSect - 1) / BytesPerSect;
	if (FatNeeded > FatSize)
	{
		die("This drive is too big for this version of fat32format, check for an upgrade\n");
	}

	// Now we're commited - print some info first
	printf("Size : %gGB %lu sectors\n", piDrive.PartitionLength.QuadPart / (1024.f * 1024.f * 1024.f), TotalSectors);
	printf("%lu Bytes Per Sector, Cluster size %lu bytes\n", BytesPerSect, SectorsPerCluster * BytesPerSect);
	printf("Volume ID is %04lX:%04lX\n", pFAT32BootSect->dBS_VolID >> 16, pFAT32BootSect->dBS_VolID & 0xffff);
	printf("%u Reserved Sectors, %lu Sectors per FAT, %u fats\n", ReservedSectCount, FatSize, NumFATs);

	printf("%llu Total clusters\n", ClusterCount);

	// fix up the FSInfo sector
	pFAT32FsInfo->dFree_Count = (UserAreaSize / SectorsPerCluster) - 1;
	pFAT32FsInfo->dNxt_Free = 3; // clusters 0-1 resered, we used cluster 2 for the root dir

	printf("%lu Free Clusters\n", pFAT32FsInfo->dFree_Count);
	// Work out the Cluster count

	printf("Formatting drive %s...\n", vol);

	// Once zero_sectors has run, any data on the drive is basically lost....

	// First zero out ReservedSect + FatSize * NumFats + SectorsPerCluster
	SystemAreaSize = (pFAT32BootSect->wRsvdSecCnt + (pFAT32BootSect->bNumFATs * FatSize) + SectorsPerCluster);
	printf("Clearing out %lu sectors for Reserved sectors, fats and root directory...\n", SystemAreaSize);
	zero_sectors(hDevice, 0, BytesPerSect, SystemAreaSize);
	puts("Initialising reserved sectors and FATs...");
	// Now we should write the boot sector and fsinfo twice, once at 0 and once at the backup boot sect position
	for (int i = 0; i < 2; i++)
	{
		int SectorStart = (i == 0) ? 0 : pFAT32BootSect->wBkBootSec;
		write_sect(hDevice, SectorStart, BytesPerSect, pFAT32BootSect, 1);
		write_sect(hDevice, SectorStart + 1, BytesPerSect, pFAT32FsInfo, 1);
	}

	// Write the first fat sector in the right places
	for (int i = 0; i < pFAT32BootSect->bNumFATs; i++)
	{
		int SectorStart = pFAT32BootSect->wRsvdSecCnt + (i * FatSize);
		write_sect(hDevice, SectorStart, BytesPerSect, pFirstSectOfFat, 1);
	}

	if (params->make_protected_autorun)
	{
		memcpy(pFAT32Directory[0].DIR_Name, "AUTORUN INF", sizeof(FAT_DIRECTORY::DIR_Name));
		pFAT32Directory[0].DIR_Attr = FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
		write_sect(hDevice, ReservedSectCount + NumFATs * FatSize, BytesPerSect, pFAT32Directory, 1);
	}

	// The filesystem recogniser in Windows XP doesn't use the partition type - in can be 
	// set to pretty much anything other Os's like Dos (still useful for Norton Ghost!) and Windows ME might, 
	// so we could fix it here 
	// On the other hand, I'm not sure that exposing big partitions to Windows ME/98 is a very good idea
	// There are a couple of issues here - 
	// 1) WinME/98 doesn't know about 48bit LBA, so IDE drives bigger than 137GB will cause it 
	//    problems. Rather than refuse to mount them, it uses 28bit LBA which wraps 
	//    around, so writing to files above the 137GB boundary will erase the FAT and root dirs.
	// 2) Win98 and WinME have 16 bit scandisk tools, which you need to disable, assuming you
	//    can get third party support for 48bit LBA, or use a USB external case, most of which 
	//    will let you use a 48bit LBA drive.
	//    see http://www.48bitlba.com/win98.htm for instructions

	// If we have a GPT disk, don't mess with the partition type
	if (!bGPTMode)
	{
		SET_PARTITION_INFORMATION spiDrive = { PARTITION_FAT32_XINT13 };
		bRet = DeviceIoControl(hDevice, IOCTL_DISK_SET_PARTITION_INFO, &spiDrive, sizeof(spiDrive), NULL, 0, &cbRet, NULL);

		if (!bRet)
		{
			// This happens because the drive is a Super Floppy
			// i.e. with no partition table. Disk.sys creates a PARTITION_INFORMATION
			// record spanning the whole disk and then fails requests to set the 
			// partition info since it's not actually stored on disk. 
			// So only complain if there really is a partition table to set      
			if (piDrive.HiddenSectors)
				die("Failed to set parition info");
		}
	}

	bRet = DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &cbRet, NULL);

	if (!bRet)
		die("Failed to dismount device");

	bRet = DeviceIoControl(hDevice, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &cbRet, NULL);

	if (!bRet)
		die("Failed to unlock device");

	CloseHandle(hDevice);

	puts("Done");

	return TRUE;
}

[[noreturn]]
void usage(void)
{
	puts(
		"Usage Fat32Format [-cN] [-lLABEL] [-p] [-y] " R"({ C: | \\.\C: | \\?\Volume{GUID} })" "\n"
		"Erase all data on specified volume, format it for FAT32\n"
		"\n"
		"    -c  Specify a cluster size by sector count.\n"
		"        Accepts 1, 2, 4, 8, 16, 32, 64, 128\n"
		"        EXAMPLE: Fat32Format -c4 X:  - use 4 sectors per cluster\n"
		"    -l  Specify volume label.\n"
		"        If exceeds 11-bytes, truncate label.\n"
		"    -p  Make immutable AUTORUN.INF on root directory.\n"
		"        This file cannot do anything on Windows.\n"
		"\n"
		"Modified Version see https://github.com/0xbadfca11/fat32format \n"
		"\n"
		"Original Version 1.07, see http://www.ridgecrop.demon.co.uk/fat32format.htm \n"
		"This software is covered by the GPL \n"
		"Use with care - Ridgecrop are not liable for data lost using this tool"
	);
	exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
	if (const FARPROC SetDefaultDllDirectoriesPtr = GetProcAddress(GetModuleHandleW(L"kernel32"), "SetDefaultDllDirectories"))
	{
		reinterpret_cast<decltype(SetDefaultDllDirectories)*>(SetDefaultDllDirectoriesPtr)(LOAD_LIBRARY_SEARCH_SYSTEM32);
	}

	if (argc < 2)
	{
		usage();
	}

	PCSTR volume_argv = nullptr;
	format_params p;
	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-' || argv[i][0] == '/')
		{
			switch (argv[i][1])
			{
			case 'c':
				if (strlen(argv[i]) >= 3)
				{
					p.sectors_per_cluster = atol(&argv[i][2]);
					if ((p.sectors_per_cluster & (p.sectors_per_cluster - 1)) != 0 || p.sectors_per_cluster > 128)
					{
						printf("Ignoring bad cluster size %d\n", p.sectors_per_cluster);
						p.sectors_per_cluster = 0;
					}
				}
				else
				{
					usage();
				}
				break;
			case 'l':
				size_t len;
				if ((len = strlen(argv[i])) >= 3)
				{
					switch (strncpy_s(p.volume_label, &argv[i][2], _TRUNCATE))
					{
					case 0:
						break;
					case STRUNCATE:
						puts("Warning Volume label too long. Truncate volume label.");
						break;
					default:
						perror("strncpy_s");
						exit(EXIT_FAILURE);
					}
					if (p.volume_label[0] == '\xE5')
					{
						puts("Warning Volume label started with 0xE5. Available until unmount.");
					}
				}
				else
				{
					usage();
				}
				break;
			case 'p':
				if (strcmp(argv[i], "-p") != 0)
				{
					usage();
				}
				p.make_protected_autorun = true;
				break;
			case 'y':
				if (strcmp(argv[i], "-y") != 0)
				{
					usage();
				}
				p.all_yes = true;
				break;
			case '?':
				usage();
				break;
			default:
				printf("bad flag '-%c'\n", argv[i][1]);
				usage();
				break;
			}
		}
		else
		{
			if (volume_argv)
			{
				usage();
			}
			volume_argv = argv[i];
		}
	}
	if (!volume_argv)
	{
		usage();
	}

	if (strncmp(volume_argv, R"(\\?\Volume{)", 11) == 0)
	{
		char volume_guid[50];
        size_t pos;

		strcpy_s(volume_guid, volume_argv);
                pos = strlen(volume_guid) - 1;
        if (volume_guid[pos] == '\\')
		{
			volume_guid[pos] = '\0';
		}
		format_volume(volume_guid, &p);
		strcat_s(volume_guid, "\\");
		if (!SetVolumeLabelA(volume_guid, p.volume_label))
		{
			die("Failed to set volume label");
		}
	}
	else
	{
		char volume[8] = R"(\\.\?:)";
		if (strlen(volume_argv) == 2 && isalpha(volume_argv[0]) && volume_argv[1] == ':')
		{
			volume[4] = volume_argv[0];
		}
		else if (strlen(volume_argv) == 6 && strncmp(volume_argv, R"(\\.\)", 4) == 0 && isalpha(volume_argv[4]) && volume_argv[5] == ':')
		{
			volume[4] = volume_argv[4];
		}
		else
		{
			usage();
		}
		format_volume(volume, &p);
		strcat_s(volume, "\\");
		if (!SetVolumeLabelA(volume, p.volume_label))
		{
			die("Failed to set volume label");
		}
	}
}
