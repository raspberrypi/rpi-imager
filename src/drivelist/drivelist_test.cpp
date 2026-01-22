/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for drive enumeration.
 *
 * These tests cover:
 * - DeviceDescriptor methods (isDisplayable, hasSystemMountpoint, uniqueKey)
 * - Linux lsblk JSON parsing (when on Linux)
 * - Platform-independent filtering logic
 */

// DRIVELIST_ENABLE_TEST_API is defined via CMake compile definitions
// Include Qt first to get platform macros
#include <QtGlobal>

#include "drivelist.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace Drivelist;
using Catch::Matchers::ContainsSubstring;

// Forward declare test API functions (defined in platform implementations)
namespace Drivelist::testing {
#ifdef Q_OS_LINUX
std::vector<DeviceDescriptor> parseLinuxBlockDevices(const std::string& jsonOutput, bool embeddedMode = false);
#endif
#ifdef Q_OS_WIN
std::string windowsBusTypeToString(int busType);
bool isWindowsSystemDevice(const std::vector<std::string>& mountpoints);
#endif
}

// ============================================================================
// DeviceDescriptor Tests
// ============================================================================

TEST_CASE("DeviceDescriptor::isDisplayable filters correctly", "[drivelist][unit]")
{
    SECTION("Filters zero-size devices")
    {
        DeviceDescriptor device;
        device.size = 0;
        device.isVirtual = false;
        device.isReadOnly = false;
        device.isSystem = false;
        device.isRemovable = true;

        CHECK_FALSE(device.isDisplayable());
    }

    SECTION("Allows normal USB drive")
    {
        DeviceDescriptor device;
        device.size = 32000000000;  // 32GB
        device.isVirtual = false;
        device.isReadOnly = false;
        device.isSystem = false;
        device.isRemovable = true;
        device.isUSB = true;

        CHECK(device.isDisplayable());
    }

    SECTION("Filters read-only virtual devices")
    {
        DeviceDescriptor device;
        device.size = 1000000000;
        device.isVirtual = true;
        device.isReadOnly = true;
        device.isSystem = false;
        device.isRemovable = true;

        CHECK_FALSE(device.isDisplayable());
    }

    SECTION("Filters system virtual devices")
    {
        DeviceDescriptor device;
        device.size = 1000000000;
        device.isVirtual = true;
        device.isReadOnly = false;
        device.isSystem = true;
        device.isRemovable = true;

        CHECK_FALSE(device.isDisplayable());
    }

    SECTION("Filters non-removable virtual devices")
    {
        DeviceDescriptor device;
        device.size = 1000000000;
        device.isVirtual = true;
        device.isReadOnly = false;
        device.isSystem = false;
        device.isRemovable = false;

        CHECK_FALSE(device.isDisplayable());
    }

    SECTION("Allows writable removable virtual devices (disk images)")
    {
        DeviceDescriptor device;
        device.size = 1000000000;
        device.isVirtual = true;
        device.isReadOnly = false;
        device.isSystem = false;
        device.isRemovable = true;

        CHECK(device.isDisplayable());
    }
}

TEST_CASE("DeviceDescriptor::hasSystemMountpoint", "[drivelist][unit]")
{
    SECTION("Detects root mount")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/", "/home/user"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Detects Windows system drive")
    {
        DeviceDescriptor device;
        device.mountpoints = {"C:\\"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Detects /usr mount")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/usr"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Detects /var mount")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/var"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Detects /boot mount")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/boot"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Detects snap mounts")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/snap/core/12345"};
        CHECK(device.hasSystemMountpoint());
    }

    SECTION("Allows /media mounts")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/media/user/USB_DRIVE"};
        CHECK_FALSE(device.hasSystemMountpoint());
    }

    SECTION("Allows /mnt mounts")
    {
        DeviceDescriptor device;
        device.mountpoints = {"/mnt/external"};
        CHECK_FALSE(device.hasSystemMountpoint());
    }

    SECTION("Allows Windows data drives")
    {
        DeviceDescriptor device;
        device.mountpoints = {"D:\\", "E:\\"};
        CHECK_FALSE(device.hasSystemMountpoint());
    }
}

TEST_CASE("DeviceDescriptor::uniqueKey", "[drivelist][unit]")
{
    SECTION("Generates key from device and size")
    {
        DeviceDescriptor device;
        device.device = "/dev/sda";
        device.size = 32000000000;
        device.isReadOnly = false;

        CHECK(device.uniqueKey() == "/dev/sda:32000000000");
    }

    SECTION("Appends 'ro' for read-only devices")
    {
        DeviceDescriptor device;
        device.device = "/dev/sdb";
        device.size = 8000000000;
        device.isReadOnly = true;

        CHECK(device.uniqueKey() == "/dev/sdb:8000000000ro");
    }

    SECTION("Windows device path works")
    {
        DeviceDescriptor device;
        device.device = "\\\\.\\PhysicalDrive1";
        device.size = 16000000000;
        device.isReadOnly = false;

        CHECK(device.uniqueKey() == "\\\\.\\PhysicalDrive1:16000000000");
    }
}

// ============================================================================
// Linux-specific Tests
// ============================================================================

#ifdef Q_OS_LINUX

TEST_CASE("Linux lsblk parsing", "[drivelist][linux][unit]")
{
    using namespace Drivelist::testing;

    SECTION("Parses USB drive correctly")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/sda",
                "type": "disk",
                "subsystems": "block:scsi:usb:pci",
                "ro": false,
                "rm": true,
                "hotplug": true,
                "size": "32010928128",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "SanDisk ",
                "model": "Cruzer Blade    ",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].device == "/dev/sda");
        CHECK(devices[0].size == 32010928128);
        CHECK(devices[0].isUSB == true);
        CHECK(devices[0].isRemovable == true);
        CHECK(devices[0].isSystem == false);
        CHECK(devices[0].isVirtual == false);
        CHECK(devices[0].blockSize == 512);
    }

    SECTION("Parses SD card in internal reader")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/mmcblk0",
                "type": "disk",
                "subsystems": "block:mmc:mmc_host:pci",
                "ro": false,
                "rm": false,
                "hotplug": false,
                "size": "31914983424",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].device == "/dev/mmcblk0");
        CHECK(devices[0].isCard == true);
        CHECK(devices[0].isRemovable == true);  // Forced for MMC
        CHECK(devices[0].isSystem == false);    // Cards are never system
        CHECK(devices[0].isVirtual == false);
    }

    SECTION("Marks NVMe as system in non-embedded mode")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/nvme0n1",
                "type": "disk",
                "subsystems": "block:nvme:pci",
                "ro": false,
                "rm": false,
                "hotplug": false,
                "size": "500107862016",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "Samsung SSD 970 EVO Plus",
                "mountpoint": "/"
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].device == "/dev/nvme0n1");
        CHECK(devices[0].isSystem == true);
    }

    SECTION("Filters out CD/DVD drives")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/sr0",
                "type": "rom",
                "subsystems": "block:scsi:pci",
                "ro": true,
                "rm": true,
                "hotplug": false,
                "size": "1073741312",
                "phy-sec": 2048,
                "log-sec": 2048,
                "label": "",
                "vendor": "VBOX    ",
                "model": "CD-ROM          ",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        CHECK(devices.empty());
    }

    SECTION("Filters out RAM devices")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/ram0",
                "type": "disk",
                "subsystems": "block",
                "ro": false,
                "rm": false,
                "hotplug": false,
                "size": "4194304",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        CHECK(devices.empty());
    }

    SECTION("Filters out zram devices")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/zram0",
                "type": "disk",
                "subsystems": "block",
                "ro": false,
                "rm": false,
                "hotplug": false,
                "size": "4294967296",
                "phy-sec": 4096,
                "log-sec": 4096,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": "[SWAP]"
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        CHECK(devices.empty());
    }

    SECTION("Filters out eMMC boot partitions")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/mmcblk0boot0",
                "type": "disk",
                "subsystems": "block:mmc:mmc_host:pci",
                "ro": true,
                "rm": false,
                "hotplug": false,
                "size": "4194304",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        CHECK(devices.empty());
    }

    SECTION("Marks loop devices as virtual")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/loop0",
                "type": "loop",
                "subsystems": "block",
                "ro": false,
                "rm": true,
                "hotplug": false,
                "size": "1073741824",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": "/mnt/image"
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].isVirtual == true);
        CHECK(devices[0].isRemovable == true);
    }

    SECTION("Handles lsblk returning size as string vs number")
    {
        // Some lsblk versions return size as string, others as number
        const std::string jsonString = R"({
            "blockdevices": [{
                "kname": "/dev/sda",
                "type": "disk",
                "subsystems": "block:scsi:usb:pci",
                "ro": "0",
                "rm": "1",
                "hotplug": "1",
                "size": "32010928128",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "Test",
                "model": "Drive",
                "mountpoint": null
            }]
        })";

        auto devices = parseLinuxBlockDevices(jsonString, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].size == 32010928128);
        CHECK(devices[0].isReadOnly == false);
        CHECK(devices[0].isRemovable == true);
    }

    SECTION("Collects mountpoints from children")
    {
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/sda",
                "type": "disk",
                "subsystems": "block:scsi:usb:pci",
                "ro": false,
                "rm": true,
                "hotplug": true,
                "size": "32010928128",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "SanDisk",
                "model": "Cruzer",
                "mountpoint": null,
                "children": [
                    {
                        "kname": "/dev/sda1",
                        "type": "part",
                        "label": "boot",
                        "mountpoint": "/media/user/boot"
                    },
                    {
                        "kname": "/dev/sda2",
                        "type": "part",
                        "label": "rootfs",
                        "mountpoint": "/media/user/rootfs"
                    }
                ]
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        REQUIRE(devices[0].mountpoints.size() == 2);
        CHECK(devices[0].mountpoints[0] == "/media/user/boot");
        CHECK(devices[0].mountpoints[1] == "/media/user/rootfs");
    }
}

#endif // Q_OS_LINUX

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("ListStorageDevices returns valid data", "[drivelist][integration]")
{
    auto devices = ListStorageDevices();

    // Should find at least one storage device on any system
    // (the system disk at minimum)
    REQUIRE(devices.size() >= 1);

    for (const auto& device : devices)
    {
        // Basic sanity checks
        INFO("Checking device: " << device.device);
        CHECK_FALSE(device.device.empty());
        CHECK(device.size > 0);
        CHECK(device.blockSize > 0);
        CHECK(device.logicalBlockSize > 0);

        // Block sizes should be powers of 2 and reasonable
        CHECK(device.blockSize >= 512);
        CHECK(device.blockSize <= 4096);
        CHECK(device.logicalBlockSize >= 512);
        CHECK(device.logicalBlockSize <= device.blockSize);

        // Consistency checks
        if (device.isCard) {
            // SD cards can be connected via USB adapter, so isUSB might be true
            // But they should be marked as removable
            CHECK(device.isRemovable);
        }

        // Virtual devices should not be SCSI/USB physical connections
        if (device.isVirtual) {
            CHECK_FALSE(device.isUSB);
            CHECK_FALSE(device.isCard);
        }
    }
}

TEST_CASE("System drive detection works", "[drivelist][integration]")
{
    auto devices = ListStorageDevices();

    // Find the system drive (should have root mountpoint)
    const DeviceDescriptor* systemDrive = nullptr;
    for (const auto& device : devices) {
        if (device.hasSystemMountpoint()) {
            systemDrive = &device;
            break;
        }
    }

    // We should have found the system drive
    REQUIRE(systemDrive != nullptr);

    INFO("System drive: " << systemDrive->device);
    
    // System drives should be marked as such
    // Note: isDisplayable() only filters obviously bad devices (zero-size,
    // read-only virtual, etc.). The actual "should we show this to the user"
    // logic is in DriveListModel, which additionally filters based on
    // mountpoints containing "/" or "C:\\"
    CHECK(systemDrive->isSystem);
    CHECK(systemDrive->hasSystemMountpoint());
}
