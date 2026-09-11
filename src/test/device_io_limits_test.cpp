/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Diagnostic test for DeviceIOLimits on all connected physical drives.
 * Reports the raw values that QueryPlatformDeviceIOLimits returns so we
 * can verify the queue-depth and max-transfer heuristics against real hardware.
 *
 * Run:  ctest -R device_io_limits --output-on-failure
 */

#include <catch2/catch_test_macros.hpp>
#include "file_operations.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#endif

using rpi_imager::FileOperations;

// Helper: enumerate connected drives and query their limits.
// Always passes — this is a diagnostic, not a correctness assertion.
TEST_CASE("DeviceIOLimits reports values for connected drives", "[device_io_limits][.diagnostic]") {

#ifdef _WIN32
    // Probe PHYSICALDRIVE0..7
    for (int i = 0; i < 8; ++i) {
        std::string path = "\\\\.\\PHYSICALDRIVE" + std::to_string(i);

        // Quick existence check — don't rely on QueryDeviceIOLimits for this
        HANDLE h = CreateFileA(path.c_str(), 0,  // zero access — just check existence
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        CloseHandle(h);

        SECTION("PHYSICALDRIVE" + std::to_string(i)) {
            auto limits = FileOperations::QueryDeviceIOLimits(path);

            std::cout << "  " << path << ":\n";
            std::cout << "    max_transfer_bytes    = " << limits.max_transfer_bytes;
            if (limits.max_transfer_bytes > 0)
                std::cout << "  (" << (limits.max_transfer_bytes / 1024) << " KB)";
            std::cout << "\n";
            std::cout << "    suggested_queue_depth = " << limits.suggested_queue_depth << "\n";

            // Also query raw adapter descriptor for full visibility
            HANDLE hq = CreateFileA(path.c_str(), 0,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
            if (hq != INVALID_HANDLE_VALUE) {
                STORAGE_PROPERTY_QUERY query = {};
                query.PropertyId = StorageAdapterProperty;
                query.QueryType = PropertyStandardQuery;
                STORAGE_ADAPTER_DESCRIPTOR ad = {};
                DWORD ret = 0;
                if (DeviceIoControl(hq, IOCTL_STORAGE_QUERY_PROPERTY,
                                    &query, sizeof(query), &ad, sizeof(ad), &ret, nullptr)
                    && ret >= offsetof(STORAGE_ADAPTER_DESCRIPTOR, BusMajorVersion)) {
                    std::cout << "    [raw] BusType=" << (int)ad.BusType
                              << " CommandQueueing=" << (ad.CommandQueueing ? "YES" : "NO")
                              << " MaxTransfer=" << ad.MaximumTransferLength
                              << " MaxPhysPages=" << ad.MaximumPhysicalPages
                              << "\n";
                }

                // Device descriptor (variable-length)
                query.PropertyId = StorageDeviceProperty;
                query.QueryType = PropertyStandardQuery;
                BYTE devBuf[512] = {};
                if (DeviceIoControl(hq, IOCTL_STORAGE_QUERY_PROPERTY,
                                    &query, sizeof(query), devBuf, sizeof(devBuf), &ret, nullptr)
                    && ret >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
                    auto* dd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(devBuf);
                    std::cout << "    [raw] DevBusType=" << (int)dd->BusType
                              << " DevCmdQueue=" << (dd->CommandQueueing ? "YES" : "NO")
                              << " RemovableMedia=" << (dd->RemovableMedia ? "YES" : "NO");
                    if (dd->VendorIdOffset)
                        std::cout << " Vendor='" << (char*)devBuf + dd->VendorIdOffset << "'";
                    if (dd->ProductIdOffset)
                        std::cout << " Product='" << (char*)devBuf + dd->ProductIdOffset << "'";
                    std::cout << "\n";
                }

                // IoCapability property 48
                struct IoCap { DWORD Version, Size, LunMax, AdapterMax; };
                query.PropertyId = static_cast<STORAGE_PROPERTY_ID>(48);
                IoCap cap = {};
                if (DeviceIoControl(hq, IOCTL_STORAGE_QUERY_PROPERTY,
                                    &query, sizeof(query), &cap, sizeof(cap), &ret, nullptr)
                    && ret >= sizeof(IoCap)) {
                    std::cout << "    [raw] LunMaxIoCount=" << cap.LunMax
                              << " AdapterMaxIoCount=" << cap.AdapterMax << "\n";
                } else {
                    std::cout << "    [raw] IoCapability: not available\n";
                }
                CloseHandle(hq);
            }

            // Structural checks — values should be sane if non-zero
            if (limits.max_transfer_bytes > 0) {
                CHECK(limits.max_transfer_bytes >= 4096);      // at least one page
                CHECK(limits.max_transfer_bytes <= 256 * 1024 * 1024);  // <256 MB
            }
            if (limits.suggested_queue_depth > 0) {
                CHECK(limits.suggested_queue_depth >= 1);
                CHECK(limits.suggested_queue_depth <= 4096);
            }
        }
    }

#elif defined(__linux__)
    // Probe /dev/sda../dev/sdz and /dev/nvme0n1../dev/nvme7n1
    auto probe = [](const std::string& path) {
        auto limits = FileOperations::QueryDeviceIOLimits(path);
        if (limits.max_transfer_bytes == 0 && limits.suggested_queue_depth == 0)
            return;  // device doesn't exist or sysfs not available

        std::cout << "  " << path << ":\n";
        std::cout << "    max_transfer_bytes    = " << limits.max_transfer_bytes;
        if (limits.max_transfer_bytes > 0)
            std::cout << "  (" << (limits.max_transfer_bytes / 1024) << " KB)";
        std::cout << "\n";
        std::cout << "    suggested_queue_depth = " << limits.suggested_queue_depth << "\n";

        if (limits.max_transfer_bytes > 0)
            CHECK(limits.max_transfer_bytes >= 4096);
        if (limits.suggested_queue_depth > 0)
            CHECK(limits.suggested_queue_depth >= 1);
    };

    for (char c = 'a'; c <= 'z'; ++c)
        probe(std::string("/dev/sd") + c);
    for (int i = 0; i < 8; ++i)
        probe("/dev/nvme" + std::to_string(i) + "n1");

#else
    // macOS: pre-open query returns defaults (limits populated post-open via ioctl)
    std::cout << "  macOS pre-open query returns defaults by design.\n";
    auto limits = FileOperations::QueryDeviceIOLimits("/dev/disk0");
    CHECK(limits.max_transfer_bytes == 0);
    CHECK(limits.suggested_queue_depth == 0);
#endif
}


#ifdef __linux__

// ── Resolving the device, not the spelling of its path ──────────────────
//
// The limits are read out of sysfs, and the node used to be found by taking
// whatever followed "/dev/" as the name under /sys/block. That only holds for
// a device sitting directly in /dev. Every indirect spelling -- the
// /dev/disk/by-id and /dev/disk/by-path symlink farms udev maintains,
// /dev/mapper/... for LVM and LUKS, /dev/md/... for RAID -- produced a sysfs
// path that does not exist, so both limits silently stayed zero and the
// caller sized its writes off nothing at all. Nothing failed; the write just
// ran with the fallback geometry.

#include <filesystem>
#include <sys/stat.h>

namespace {

// First block device whose limits are actually readable here. Loop devices
// are preferred: they are file-backed, so nothing real is touched. Only
// sysfs attributes are read either way -- no I/O reaches any device.
std::string findQueryableBlockDevice(FileOperations::DeviceIOLimits &limitsOut)
{
    std::vector<std::string> candidates;
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator("/sys/block", ec)) {
        std::string name = e.path().filename().string();
        if (name.rfind("loop", 0) == 0)
            candidates.insert(candidates.begin(), "/dev/" + name);
        else
            candidates.push_back("/dev/" + name);
    }
    if (ec)
        return {};

    for (const auto &path : candidates) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISBLK(st.st_mode))
            continue;
        auto limits = FileOperations::QueryDeviceIOLimits(path);
        if (limits.max_transfer_bytes > 0 && limits.suggested_queue_depth > 0) {
            limitsOut = limits;
            return path;
        }
    }
    return {};
}

} // namespace

TEST_CASE("Device limits follow a symlink to the device", "[device_io_limits]")
{
    FileOperations::DeviceIOLimits direct;
    const std::string device = findQueryableBlockDevice(direct);
    if (device.empty())
        SKIP("no block device here reports its queue limits");

    // A path that is not under /dev at all: the old form gave up before it
    // even looked, because the string did not start with "/dev/".
    const auto link = std::filesystem::temp_directory_path()
                    / ("rpi-imager-devlink-" + std::to_string(::getpid()));
    std::error_code ec;
    std::filesystem::remove(link, ec);
    std::filesystem::create_symlink(device, link, ec);
    if (ec)
        SKIP("could not create a symlink to " + device);

    const auto viaLink = FileOperations::QueryDeviceIOLimits(link.string());
    std::filesystem::remove(link, ec);

    INFO("device " << device << " direct max_transfer=" << direct.max_transfer_bytes
         << " via link=" << viaLink.max_transfer_bytes);
    CHECK(viaLink.max_transfer_bytes == direct.max_transfer_bytes);
    CHECK(viaLink.suggested_queue_depth == direct.suggested_queue_depth);
}

TEST_CASE("Device limits are found through the by-id symlink farm",
          "[device_io_limits]")
{
    // The spelling a user's drive most often arrives as. Reads sysfs only.
    std::error_code ec;
    std::filesystem::directory_iterator it("/dev/disk/by-id", ec);
    if (ec)
        SKIP("no /dev/disk/by-id on this host");

    for (const auto &entry : it) {
        const std::string path = entry.path().string();
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISBLK(st.st_mode))
            continue;

        const std::string canonical =
            "/dev/" + std::filesystem::canonical(entry.path(), ec).filename().string();
        if (ec)
            continue;

        const auto direct = FileOperations::QueryDeviceIOLimits(canonical);
        if (direct.max_transfer_bytes == 0)
            continue;   // this one reports nothing either way

        const auto byId = FileOperations::QueryDeviceIOLimits(path);
        INFO(path << " -> " << canonical);
        CHECK(byId.max_transfer_bytes == direct.max_transfer_bytes);
        CHECK(byId.suggested_queue_depth == direct.suggested_queue_depth);
        return;   // one is enough to prove the resolution
    }
    SKIP("no by-id entry reported limits");
}

TEST_CASE("A path that is not a block device reports nothing",
          "[device_io_limits]")
{
    // A regular file has no queue directory, and asking for one by
    // major:minor would land on some unrelated device. Zero means "unknown",
    // which is what the caller's fallback geometry is for.
    const auto file = std::filesystem::temp_directory_path()
                    / ("rpi-imager-notadevice-" + std::to_string(::getpid()));
    { std::ofstream f(file); f << "x"; }

    const auto limits = FileOperations::QueryDeviceIOLimits(file.string());
    std::error_code ec;
    std::filesystem::remove(file, ec);

    CHECK(limits.max_transfer_bytes == 0);
    CHECK(limits.suggested_queue_depth == 0);
}

#endif // __linux__
