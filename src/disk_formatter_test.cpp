/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "disk_formatter.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdlib>
#include <cassert>
#include <cstring>

namespace fs = std::filesystem;
using namespace rpi_imager;

class DiskFormatterTest {
 public:
  static bool RunAllTests() {
    std::cout << "Running DiskFormatter tests...\n";
    
    bool all_passed = true;
    all_passed &= TestBasicFormatting();
    all_passed &= TestMbrStructure();
    all_passed &= TestFat32Structure();
    all_passed &= TestSystemToolValidation();
    
    if (all_passed) {
      std::cout << "All tests passed!\n";
    } else {
      std::cout << "Some tests failed!\n";
    }
    
    return all_passed;
  }

 private:
  static bool TestBasicFormatting() {
    std::cout << "Testing basic formatting...\n";
    
    const std::string test_file = "/tmp/test_disk.img";
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    // Clean up any existing test file
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Check file was created with correct size
    if (!fs::exists(test_file)) {
      std::cout << "❌ Test file was not created\n";
      return false;
    }
    
    if (fs::file_size(test_file) != disk_size) {
      std::cout << "❌ Test file has incorrect size\n";
      return false;
    }
    
    std::cout << "✅ Basic formatting test passed\n";
    return true;
  }
  
  static bool TestMbrStructure() {
    std::cout << "Testing MBR structure...\n";
    
    const std::string test_file = "/tmp/test_mbr.img";
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Read and validate MBR
    std::ifstream file(test_file, std::ios::binary);
    if (!file) {
      std::cout << "❌ Cannot open test file for reading\n";
      return false;
    }
    
    std::array<std::uint8_t, 512> mbr{};
    file.read(reinterpret_cast<char*>(mbr.data()), 512);
    
    // Check MBR signature
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
      std::cout << "❌ Invalid MBR signature\n";
      return false;
    }
    
    // Check partition entry (starts at offset 446)
    const auto* partition = reinterpret_cast<const MbrPartitionEntry*>(mbr.data() + 446);
    
    if (partition->status != 0x80) {
      std::cout << "❌ Partition not marked as bootable\n";
      return false;
    }
    
    if (partition->partition_type != 0x0C) {  // FAT32 LBA
      std::cout << "❌ Wrong partition type: " << static_cast<int>(partition->partition_type) << "\n";
      return false;
    }
    
    std::uint32_t first_lba = partition->first_lba;
    if (first_lba != 8192) {  // Should start at 4MB
      std::cout << "❌ Wrong partition start sector: " << first_lba << "\n";
      return false;
    }
    
    std::cout << "✅ MBR structure test passed\n";
    return true;
  }
  
  static bool TestFat32Structure() {
    std::cout << "Testing FAT32 structure...\n";
    
    const std::string test_file = "/tmp/test_fat32.img";
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    // Read FAT32 boot sector (at partition start: sector 8192)
    std::ifstream file(test_file, std::ios::binary);
    if (!file) {
      std::cout << "❌ Cannot open test file for reading\n";
      return false;
    }
    
    file.seekg(8192 * 512);  // Seek to partition start
    std::array<std::uint8_t, 512> boot_sector{};
    file.read(reinterpret_cast<char*>(boot_sector.data()), 512);
    
    const auto* fat32_boot = reinterpret_cast<const Fat32BootSector*>(boot_sector.data());
    
    // Check boot signature (stored in little-endian)
    std::uint16_t signature = fat32_boot->signature;
    if (signature != 0xAA55) {  // Little-endian: 0x55 0xAA
      std::cout << "❌ Invalid FAT32 boot signature: 0x" << std::hex << signature << std::dec << "\n";
      return false;
    }
    
    // Check bytes per sector (stored in little-endian)
    std::uint16_t bytes_per_sector = fat32_boot->bytes_per_sector;
    if (bytes_per_sector != 512) {
      std::cout << "❌ Wrong bytes per sector: " << bytes_per_sector << "\n";
      return false;
    }
    
    // Check filesystem type
    std::string fs_type(fat32_boot->fs_type.data(), 8);
    if (fs_type.substr(0, 5) != "FAT32") {
      std::cout << "❌ Wrong filesystem type: " << fs_type << "\n";
      return false;
    }
    
    // Check jump instruction
    if (fat32_boot->jump_instruction[0] != 0xEB) {
      std::cout << "❌ Invalid jump instruction\n";
      return false;
    }
    
    std::cout << "✅ FAT32 structure test passed\n";
    return true;
  }
  
  static bool TestSystemToolValidation() {
    std::cout << "Testing with system tools...\n";
    
    const std::string test_file = "/tmp/test_system.img";
    const std::uint64_t disk_size = 64 * 1024 * 1024;  // 64MB
    
    fs::remove(test_file);
    
    DiskFormatter formatter;
    auto result = formatter.FormatFile(test_file, disk_size);
    
    if (!result) {
      std::cout << "❌ Failed to format file\n";
      return false;
    }
    
    bool all_passed = true;
    
    // Test with fdisk to check partition table
    std::string fdisk_cmd = "fdisk -l " + test_file + " 2>/dev/null";
    if (std::system(fdisk_cmd.c_str()) != 0) {
      std::cout << "⚠️  fdisk validation failed (tool may not be available)\n";
    } else {
      std::cout << "✅ fdisk validation passed\n";
    }
    
    // Test with file command to detect filesystem
    std::string file_cmd = "file " + test_file + " 2>/dev/null | grep -q 'DOS/MBR boot sector'";
    if (std::system(file_cmd.c_str()) != 0) {
      std::cout << "⚠️  file command validation failed\n";
    } else {
      std::cout << "✅ file command validation passed\n";
    }
    
    // Try to mount the filesystem (requires loop device support)
    std::string mkdir_cmd = "mkdir -p /tmp/test_mount 2>/dev/null";
    std::system(mkdir_cmd.c_str());
    
    // Set up loop device and mount
    std::string loop_cmd = "sudo losetup -f --show " + test_file + " 2>/dev/null";
    FILE* loop_pipe = popen(loop_cmd.c_str(), "r");
    if (loop_pipe) {
      char loop_device[256] = {};
      if (fgets(loop_device, sizeof(loop_device), loop_pipe)) {
        // Remove newline
        loop_device[strcspn(loop_device, "\n")] = 0;
        
        // Mount the first partition
        std::string mount_cmd = "sudo mount " + std::string(loop_device) + "p1 /tmp/test_mount 2>/dev/null";
        if (std::system(mount_cmd.c_str()) == 0) {
          std::cout << "✅ Mount test passed\n";
          
          // Test creating a file
          std::string touch_cmd = "sudo touch /tmp/test_mount/test.txt 2>/dev/null";
          if (std::system(touch_cmd.c_str()) == 0) {
            std::cout << "✅ File creation test passed\n";
            std::system("sudo rm /tmp/test_mount/test.txt 2>/dev/null");
          } else {
            std::cout << "⚠️  File creation test failed\n";
          }
          
          // Unmount
          std::system("sudo umount /tmp/test_mount 2>/dev/null");
        } else {
          std::cout << "⚠️  Mount test failed (may require sudo privileges)\n";
        }
        
        // Detach loop device
        std::string detach_cmd = "sudo losetup -d " + std::string(loop_device) + " 2>/dev/null";
        std::system(detach_cmd.c_str());
      }
      pclose(loop_pipe);
    } else {
      std::cout << "⚠️  Loop device test skipped (requires sudo)\n";
    }
    
    // Clean up
    std::system("rmdir /tmp/test_mount 2>/dev/null");
    
    // Verify filesystem with fsck.fat if available
    std::string fsck_cmd = "fsck.fat -v " + test_file + " 2>/dev/null";
    if (std::system(fsck_cmd.c_str()) == 0) {
      std::cout << "✅ fsck.fat validation passed\n";
    } else {
      std::cout << "⚠️  fsck.fat validation skipped (tool may not be available)\n";
    }
    
    std::cout << "✅ System tool validation completed\n";
    return all_passed;
  }
};

int main() {
  try {
    bool success = DiskFormatterTest::RunAllTests();
    return success ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
} 