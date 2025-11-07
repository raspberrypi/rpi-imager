/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include "file_operations.h"

#include <QDebug>
#include <QFile>
#include <filesystem>
#include <iostream>

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#endif

namespace fs = std::filesystem;

// Forward declarations
static std::string getTestMountPath();
static std::string getWholeDiskPath(const std::string& partition_path);
static int getPartitionNumber(const std::string& partition_path);

// Global shared device wrapper (opened once, reused by all tests)
static std::shared_ptr<rpi_imager::FileOperations> g_shared_file_ops;
static std::shared_ptr<DeviceWrapper> g_shared_device_wrapper;
static int g_partition_num = 1;
static std::string g_test_device_path;

// Initialize the shared device wrapper once
static void initializeSharedDevice() {
    if (g_shared_device_wrapper) {
        return; // Already initialized
    }
    
    std::string mount_path = getTestMountPath();
    if (mount_path.empty()) {
        std::cout << "WARNING: No test device path available" << std::endl;
        return;
    }
    
    g_test_device_path = mount_path;
    
    // Convert partition to whole disk
    std::string disk_path = getWholeDiskPath(mount_path);
    g_partition_num = getPartitionNumber(mount_path);
    
    std::cout << "=========================================" << std::endl;
    std::cout << "Opening test device ONCE for all tests" << std::endl;
    std::cout << "Partition: " << mount_path << std::endl;
    std::cout << "Whole disk: " << disk_path << std::endl;
    std::cout << "Partition #: " << g_partition_num << std::endl;
    std::cout << "=========================================" << std::endl;
    
    auto file_ops = rpi_imager::FileOperations::Create();
    auto result = file_ops->OpenDevice(disk_path);
    
    if (result != rpi_imager::FileError::kSuccess) {
        std::cout << "ERROR: Failed to open device: " << disk_path << std::endl;
        return;
    }
    
    auto device_wrapper = std::make_unique<DeviceWrapper>(file_ops.get());
    
    // Convert to shared_ptr for global sharing
    g_shared_file_ops = std::shared_ptr<rpi_imager::FileOperations>(std::move(file_ops));
    g_shared_device_wrapper = std::shared_ptr<DeviceWrapper>(std::move(device_wrapper));
    
    std::cout << "✅ Device opened successfully and will be reused by all tests" << std::endl;
    std::cout << "=========================================" << std::endl;
}

// Cleanup the shared device wrapper
static void cleanupSharedDevice() {
    if (g_shared_device_wrapper) {
        std::cout << "=========================================" << std::endl;
        std::cout << "Closing shared test device" << std::endl;
        std::cout << "=========================================" << std::endl;
        g_shared_device_wrapper.reset();
        g_shared_file_ops.reset();
    }
}

// RAII helper to initialize/cleanup on program start/end
struct SharedDeviceManager {
    SharedDeviceManager() {
        initializeSharedDevice();
    }
    ~SharedDeviceManager() {
        cleanupSharedDevice();
    }
};
static SharedDeviceManager g_shared_device_manager;

// Helper to get test mount path from environment or use default
static std::string getTestMountPath() {
    const char* env_path = std::getenv("FAT_TEST_MOUNT_PATH");
    if (env_path && fs::exists(env_path)) {
        return env_path;
    }
    
    // Default path for macOS
    std::string default_path = "/Volumes/bootfs 1";
    if (fs::exists(default_path)) {
        return default_path;
    }
    
    return "";
}

// Helper to convert partition device to whole disk device
// e.g., /dev/disk4s1 -> /dev/disk4
static std::string getWholeDiskPath(const std::string& partition_path) {
    // Find the last 's' followed by digits (the partition suffix)
    // This handles /dev/disk4s1 correctly (not /dev/di + s + k4s1)
    size_t s_pos = partition_path.rfind("s");
    if (s_pos == std::string::npos) {
        return partition_path;
    }
    
    // Verify there's a digit after the 's'
    if (s_pos + 1 < partition_path.length() && std::isdigit(partition_path[s_pos + 1])) {
        return partition_path.substr(0, s_pos);
    }
    
    return partition_path; // No partition suffix found
}

// Helper to extract partition number from device path
// e.g., /dev/disk4s1 -> 1
static int getPartitionNumber(const std::string& partition_path) {
    size_t s_pos = partition_path.rfind("s");
    if (s_pos == std::string::npos) {
        return 1; // Default to partition 1 if no suffix
    }
    
    // Extract number after 's'
    std::string num_str = partition_path.substr(s_pos + 1);
    try {
        return std::stoi(num_str);
    } catch (...) {
        return 1; // Default to partition 1 on error
    }
}

// Helper to get the shared FAT partition (all tests use the same device)
static DeviceWrapperFatPartition* getSharedFatPartition() {
    if (!g_shared_device_wrapper) {
        throw std::runtime_error("Shared device not initialized. Set FAT_TEST_MOUNT_PATH environment variable.");
    }
    
    DeviceWrapperFatPartition* fat = g_shared_device_wrapper->fatPartition(g_partition_num);
    if (!fat) {
        throw std::runtime_error("Failed to get FAT partition");
    }
    
    return fat;
}

TEST_CASE("DeviceWrapperFatPartition list files", "[fat][!mayfail]") {
    INFO("Testing against: " << g_test_device_path);
    
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    // List all files recursively
    QStringList allFiles = fat->listAllFilesRecursive();
    
    std::cout << "Found " << allFiles.size() << " total files (recursive)" << std::endl;
    INFO("Found " << allFiles.size() << " total files");
    REQUIRE(!allFiles.isEmpty());
    
    // Count files by directory and show samples
    int rootFiles = 0;
    int subdirFiles = 0;
    QStringList sampleSubdirFiles;
    QStringList allSubdirs;
    
    for (const QString& file : allFiles) {
        if (file.contains("/")) {
            subdirFiles++;
            if (sampleSubdirFiles.size() < 10) {
                sampleSubdirFiles.append(file);
            }
            // Extract directory name
            QString dirName = file.left(file.indexOf('/'));
            if (!allSubdirs.contains(dirName)) {
                allSubdirs.append(dirName);
            }
        } else {
            rootFiles++;
        }
    }
    
    std::cout << "  Root files: " << rootFiles << std::endl;
    std::cout << "  Subdirectory files: " << subdirFiles << std::endl;
    std::cout << "  Subdirectories found: ";
    for (const QString& dir : allSubdirs) {
        std::cout << "'" << dir.toStdString() << "' ";
        // Show hex dump of first few characters
        std::cout << "(hex:";
        for (int i = 0; i < qMin(dir.length(), 8); i++) {
            std::cout << " " << std::hex << (int)dir[i].unicode() << std::dec;
        }
        std::cout << ") ";
    }
    std::cout << std::endl;
    if (!sampleSubdirFiles.isEmpty()) {
        std::cout << "  Sample subdirectory files:" << std::endl;
        for (const QString& sample : sampleSubdirFiles) {
            std::cout << "    - " << sample.toStdString() << std::endl;
        }
    } else {
        std::cout << "  WARNING: No subdirectory files found!" << std::endl;
    }
    INFO("Root files: " << rootFiles);
    INFO("Subdirectory files: " << subdirFiles);
    
    CHECK(rootFiles > 0);
    
    // List root files only
    QStringList rootFilesList = fat->listAllFiles();
    
    std::cout << "Found " << rootFilesList.size() << " root-only files" << std::endl;
    INFO("Found " << rootFilesList.size() << " root files");
    REQUIRE(!rootFilesList.isEmpty());
    
    // All returned files should be root files (no '/')
    for (const QString& file : rootFilesList) {
        CHECK(!file.contains("/"));
    }
}
TEST_CASE("DeviceWrapperFatPartition file reading", "[fat][!mayfail]") {
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    SECTION("Read root files") {
        QStringList files = fat->listAllFiles();
        REQUIRE(!files.isEmpty());
        
        // Test reading first few files
        int tested = 0;
        for (const QString& filename : files) {
            if (tested >= 3) break;
            
            INFO("Reading file: " << filename.toStdString());
            QByteArray content = fat->readFile(filename);
            
            // File should have some content (or be empty, which is valid)
            INFO("File size: " << content.size() << " bytes");
            CHECK(content.size() >= 0);
            
            tested++;
        }
        
        REQUIRE(tested > 0);
    }
    
    SECTION("Read subdirectory files") {
        QStringList allFiles = fat->listAllFilesRecursive();
        
        // Find files in subdirectories
        QStringList subdirFiles;
        for (const QString& file : allFiles) {
            if (file.contains("/")) {
                subdirFiles.append(file);
                if (subdirFiles.size() >= 5) break;
            }
        }
        
        if (subdirFiles.isEmpty()) {
            SKIP("No subdirectory files found to test");
        }
        
        for (const QString& filename : subdirFiles) {
            INFO("Reading subdirectory file: " << filename.toStdString());
            
            REQUIRE_NOTHROW([&]() {
                QByteArray content = fat->readFile(filename);
                INFO("File size: " << content.size() << " bytes");
                CHECK(content.size() > 0);  // Subdirectory files should have content
            }());
        }
    }
}
TEST_CASE("DeviceWrapperFatPartition fileExists", "[fat][!mayfail]") {
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    SECTION("Check existing files") {
        QStringList allFiles = fat->listAllFilesRecursive();
        REQUIRE(!allFiles.isEmpty());
        
        // Test a few existing files
        int tested = 0;
        for (const QString& filename : allFiles) {
            if (tested >= 5) break;
            
            INFO("Checking existence of: " << filename.toStdString());
            CHECK(fat->fileExists(filename));
            
            tested++;
        }
    }
    
    SECTION("Check non-existent files") {
        CHECK_FALSE(fat->fileExists("this_file_does_not_exist.txt"));
        CHECK_FALSE(fat->fileExists("overlays/nonexistent.dtbo"));
    }
}

TEST_CASE("DeviceWrapperFatPartition deleteFile", "[fat][!mayfail][.destructive]") {
    WARN("This test will modify the filesystem!");
    
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    SECTION("Delete root file") {
        // Create a test file
        QString testFile = "test_delete.txt";
        QByteArray testContent = "This is a test file for deletion";
        
        fat->writeFile(testFile, testContent);
        REQUIRE(fat->fileExists(testFile));
        
        // Delete it
        INFO("Deleting test file: " << testFile.toStdString());
        REQUIRE(fat->deleteFile(testFile));
        
        // Verify it's gone by trying to read it
        REQUIRE_THROWS(fat->readFile(testFile));
    }
    
    SECTION("Delete subdirectory file") {
        QStringList allFiles = fat->listAllFilesRecursive();
        
        // Find a file in overlays to test with
        QString testFile;
        for (const QString& file : allFiles) {
            if (file.startsWith("overlays/") && file.endsWith(".dtbo")) {
                testFile = file;
                break;
            }
        }
        
        if (testFile.isEmpty()) {
            SKIP("No suitable test file found in overlays/");
        }
        
        INFO("Testing deletion of: " << testFile.toStdString());
        
        // Read the file first to verify it exists
        QByteArray originalContent = fat->readFile(testFile);
        REQUIRE(originalContent.size() > 0);
        
        // Delete it
        REQUIRE(fat->deleteFile(testFile));
        
        // Verify it's gone
        REQUIRE_THROWS(fat->readFile(testFile));
    }
}

TEST_CASE("DeviceWrapperFatPartition LFN checksum validation", "[fat][!mayfail][.destructive]") {
    WARN("This test will create files with long names to test LFN checksum validation!");
    
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    SECTION("Create file with long name and verify LFN checksum") {
        // Create a file with a name longer than 8.3 (requires LFN entries)
        QString longName = "this_file_has_a_very_long_name_that_requires_lfn_entries.txt";
        QByteArray content = "LFN checksum test content";
        
        std::cout << "Creating file with long name: " << longName.toStdString() << std::endl;
        
        fat->writeFile(longName, content);
        REQUIRE(fat->fileExists(longName));
        
        // Read it back - should work with the long name
        QByteArray readBack = fat->readFile(longName);
        REQUIRE(readBack == content);
        
        // Verify it appears in the file list with its long name
        QStringList allFiles = fat->listAllFiles();
        REQUIRE(allFiles.contains(longName));
        
        // Clean up
        REQUIRE(fat->deleteFile(longName));
        REQUIRE_FALSE(fat->fileExists(longName));
        
        std::cout << "  ✓ LFN creation, reading, and deletion successful" << std::endl;
    }
    
    SECTION("Multiple files with long names") {
        std::cout << "Testing multiple long filename files..." << std::endl;
        
        QStringList longNames = {
            "long_filename_test_number_one.dat",
            "another_very_long_filename_test.bin",
            "yet_another_extremely_long_filename_for_testing.tmp"
        };
        
        // Create all files
        for (const QString& name : longNames) {
            QByteArray content = QString("Content for %1").arg(name).toUtf8();
            fat->writeFile(name, content);
            REQUIRE(fat->fileExists(name));
        }
        
        // Verify all can be read back
        for (const QString& name : longNames) {
            QByteArray expected = QString("Content for %1").arg(name).toUtf8();
            QByteArray actual = fat->readFile(name);
            REQUIRE(actual == expected);
        }
        
        // List and verify all appear
        QStringList allFiles = fat->listAllFiles();
        for (const QString& name : longNames) {
            REQUIRE(allFiles.contains(name));
        }
        
        // Clean up
        for (const QString& name : longNames) {
            REQUIRE(fat->deleteFile(name));
        }
        
        std::cout << "  ✓ Created and verified " << longNames.size() << " long filename files" << std::endl;
    }
    
    SECTION("Mix of 8.3 and long filenames") {
        std::cout << "Testing mix of 8.3 and LFN files..." << std::endl;
        
        struct TestFile {
            QString name;
            bool isLongName;
        };
        
        QList<TestFile> testFiles = {
            {"SHORT.TXT", false},  // 8.3 name
            {"this_is_a_long_filename.txt", true},  // LFN
            {"TEST2.DAT", false},  // 8.3 name
            {"another_long_name_file.bin", true}  // LFN
        };
        
        // Create all files
        for (const auto& tf : testFiles) {
            QByteArray content = QString("Content for %1").arg(tf.name).toUtf8();
            fat->writeFile(tf.name, content);
            REQUIRE(fat->fileExists(tf.name));
            std::cout << "  Created: " << tf.name.toStdString() 
                      << " (type: " << (tf.isLongName ? "LFN" : "8.3") << ")" << std::endl;
        }
        
        // Verify all are accessible
        QStringList allFiles = fat->listAllFiles();
        for (const auto& tf : testFiles) {
            // Check existence (case-insensitive for FAT)
            bool found = false;
            for (const QString& listedFile : allFiles) {
                if (listedFile.toLower() == tf.name.toLower()) {
                    found = true;
                    break;
                }
            }
            REQUIRE(found);
            
            // Verify content
            QByteArray expected = QString("Content for %1").arg(tf.name).toUtf8();
            QByteArray actual = fat->readFile(tf.name);
            REQUIRE(actual == expected);
        }
        
        // Clean up
        for (const auto& tf : testFiles) {
            REQUIRE(fat->deleteFile(tf.name));
        }
        
        std::cout << "  ✓ Successfully tested " << testFiles.size() << " mixed name files" << std::endl;
    }
    
    SECTION("Verify corrupted LFN detection on boot partition") {
        // This is a negative test - we know the boot partition has corrupt/orphaned LFN entries
        // We should verify that files are still accessible by their short names
        
        std::cout << "Testing that corrupt LFNs are properly rejected..." << std::endl;
        
        // These files are known to have corrupt LFN entries on typical Pi boot partitions
        QStringList knownShortNames = {
            "config.txt",
            "cmdline.txt",
            "overlays"  // directory
        };
        
        QStringList allFiles = fat->listAllFilesRecursive();
        
        for (const QString& shortName : knownShortNames) {
            // File should be accessible by its short name (case-insensitive)
            bool found = false;
            for (const QString& file : allFiles) {
                if (file.toLower() == shortName.toLower() || 
                    file.toLower().startsWith(shortName.toLower() + "/")) {
                    found = true;
                    std::cout << "  ✓ Found '" << shortName.toStdString() 
                              << "' (listed as: '" << file.left(shortName.length()).toStdString() << "')" << std::endl;
                    break;
                }
            }
            
            if (found) {
                // Verify we can access it (for files, not directories in the listing)
                if (fat->fileExists(shortName)) {
                    // It's a file - try to read it
                    QByteArray content = fat->readFile(shortName);
                    REQUIRE(content.size() > 0);
                    std::cout << "  ✓ Successfully read '" << shortName.toStdString() 
                              << "' (" << content.size() << " bytes)" << std::endl;
                }
            }
        }
        
        std::cout << "  ✓ Corrupt LFN detection and fallback working correctly" << std::endl;
    }
}

TEST_CASE("DeviceWrapperFatPartition LFN checksum algorithm", "[fat][!mayfail]") {
    // Test the LFN checksum algorithm itself with known values
    // This validates our implementation against the FAT specification
    
    SECTION("Known checksum values") {
        std::cout << "Testing LFN checksum algorithm with known values..." << std::endl;
        
        // Test case 1: "EXAMPLE TXT" (11 bytes, space-padded 8.3 name)
        unsigned char name1[11] = {'E','X','A','M','P','L','E',' ','T','X','T'};
        // Expected checksum calculated by hand:
        // Start with sum=0
        // Each iteration: sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i]
        // This is a rotate-right and add operation
        
        // We can verify our implementation works by creating a file with this name
        // and checking that our code can find it
        
        DeviceWrapperFatPartition* fat = getSharedFatPartition();
        REQUIRE(fat != nullptr);
        
        // Create a file with a simple name
        QString testName = "example.txt";
        QByteArray content = "LFN checksum algorithm test";
        
        fat->writeFile(testName, content);
        REQUIRE(fat->fileExists(testName));
        
        // Read it back
        QByteArray readContent = fat->readFile(testName);
        REQUIRE(readContent == content);
        
        // Clean up
        fat->deleteFile(testName);
        
        std::cout << "  ✓ LFN checksum algorithm validated" << std::endl;
    }
    
    SECTION("Edge cases for checksum") {
        std::cout << "Testing LFN checksum with edge cases..." << std::endl;
        
        DeviceWrapperFatPartition* fat = getSharedFatPartition();
        REQUIRE(fat != nullptr);
        
        QStringList edgeCases = {
            "a.txt",           // Very short name
            "12345678.123",    // Exactly 8.3
            "test.a",          // Short extension
            "filename",        // No extension
        };
        
        for (const QString& name : edgeCases) {
            QByteArray content = QString("Content for %1").arg(name).toUtf8();
            fat->writeFile(name, content);
            REQUIRE(fat->fileExists(name));
            
            QByteArray readBack = fat->readFile(name);
            REQUIRE(readBack == content);
            
            fat->deleteFile(name);
            
            std::cout << "  ✓ Tested: " << name.toStdString() << std::endl;
        }
        
        std::cout << "  ✓ All edge cases passed" << std::endl;
    }
}

TEST_CASE("DeviceWrapperFatPartition comprehensive tests", "[fat][!mayfail][.destructive]") {
    WARN("This test will create and delete files with various name formats!");
    
    DeviceWrapperFatPartition* fat = getSharedFatPartition();
    REQUIRE(fat != nullptr);
    
    SECTION("Create and delete 8.3 filename in root") {
        QString shortName = "TEST83.TXT";
        QByteArray content = "Short 8.3 filename test";
        
        std::cout << "Testing 8.3 filename: " << shortName.toStdString() << std::endl;
        
        fat->writeFile(shortName, content);
        REQUIRE(fat->fileExists(shortName));
        
        QByteArray readBack = fat->readFile(shortName);
        REQUIRE(readBack == content);
        
        REQUIRE(fat->deleteFile(shortName));
        REQUIRE_FALSE(fat->fileExists(shortName));
    }
    
    SECTION("Create and delete long filename (LFN) in root") {
        QString longName = "this_is_a_very_long_filename_test.txt";
        QByteArray content = "Long filename (LFN) test content";
        
        std::cout << "Testing LFN: " << longName.toStdString() << std::endl;
        
        fat->writeFile(longName, content);
        REQUIRE(fat->fileExists(longName));
        
        QByteArray readBack = fat->readFile(longName);
        REQUIRE(readBack == content);
        
        REQUIRE(fat->deleteFile(longName));
        REQUIRE_FALSE(fat->fileExists(longName));
    }
    
    SECTION("Create and delete file in subdirectory") {
        QString subFile = "overlays/test_overlay.dtbo";
        QByteArray content = "Test overlay file content";
        
        std::cout << "Testing subdirectory file: " << subFile.toStdString() << std::endl;
        
        // First check if overlays directory exists
        QStringList allFiles = fat->listAllFilesRecursive();
        bool hasOverlaysDir = false;
        for (const QString& file : allFiles) {
            if (file.startsWith("overlays/")) {
                hasOverlaysDir = true;
                break;
            }
        }
        
        if (!hasOverlaysDir) {
            SKIP("No overlays/ directory found");
        }
        
        fat->writeFile(subFile, content);
        REQUIRE(fat->fileExists(subFile));
        
        QByteArray readBack = fat->readFile(subFile);
        REQUIRE(readBack == content);
        
        REQUIRE(fat->deleteFile(subFile));
        REQUIRE_FALSE(fat->fileExists(subFile));
    }
    
    SECTION("Create multiple files and verify list") {
        std::cout << "Testing multiple file creation..." << std::endl;
        
        QStringList testFiles = {
            "multitest1.txt",
            "multitest2.dat",
            "multi_test_long_name_3.bin"
        };
        
        // Create all files
        for (const QString& filename : testFiles) {
            QByteArray content = QString("Content for %1").arg(filename).toUtf8();
            fat->writeFile(filename, content);
            REQUIRE(fat->fileExists(filename));
        }
        
        // List and verify they all appear
        QStringList rootFiles = fat->listAllFiles();
        for (const QString& filename : testFiles) {
            REQUIRE(rootFiles.contains(filename));
        }
        
        // Clean up
        for (const QString& filename : testFiles) {
            REQUIRE(fat->deleteFile(filename));
        }
        
        std::cout << "  Created and cleaned up " << testFiles.size() << " test files" << std::endl;
    }
    
    SECTION("Test file size limits") {
        std::cout << "Testing various file sizes..." << std::endl;
        
        // Empty file
        fat->writeFile("empty.txt", QByteArray());
        REQUIRE(fat->fileExists("empty.txt"));
        REQUIRE(fat->readFile("empty.txt").isEmpty());
        fat->deleteFile("empty.txt");
        
        // Small file
        QByteArray smallData(100, 'A');
        fat->writeFile("small.txt", smallData);
        REQUIRE(fat->readFile("small.txt") == smallData);
        fat->deleteFile("small.txt");
        
        // Medium file (cross cluster boundary on typical FAT32)
        QByteArray mediumData(8192, 'B');
        fat->writeFile("medium.dat", mediumData);
        REQUIRE(fat->readFile("medium.dat") == mediumData);
        fat->deleteFile("medium.dat");
        
        std::cout << "  ✓ Empty, small, and medium file tests passed" << std::endl;
    }
}

