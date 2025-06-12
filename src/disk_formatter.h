/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef DISK_FORMATTER_H_
#define DISK_FORMATTER_H_

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <system_error>
#include <optional>

namespace rpi_imager {

// Error types for disk formatting operations
enum class FormatError {
  kFileOpenError,
  kFileWriteError,
  kFileSeekError,
  kInvalidParameters,
  kInsufficientSpace
};

// Result type for operations that can fail
template<typename T>
class Result {
 public:
  Result(T value) : value_(std::move(value)), error_() {}
  explicit Result(FormatError error) : value_(), error_(error) {}
  
  bool has_value() const { return !error_.has_value(); }
  explicit operator bool() const { return has_value(); }
  
  const T& value() const { return value_.value(); }
  T& value() { return value_.value(); }
  FormatError error() const { return error_.value_or(FormatError::kFileOpenError); }
  
 private:
  std::optional<T> value_;
  std::optional<FormatError> error_;
};

// Specialization for void
template<>
class Result<void> {
 public:
  Result() : error_() {}
  explicit Result(FormatError error) : error_(error) {}
  
  bool has_value() const { return !error_.has_value(); }
  explicit operator bool() const { return has_value(); }
  
  FormatError error() const { return error_.value_or(FormatError::kFileOpenError); }
  
 private:
  std::optional<FormatError> error_;
};

// Configuration for FAT32 formatting
struct Fat32Config {
  std::uint32_t total_sectors;
  std::uint32_t sectors_per_cluster = 8;  // 4KB clusters
  std::uint16_t reserved_sectors = 32;
  std::uint8_t num_fats = 2;
  std::string volume_label = "SDCARD";
  std::uint32_t volume_id = 0x12345678;
};

// MBR partition entry
struct __attribute__((packed)) MbrPartitionEntry {
  std::uint8_t status;
  std::uint8_t first_head;
  std::uint8_t first_sector;
  std::uint8_t first_cylinder;
  std::uint8_t partition_type;
  std::uint8_t last_head;
  std::uint8_t last_sector;
  std::uint8_t last_cylinder;
  std::uint32_t first_lba;
  std::uint32_t num_sectors;
};

// FAT32 boot sector structure
struct __attribute__((packed)) Fat32BootSector {
  std::array<std::uint8_t, 3> jump_instruction;
  std::array<char, 8> oem_name;
  std::uint16_t bytes_per_sector;
  std::uint8_t sectors_per_cluster;
  std::uint16_t reserved_sectors;
  std::uint8_t num_fats;
  std::uint16_t root_entries;  // 0 for FAT32
  std::uint16_t total_sectors_16;  // 0 for FAT32
  std::uint8_t media_descriptor;
  std::uint16_t sectors_per_fat_16;  // 0 for FAT32
  std::uint16_t sectors_per_track;
  std::uint16_t num_heads;
  std::uint32_t hidden_sectors;
  std::uint32_t total_sectors_32;
  std::uint32_t sectors_per_fat_32;
  std::uint16_t ext_flags;
  std::uint16_t fs_version;
  std::uint32_t root_cluster;
  std::uint16_t fs_info_sector;
  std::uint16_t backup_boot_sector;
  std::array<std::uint8_t, 12> reserved;
  std::uint8_t drive_number;
  std::uint8_t reserved1;
  std::uint8_t boot_signature;
  std::uint32_t volume_id;
  std::array<char, 11> volume_label;
  std::array<char, 8> fs_type;
  std::array<std::uint8_t, 420> boot_code;
  std::uint16_t signature;
};

// FSInfo sector structure
struct __attribute__((packed)) Fat32FsInfo {
  std::uint32_t lead_signature;
  std::array<std::uint8_t, 480> reserved1;
  std::uint32_t struct_signature;
  std::uint32_t free_count;
  std::uint32_t next_free;
  std::array<std::uint8_t, 12> reserved2;
  std::uint32_t trail_signature;
};

class DiskFormatter {
 public:
  DiskFormatter() = default;
  ~DiskFormatter() = default;

  // Non-copyable, movable
  DiskFormatter(const DiskFormatter&) = delete;
  DiskFormatter& operator=(const DiskFormatter&) = delete;
  DiskFormatter(DiskFormatter&&) = default;
  DiskFormatter& operator=(DiskFormatter&&) = default;

  // Format a device with MBR partition table and FAT32 filesystem
  Result<void> FormatDrive(
      const std::string& device_path, 
      std::uint64_t device_size_bytes);

  // Format to a file for testing
  Result<void> FormatFile(
      const std::string& file_path,
      std::uint64_t file_size_bytes);

 private:
  static constexpr std::uint32_t kSectorSize = 512;
  static constexpr std::uint32_t kPartitionStartSector = 8192;  // 4MB offset
  static constexpr std::uint8_t kFat32PartitionType = 0x0C;    // FAT32 LBA

  // Write MBR with single partition
  Result<void> WriteMbr(
      int fd, 
      std::uint64_t device_size_bytes) const;

  // Write FAT32 filesystem
  Result<void> WriteFat32(
      int fd,
      std::uint32_t partition_start_sector,
      std::uint32_t partition_size_sectors) const;

  // Helper functions
  Result<void> WriteBootSector(
      int fd,
      std::uint32_t offset_sectors,
      const Fat32Config& config) const;

  Result<void> WriteFsInfo(
      int fd,
      std::uint32_t offset_sectors,
      const Fat32Config& config) const;

  Result<void> WriteFatTables(
      int fd,
      std::uint32_t fat_start_sector,
      const Fat32Config& config) const;

  Result<void> WriteRootDirectory(
      int fd,
      std::uint32_t root_cluster_sector) const;

  // Utility functions
  Fat32Config CalculateFat32Config(std::uint32_t partition_size_sectors) const;
  std::uint32_t CalculateSectorsPerFat(const Fat32Config& config) const;
  
  Result<void> WriteAtOffset(
      int fd,
      std::uint64_t offset,
      const std::uint8_t* data,
      std::size_t size) const;

  template<typename T>
  Result<void> WriteStructAtOffset(
      int fd,
      std::uint64_t offset,
      const T& structure) const;
};

}  // namespace rpi_imager

#endif  // DISK_FORMATTER_H_ 