/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "disk_formatter.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <bit>
#include <vector>

namespace rpi_imager {

namespace {

// Check if system is little-endian
inline bool IsLittleEndian() {
  constexpr std::uint16_t test = 0x0001;
  return *reinterpret_cast<const std::uint8_t*>(&test) == 0x01;
}

// Manual byte swapping for different sizes
template<typename T>
T ByteSwap(T value) {
  if constexpr (sizeof(T) == 1) {
    return value;
  } else if constexpr (sizeof(T) == 2) {
    return ((value & 0xFF) << 8) | ((value >> 8) & 0xFF);
  } else if constexpr (sizeof(T) == 4) {
    return ((value & 0xFF000000U) >> 24) |
           ((value & 0x00FF0000U) >> 8) |
           ((value & 0x0000FF00U) << 8) |
           ((value & 0x000000FFU) << 24);
  } else if constexpr (sizeof(T) == 8) {
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
  } else {
    return value;
  }
}

// Convert to little-endian values (disk storage format)
template<typename T>
T ToLittleEndian(T value) {
  if (IsLittleEndian()) {
    return value;
  } else {
    return ByteSwap(value);
  }
}

// RAII file descriptor wrapper
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) : fd_(fd) {}
  ~FileDescriptor() { 
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  // Non-copyable, movable
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }
  FileDescriptor& operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
      if (fd_ >= 0) close(fd_);
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }
  bool is_valid() const { return fd_ >= 0; }

 private:
  int fd_;
};

}  // namespace <anonymous>

Result<void> DiskFormatter::FormatDrive(
    const std::string& device_path, 
    std::uint64_t device_size_bytes) {
  
  FileDescriptor fd(open(device_path.c_str(), O_RDWR | O_SYNC));
  if (!fd.is_valid()) {
    return Result<void>(FormatError::kFileOpenError);
  }

  // Write MBR
  if (auto result = WriteMbr(fd.get(), device_size_bytes); !result) {
    return result;
  }

  // Calculate partition size
  std::uint32_t total_sectors = device_size_bytes / kSectorSize;
  std::uint32_t partition_size_sectors = total_sectors - kPartitionStartSector;

  // Write FAT32 filesystem
  return WriteFat32(fd.get(), kPartitionStartSector, partition_size_sectors);
}

Result<void> DiskFormatter::FormatFile(
    const std::string& file_path,
    std::uint64_t file_size_bytes) {
  
  FileDescriptor fd(open(file_path.c_str(), 
                        O_CREAT | O_RDWR | O_TRUNC, 
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!fd.is_valid()) {
    return Result<void>(FormatError::kFileOpenError);
  }

  // Extend file to desired size
  if (ftruncate(fd.get(), file_size_bytes) != 0) {
    return Result<void>(FormatError::kFileWriteError);
  }

  return FormatDrive("/proc/self/fd/" + std::to_string(fd.get()), file_size_bytes);
}

Result<void> DiskFormatter::WriteMbr(
    int fd, 
    std::uint64_t device_size_bytes) const {
  
  std::array<std::uint8_t, kSectorSize> mbr_sector{};
  
  // Calculate total sectors
  std::uint64_t total_sectors = device_size_bytes / kSectorSize;
  if (total_sectors > UINT32_MAX) {
    total_sectors = UINT32_MAX;  // MBR limitation
  }
  
  std::uint32_t partition_sectors = static_cast<std::uint32_t>(total_sectors) - kPartitionStartSector;

  // Create partition entry at offset 446
  MbrPartitionEntry partition{};
  partition.status = 0x80;  // Bootable
  partition.partition_type = kFat32PartitionType;
  partition.first_lba = ToLittleEndian(kPartitionStartSector);
  partition.num_sectors = ToLittleEndian(partition_sectors);

  // Simple CHS calculation for compatibility
  // For modern drives, LBA is what matters
  std::uint32_t start_cyl = kPartitionStartSector / (63 * 255);
  std::uint32_t start_head = (kPartitionStartSector / 63) % 255;
  std::uint32_t start_sect = (kPartitionStartSector % 63) + 1;
  
  partition.first_cylinder = start_cyl & 0xFF;
  partition.first_head = start_head;
  partition.first_sector = ((start_cyl >> 2) & 0xC0) | (start_sect & 0x3F);

  std::uint32_t end_lba = kPartitionStartSector + partition_sectors - 1;
  std::uint32_t end_cyl = end_lba / (63 * 255);
  std::uint32_t end_head = (end_lba / 63) % 255;
  std::uint32_t end_sect = (end_lba % 63) + 1;
  
  partition.last_cylinder = std::min(end_cyl, 1023U) & 0xFF;
  partition.last_head = end_head;
  partition.last_sector = ((std::min(end_cyl, 1023U) >> 2) & 0xC0) | (end_sect & 0x3F);

  // Copy partition entry to MBR
  std::memcpy(mbr_sector.data() + 446, &partition, sizeof(partition));

  // MBR signature
  mbr_sector[510] = 0x55;
  mbr_sector[511] = 0xAA;

  return WriteAtOffset(fd, 0, mbr_sector.data(), mbr_sector.size());
}

Result<void> DiskFormatter::WriteFat32(
    int fd,
    std::uint32_t partition_start_sector,
    std::uint32_t partition_size_sectors) const {
  
  Fat32Config config = CalculateFat32Config(partition_size_sectors);
  config.total_sectors = partition_size_sectors;

  // Write boot sector
  if (auto result = WriteBootSector(fd, partition_start_sector, config); !result) {
    return result;
  }

  // Write FSInfo sector
  if (auto result = WriteFsInfo(fd, partition_start_sector + 1, config); !result) {
    return result;
  }

  // Write backup boot sector
  if (auto result = WriteBootSector(fd, partition_start_sector + config.reserved_sectors / 2, config); !result) {
    return result;
  }

  // Write FAT tables
  std::uint32_t fat_start_sector = partition_start_sector + config.reserved_sectors;
  if (auto result = WriteFatTables(fd, fat_start_sector, config); !result) {
    return result;
  }

  // Write root directory
  std::uint32_t sectors_per_fat = CalculateSectorsPerFat(config);
  std::uint32_t root_sector = fat_start_sector + (config.num_fats * sectors_per_fat);
  return WriteRootDirectory(fd, root_sector);
}

Result<void> DiskFormatter::WriteBootSector(
    int fd,
    std::uint32_t offset_sectors,
    const Fat32Config& config) const {
  
  Fat32BootSector boot_sector{};

  // Boot jump instruction. Yes, it's x86, and yes, it's a jump to the start of the boot sector.
  // And no, it doesn't actually get executed on the arm64 device.
  boot_sector.jump_instruction = {0xEB, 0x58, 0x90};
  
  // OEM name
  std::string oem = "mkfs.fat";
  std::copy_n(oem.begin(), std::min(oem.size(), boot_sector.oem_name.size()), 
              boot_sector.oem_name.begin());

  // BIOS Parameter Block
  boot_sector.bytes_per_sector = ToLittleEndian(static_cast<std::uint16_t>(kSectorSize));
  boot_sector.sectors_per_cluster = config.sectors_per_cluster;
  boot_sector.reserved_sectors = ToLittleEndian(config.reserved_sectors);
  boot_sector.num_fats = config.num_fats;
  boot_sector.root_entries = 0;  // FAT32
  boot_sector.total_sectors_16 = 0;  // FAT32
  boot_sector.media_descriptor = 0xF8;  // Fixed disk
  boot_sector.sectors_per_fat_16 = 0;  // FAT32
  boot_sector.sectors_per_track = ToLittleEndian(static_cast<std::uint16_t>(63));
  boot_sector.num_heads = ToLittleEndian(static_cast<std::uint16_t>(255));
  boot_sector.hidden_sectors = ToLittleEndian(static_cast<std::uint32_t>(offset_sectors));
  boot_sector.total_sectors_32 = ToLittleEndian(config.total_sectors);

  // FAT32 specific fields
  std::uint32_t sectors_per_fat = CalculateSectorsPerFat(config);
  boot_sector.sectors_per_fat_32 = ToLittleEndian(sectors_per_fat);
  boot_sector.ext_flags = 0;
  boot_sector.fs_version = 0;
  boot_sector.root_cluster = ToLittleEndian(static_cast<std::uint32_t>(2));
  boot_sector.fs_info_sector = ToLittleEndian(static_cast<std::uint16_t>(1));
  boot_sector.backup_boot_sector = ToLittleEndian(static_cast<std::uint16_t>(6));
  
  // Extended fields
  boot_sector.drive_number = 0x80;
  boot_sector.boot_signature = 0x29;
  boot_sector.volume_id = ToLittleEndian(config.volume_id);
  
  // Volume label (11 bytes, space-padded)
  std::array<char, 11> padded_label{};
  std::fill(padded_label.begin(), padded_label.end(), ' ');
  std::copy_n(config.volume_label.begin(), 
              std::min(config.volume_label.size(), padded_label.size()),
              padded_label.begin());
  boot_sector.volume_label = padded_label;

  // Filesystem type
  std::string fs_type = "FAT32   ";
  std::copy_n(fs_type.begin(), boot_sector.fs_type.size(), boot_sector.fs_type.begin());

  // Boot signature
  boot_sector.signature = ToLittleEndian(static_cast<std::uint16_t>(0xAA55));

  std::uint64_t offset = static_cast<std::uint64_t>(offset_sectors) * kSectorSize;
  return WriteStructAtOffset(fd, offset, boot_sector);
}

Result<void> DiskFormatter::WriteFsInfo(
    int fd,
    std::uint32_t offset_sectors,
    const Fat32Config& config) const {
  
  Fat32FsInfo fs_info{};
  
  fs_info.lead_signature = ToLittleEndian(static_cast<std::uint32_t>(0x41615252));
  fs_info.struct_signature = ToLittleEndian(static_cast<std::uint32_t>(0x61417272));
  
  // Calculate free clusters
  std::uint32_t sectors_per_fat = CalculateSectorsPerFat(config);
  std::uint32_t data_sectors = config.total_sectors - config.reserved_sectors - 
                               (config.num_fats * sectors_per_fat);
  std::uint32_t total_clusters = data_sectors / config.sectors_per_cluster;
  std::uint32_t free_clusters = total_clusters - 1;  // Minus root directory cluster
  
  fs_info.free_count = ToLittleEndian(free_clusters);
  fs_info.next_free = ToLittleEndian(static_cast<std::uint32_t>(3));  // Next available cluster
  fs_info.trail_signature = ToLittleEndian(static_cast<std::uint32_t>(0xAA550000));

  std::uint64_t offset = static_cast<std::uint64_t>(offset_sectors) * kSectorSize;
  return WriteStructAtOffset(fd, offset, fs_info);
}

Result<void> DiskFormatter::WriteFatTables(
    int fd,
    std::uint32_t fat_start_sector,
    const Fat32Config& config) const {
  
  std::uint32_t sectors_per_fat = CalculateSectorsPerFat(config);
  std::uint32_t fat_size_bytes = sectors_per_fat * kSectorSize;
  
  // Create FAT table in memory
  std::vector<std::uint8_t> fat_table(fat_size_bytes, 0);
  auto* fat_entries = reinterpret_cast<std::uint32_t*>(fat_table.data());
  
  // First three entries are special
  fat_entries[0] = ToLittleEndian(0x0FFFFFF8);  // Media descriptor + end marker
  fat_entries[1] = ToLittleEndian(0x0FFFFFFF);  // End of cluster chain
  fat_entries[2] = ToLittleEndian(0x0FFFFFFF);  // Root directory end marker

  // Write both FAT copies
  for (std::uint8_t fat_num = 0; fat_num < config.num_fats; ++fat_num) {
    std::uint64_t fat_offset = static_cast<std::uint64_t>(fat_start_sector + (fat_num * sectors_per_fat)) * kSectorSize;
    if (auto result = WriteAtOffset(fd, fat_offset, fat_table.data(), fat_table.size()); !result) {
      return result;
    }
  }
  
  return Result<void>();
}

Result<void> DiskFormatter::WriteRootDirectory(
    int fd,
    std::uint32_t root_cluster_sector) const {
  
  // Root directory is just an empty cluster
  std::array<std::uint8_t, 4096> root_cluster{};  // 8 sectors * 512 bytes
  
  std::uint64_t offset = static_cast<std::uint64_t>(root_cluster_sector) * kSectorSize;
  return WriteAtOffset(fd, offset, root_cluster.data(), root_cluster.size());
}

Fat32Config DiskFormatter::CalculateFat32Config(std::uint32_t partition_size_sectors) const {
  Fat32Config config;
  config.total_sectors = partition_size_sectors;
  
  // Adjust cluster size based on partition size for optimal performance
  if (partition_size_sectors < 66600) {  // < 32.5MB
    config.sectors_per_cluster = 1;   // 512 bytes
  } else if (partition_size_sectors < 532480) {  // < 260MB
    config.sectors_per_cluster = 2;   // 1KB
  } else if (partition_size_sectors < 16777216) {  // < 8GB
    config.sectors_per_cluster = 8;   // 4KB
  } else if (partition_size_sectors < 33554432) {  // < 16GB
    config.sectors_per_cluster = 16;  // 8KB
  } else {
    config.sectors_per_cluster = 32;  // 16KB
  }
  
  return config;
}

std::uint32_t DiskFormatter::CalculateSectorsPerFat(const Fat32Config& config) const {
  // Calculate number of data sectors
  std::uint32_t data_sectors = config.total_sectors - config.reserved_sectors;
  
  // Each cluster needs 4 bytes in FAT table
  std::uint32_t clusters = data_sectors / config.sectors_per_cluster;
  std::uint32_t fat_bytes = (clusters + 2) * 4;  // +2 for reserved entries
  std::uint32_t fat_sectors = (fat_bytes + kSectorSize - 1) / kSectorSize;
  
  // Account for space taken by FAT tables themselves
  std::uint32_t total_fat_sectors = fat_sectors * config.num_fats;
  data_sectors -= total_fat_sectors;
  clusters = data_sectors / config.sectors_per_cluster;
  fat_bytes = (clusters + 2) * 4;
  fat_sectors = (fat_bytes + kSectorSize - 1) / kSectorSize;
  
  return fat_sectors;
}

Result<void> DiskFormatter::WriteAtOffset(
    int fd,
    std::uint64_t offset,
    const std::uint8_t* data,
    std::size_t size) const {
  
  if (lseek(fd, offset, SEEK_SET) == -1) {
    return Result<void>(FormatError::kFileSeekError);
  }
  
  std::size_t bytes_written = 0;
  while (bytes_written < size) {
    ssize_t result = write(fd, data + bytes_written, size - bytes_written);
    if (result <= 0) {
      return Result<void>(FormatError::kFileWriteError);
    }
    bytes_written += result;
  }
  
  return Result<void>();
}

template<typename T>
Result<void> DiskFormatter::WriteStructAtOffset(
    int fd,
    std::uint64_t offset,
    const T& structure) const {
  
  const auto* data = reinterpret_cast<const std::uint8_t*>(&structure);
  return WriteAtOffset(fd, offset, data, sizeof(T));
}

}  // namespace rpi_imager 