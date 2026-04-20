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

#include <iostream>
#include <string>

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
