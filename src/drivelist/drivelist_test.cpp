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

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <optional>
#include "drivelist.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace Drivelist;
using Catch::Matchers::ContainsSubstring;

// Forward declare test API functions (defined in platform implementations)
namespace Drivelist::testing {
#ifdef Q_OS_LINUX
std::vector<DeviceDescriptor> parseLinuxBlockDevices(const std::string& jsonOutput, bool embeddedMode = false);
std::vector<DeviceDescriptor> devicesWhenLsblkCannotBeRun(bool embeddedMode = false);
std::optional<QByteArray> runLsblk();
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

    SECTION("Detects /boot/firmware mount")
    {
        // Where current Raspberry Pi OS mounts the firmware partition. This
        // list used to be duplicated in drivelist_linux.cpp and only that
        // copy knew about /boot/firmware, so the helper reported a running
        // Pi's own boot disk as safe to overwrite.
        DeviceDescriptor device;
        device.mountpoints = {"/boot/firmware"};
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

    // ── NVMe in embedded mode ──────────────────────────────────────────
    //
    // On a desktop, every NVMe is treated as system and hidden: nobody images
    // an SD card onto their laptop's internal SSD. On a Pi running the imager
    // itself, an attached NVMe is a perfectly ordinary target -- unless the Pi
    // booted from it, in which case offering it would let the user overwrite
    // the disk they are running from.

    auto nvmeJson = [](const std::string& children) {
        return R"({
            "blockdevices": [{
                "kname": "/dev/nvme0n1",
                "type": "disk",
                "subsystems": "block:nvme:pci",
                "ro": false, "rm": false, "hotplug": false,
                "size": "512110190592",
                "phy-sec": 512, "log-sec": 512,
                "label": "", "vendor": "", "model": "WD Blue SN570",
                "mountpoint": null)" + children + R"(
            }]
        })";
    };

    SECTION("Embedded mode offers an unmounted NVMe")
    {
        auto devices = parseLinuxBlockDevices(nvmeJson(""), true);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].isSystem == false);
    }

    SECTION("Embedded mode keeps an NVMe carrying root as a system drive")
    {
        // The Pi booted from this disk. Overwriting it destroys the running
        // system, so it must stay marked system whatever the mode.
        const std::string children = R"(,
                "children": [
                    {"kname":"/dev/nvme0n1p1","type":"part","mountpoint":"/boot/firmware"},
                    {"kname":"/dev/nvme0n1p2","type":"part","mountpoint":"/"}
                ])";
        auto devices = parseLinuxBlockDevices(nvmeJson(children), true);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].isSystem == true);
    }

    SECTION("Embedded mode offers an NVMe mounted only under /media")
    {
        // A data disk the user happens to have plugged in and mounted.
        const std::string children = R"(,
                "children": [
                    {"kname":"/dev/nvme0n1p1","type":"part","mountpoint":"/media/pi/backup"}
                ])";
        auto devices = parseLinuxBlockDevices(nvmeJson(children), true);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].isSystem == false);
    }

    SECTION("Embedded mode keeps an NVMe with a non-/media mount as system")
    {
        // Anything mounted outside /media is assumed to matter to the running
        // system -- the conservative answer when the alternative is data loss.
        const std::string children = R"(,
                "children": [
                    {"kname":"/dev/nvme0n1p1","type":"part","mountpoint":"/srv/data"}
                ])";
        auto devices = parseLinuxBlockDevices(nvmeJson(children), true);
        REQUIRE(devices.size() == 1);
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

    SECTION("Loop devices get fallback description when label/vendor/model are empty")
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
        CHECK_THAT(devices[0].description, ContainsSubstring("loop0"));
        CHECK(!devices[0].description.empty());
    }

    SECTION("Marks loop devices as virtual even with empty subsystems (lsblk bug)")
    {
        // util-linux 2.39.x (shipped in Ubuntu 24.04 LTS) has a bug where
        // the "subsystems" column intermittently returns empty for loop devices.
        // See: https://github.com/util-linux/util-linux/pull/3089
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/loop1",
                "type": "loop",
                "subsystems": "",
                "ro": false,
                "rm": true,
                "hotplug": false,
                "size": "1073741824",
                "phy-sec": 512,
                "log-sec": 512,
                "label": "",
                "vendor": "",
                "model": "",
                "mountpoint": "/snap/chromium/3352"
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].device == "/dev/loop1");
        CHECK(devices[0].isVirtual == true);
        CHECK(devices[0].isSystem == true);  // /snap/ mount makes it a system device
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

    SECTION("Marks a removable SD card carrying root as a system drive")
    {
        // The most common Raspberry Pi setup there is: booted from the SD
        // card in the built-in reader. isCard forces isRemovable, and
        // isSystem used to be derived from removability alone, so the drive
        // the machine was running from came back not-a-system-drive and the
        // confirmation shown before overwriting it never appeared.
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/mmcblk0",
                "type": "disk",
                "subsystems": "block:mmc:mmc_host:pci",
                "ro": false, "rm": false, "hotplug": false,
                "size": "62537072640",
                "phy-sec": 512, "log-sec": 512,
                "label": "", "vendor": "", "model": "",
                "mountpoint": null,
                "children": [
                    {"kname":"/dev/mmcblk0p1","type":"part","label":"bootfs",
                     "mountpoint":"/boot/firmware"},
                    {"kname":"/dev/mmcblk0p2","type":"part","label":"rootfs",
                     "mountpoint":"/"}
                ]
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].hasSystemMountpoint());
        CHECK(devices[0].isSystem);
    }

    SECTION("A removable card with only data mounts is not a system drive")
    {
        // The counterpart: a card the user is about to image. It must stay
        // unflagged, or every ordinary write raises a system-drive
        // confirmation and the warning stops meaning anything.
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/mmcblk0",
                "type": "disk",
                "subsystems": "block:mmc:mmc_host:pci",
                "ro": false, "rm": false, "hotplug": false,
                "size": "31914983424",
                "phy-sec": 512, "log-sec": 512,
                "label": "", "vendor": "", "model": "",
                "mountpoint": null,
                "children": [
                    {"kname":"/dev/mmcblk0p1","type":"part","label":"bootfs",
                     "mountpoint":"/media/pi/bootfs"}
                ]
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);
        REQUIRE(devices.size() == 1);
        CHECK_FALSE(devices[0].hasSystemMountpoint());
        CHECK_FALSE(devices[0].isSystem);
    }

    SECTION("A removable USB drive mounted at /home is a system drive")
    {
        // Removable, but the machine depends on it: overwriting it takes the
        // user's home directory with it.
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/sdb",
                "type": "disk",
                "subsystems": "block:scsi:usb:pci",
                "ro": false, "rm": true, "hotplug": true,
                "size": "128035676160",
                "phy-sec": 512, "log-sec": 512,
                "label": "", "vendor": "Samsung", "model": "Portable SSD",
                "mountpoint": null,
                "children": [
                    {"kname":"/dev/sdb1","type":"part","label":"home","mountpoint":"/home"}
                ]
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].isSystem);
    }

    SECTION("Finds a system mountpoint on a nested child")
    {
        // A partition holding an LVM volume, which lsblk reports as a
        // grandchild. The mountpoint has to be found at any depth or a
        // system drive behind LVM goes unflagged.
        const std::string json = R"({
            "blockdevices": [{
                "kname": "/dev/sdd",
                "type": "disk",
                "subsystems": "block:scsi:pci",
                "ro": false, "rm": false, "hotplug": false,
                "size": "256060514304",
                "phy-sec": 512, "log-sec": 512,
                "label": "", "vendor": "Crucial", "model": "CT256",
                "mountpoint": null,
                "children": [
                    {"kname":"/dev/sdd1","type":"part","label":"","mountpoint":null,
                     "children":[
                        {"kname":"/dev/mapper/vg-root","type":"lvm","label":"root",
                         "mountpoint":"/"}
                     ]}
                ]
            }]
        })";

        auto devices = parseLinuxBlockDevices(json, false);
        REQUIRE(devices.size() == 1);
        CHECK(devices[0].hasSystemMountpoint());
        CHECK(devices[0].isSystem);
    }

    SECTION("Well-formed output with nothing in it offers nothing")
    {
        // No drives attached is not a failure, and must not be dressed up as
        // one: these all parse, and all mean the same thing.
        CHECK(parseLinuxBlockDevices("{}", false).empty());
        CHECK(parseLinuxBlockDevices("{\"blockdevices\":[]}", false).empty());
        CHECK(parseLinuxBlockDevices("{\"blockdevices\":{}}", false).empty());
        CHECK(parseLinuxBlockDevices("[]", false).empty());
    }

    SECTION("Output that cannot be read at all is reported, not swallowed")
    {
        // The JSON comes straight from a subprocess, and a truncated or
        // garbled response is not the same thing as no drives. An empty list
        // reaches the storage screen as "no drives found", which sends the
        // user off to reseat a card that was never the problem -- so
        // enumeration failing produces a row saying so instead.
        //
        // The screen knows what to do with it: DriveListModel picks the
        // sentinel out by name and shows its message. What had never been
        // checked was that anything produces one, because the test API used
        // to carry its own copy of the parsing that returned an empty list
        // here.
        for (const char *garbled : {"", "not json at all", "{\"blockdevices\":"}) {
            INFO("input: " << garbled);
            const auto devices = parseLinuxBlockDevices(garbled, false);
            REQUIRE(devices.size() == 1);
            CHECK(devices[0].device == "__error__");
            CHECK_FALSE(devices[0].error.empty());
            CHECK_FALSE(devices[0].description.empty());
        }
    }

    SECTION("An lsblk that will not run at all is reported the same way")
    {
        // No output rather than bad output: lsblk missing, or killed after
        // the timeout. Same outcome for the user, who cannot tell the two
        // apart and does not need to.
        const auto devices = devicesWhenLsblkCannotBeRun(false);

        REQUIRE(devices.size() == 1);
        CHECK(devices[0].device == "__error__");
        CHECK_THAT(devices[0].error, ContainsSubstring("enumerate"));
        CHECK_FALSE(devices[0].description.empty());
    }

    SECTION("A failure row is not something the user can write to")
    {
        // It exists to be read. isDisplayable() is what the storage list
        // filters on, and a sentinel that passed it would appear as a drive
        // with no size and no name.
        const auto devices = devicesWhenLsblkCannotBeRun(false);
        REQUIRE(devices.size() == 1);
        CHECK_FALSE(devices[0].isDisplayable());
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

// ── Display sanitisation ────────────────────────────────────────────────────
//
// Device names come from USB descriptors, so they are attacker-controlled: a
// hostile device chooses what the imager shows in the drive list. The attack
// this defends against is a name that renders as something other than what it
// is -- bidirectional overrides that reverse part of the text, or zero-width
// characters that hide a suffix -- so the user picks it believing it is a
// different drive and overwrites the wrong one.
//
// The sanitiser therefore has to be judged on two counts: it must strip
// everything that can misrepresent, and it must leave ordinary names alone,
// because mangling legitimate non-English device names would be its own bug.

TEST_CASE("Plain ASCII names are left alone", "[drivelist][sanitize]")
{
    CHECK(sanitizeForDisplay("SanDisk Cruzer Blade") == "SanDisk Cruzer Blade");
    CHECK(sanitizeForDisplay("") == "");
    CHECK(sanitizeForDisplay("Generic USB 3.0 (32 GB)") == "Generic USB 3.0 (32 GB)");
}

TEST_CASE("Bidirectional overrides are stripped", "[drivelist][sanitize]")
{
    // The documented attack: U+202E reverses what follows, so a crafted name
    // can be made to read as an entirely different device.
    const std::string attack = "Safe\xE2\x80\xAE" "drivevod\xE2\x80\xAC Storage";
    const std::string clean  = sanitizeForDisplay(attack);

    CHECK(clean.find("\xE2\x80\xAE") == std::string::npos);   // U+202E RLO
    CHECK(clean.find("\xE2\x80\xAC") == std::string::npos);   // U+202C PDF
    // The visible text survives; only the direction controls go.
    CHECK(clean == "Safedrivevod Storage");
}

TEST_CASE("Every bidi control in the guarded ranges is stripped",
          "[drivelist][sanitize]")
{
    // U+202A-U+202E and U+2066-U+2069.
    auto utf8 = [](uint32_t cp) {
        std::string s;
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
        return s;
    };

    for (uint32_t cp = 0x202A; cp <= 0x202E; ++cp) {
        INFO("codepoint U+" << std::hex << cp);
        CHECK(sanitizeForDisplay("a" + utf8(cp) + "b") == "ab");
    }
    for (uint32_t cp = 0x2066; cp <= 0x2069; ++cp) {
        INFO("codepoint U+" << std::hex << cp);
        CHECK(sanitizeForDisplay("a" + utf8(cp) + "b") == "ab");
    }
}

TEST_CASE("Zero-width characters are stripped", "[drivelist][sanitize]")
{
    // These render as nothing, so they let two different names look identical.
    CHECK(sanitizeForDisplay("Kingston\xE2\x80\x8B") == "Kingston");   // U+200B
    CHECK(sanitizeForDisplay("King\xE2\x80\x8Cston") == "Kingston");   // U+200C
    CHECK(sanitizeForDisplay("King\xE2\x80\x8Dston") == "Kingston");   // U+200D
    CHECK(sanitizeForDisplay("\xEF\xBB\xBF" "Kingston") == "Kingston"); // U+FEFF
}

TEST_CASE("Invisible formatting characters are stripped", "[drivelist][sanitize]")
{
    CHECK(sanitizeForDisplay("soft\xC2\xAD" "hyphen") == "softhyphen");   // U+00AD
    CHECK(sanitizeForDisplay("a\xE2\x81\xA0" "b") == "ab");               // U+2060
    CHECK(sanitizeForDisplay("a\xEF\xB8\x80" "b") == "ab");               // U+FE00
}

TEST_CASE("Control characters become spaces rather than vanishing",
          "[drivelist][sanitize]")
{
    // A newline or tab that simply disappeared would let "Disk 1\nEvil" show
    // as "Disk 1Evil"; replacing with a space keeps the join visible.
    CHECK(sanitizeForDisplay("Disk\n1") == "Disk 1");
    CHECK(sanitizeForDisplay("Disk\t1") == "Disk 1");
    CHECK(sanitizeForDisplay(std::string("Disk\0" "1", 6)) == "Disk 1");
    CHECK(sanitizeForDisplay("Disk\x7F" "1") == "Disk 1");
}

TEST_CASE("Legitimate non-English names survive", "[drivelist][sanitize]")
{
    // Over-zealous stripping would be its own defect: a user with a device
    // named in their own script must still be able to recognise it.
    CHECK(sanitizeForDisplay("Sandisk \xC3\xBC" "berdrive") == "Sandisk \xC3\xBC" "berdrive");
    CHECK(sanitizeForDisplay("\xE3\x83\x87\xE3\x82\xA3\xE3\x82\xB9\xE3\x82\xAF")
          == "\xE3\x83\x87\xE3\x82\xA3\xE3\x82\xB9\xE3\x82\xAF");  // ディスク
    CHECK(sanitizeForDisplay("\xF0\x9F\x92\xBE") == "\xF0\x9F\x92\xBE");  // 💾, 4-byte
}

TEST_CASE("Invalid UTF-8 becomes a replacement character", "[drivelist][sanitize]")
{
    // A truncated sequence must not be copied through as-is, or the string
    // handed to the UI is not valid UTF-8 at all.
    const std::string out = sanitizeForDisplay("bad\xC3");
    CHECK(out == "bad\xEF\xBF\xBD");

    // A continuation byte with no lead byte.
    CHECK(sanitizeForDisplay("\x80") == "\xEF\xBF\xBD");
}

TEST_CASE("A name made only of hostile characters collapses to nothing",
          "[drivelist][sanitize]")
{
    CHECK(sanitizeForDisplay("\xE2\x80\xAE\xE2\x80\xAC\xE2\x80\x8B").empty());
}

// ── IDN homograph detection ─────────────────────────────────────────────────

TEST_CASE("Ordinary hostnames are not flagged", "[drivelist][sanitize]")
{
    CHECK_FALSE(hasNonAsciiChars("downloads.raspberrypi.org"));
    CHECK_FALSE(hasNonAsciiChars("localhost"));
    CHECK_FALSE(hasNonAsciiChars(""));
}

TEST_CASE("A hostname with lookalike characters is flagged", "[drivelist][sanitize]")
{
    // Cyrillic 'а' (U+0430) is indistinguishable from Latin 'a' in most
    // fonts, which is the whole point of an IDN homograph.
    CHECK(hasNonAsciiChars("r\xD0\xB0" "spberrypi.org"));
}


#ifdef Q_OS_LINUX
// ══════════════════════════════════════════════════════════════
// An lsblk that does not come back.
//
// Enumerating block devices goes out to the kernel and, through it, to
// whatever is plugged in. A stuck USB bridge or a slow hub can wedge lsblk
// for as long as it likes, and the drive list is refreshed on a timer -- so
// a wait without a limit does not merely delay the list, it takes the
// window with it. The user sees Imager stop responding, with no clue that
// the cause is the reader they just plugged in.
//
// The limit is five seconds, after which the list comes back empty and the
// screen says there are no drives. That is a far better answer than a
// frozen window, and it had no test: every other case here feeds
// pre-captured JSON to the parser and never runs lsblk at all.
// ══════════════════════════════════════════════════════════════

namespace {
// A stand-in lsblk on a PATH of the test's own making, restored afterwards.
class FakeLsblk
{
public:
    explicit FakeLsblk(const QByteArray& body)
        : _savedPath(qgetenv("PATH"))
    {
        REQUIRE(_dir.isValid());
        const QString path = _dir.filePath(QStringLiteral("lsblk"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        const QByteArray script = "#!/bin/sh\n" + body;
        REQUIRE(f.write(script) == script.size());
        f.close();
        REQUIRE(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner));
        // The fixture directory alone: QStandardPaths would otherwise fall
        // back to a built-in default and find the real lsblk.
        qputenv("PATH", _dir.path().toUtf8());
    }

    ~FakeLsblk() { qputenv("PATH", _savedPath); }

private:
    QTemporaryDir _dir;
    QByteArray _savedPath;
};
} // namespace

TEST_CASE("An lsblk that hangs does not take the drive list with it",
          "[drivelist][linux][timeout]")
{
    // Absolute /bin/sleep: PATH is the fixture directory alone, so a bare
    // "sleep" would not be found and the script would fall straight through
    // and answer instantly -- the opposite of the case.
    FakeLsblk fake("/bin/sleep 120\n");

    QElapsedTimer elapsed;
    elapsed.start();
    const std::optional<QByteArray> out = Drivelist::testing::runLsblk();
    const qint64 took = elapsed.elapsed();

    INFO("took " << took << "ms");
    // Gave up rather than waiting on it.
    CHECK_FALSE(out.has_value());
    // Bounded by the five-second limit, not by the two minutes the fixture
    // would otherwise take.
    CHECK(took < 30000);
}

TEST_CASE("An lsblk that fails is not read as an empty machine",
          "[drivelist][linux][timeout]")
{
    // lsblk can print part of an answer and then fail -- a device that goes
    // away mid-enumeration does exactly that. The half-answer must not be
    // parsed as the whole truth, or a drive that is really there disappears
    // from the list while the rest of it looks normal.
    //
    // The fixture prints before failing on purpose. Failing silently is
    // caught further down by the empty-output check, so a fixture that
    // printed nothing would pass with the exit-code check removed and prove
    // nothing about it.
    FakeLsblk fake("echo '{\"blockdevices\":[{\"kname\":\"/dev/sda\"}]}'\n"
                   "echo 'lsblk: /dev/sdb: not a block device' >&2\n"
                   "exit 1\n");

    CHECK_FALSE(Drivelist::testing::runLsblk().has_value());
}

TEST_CASE("An lsblk that answers is believed", "[drivelist][linux][timeout]")
{
    // The other side, so the two refusals above are not simply "runLsblk
    // always gives up".
    FakeLsblk fake("echo '{\"blockdevices\":[]}'\n");

    const std::optional<QByteArray> out = Drivelist::testing::runLsblk();
    REQUIRE(out.has_value());
    CHECK_THAT(out->toStdString(), ContainsSubstring("blockdevices"));
}
#endif // Q_OS_LINUX
