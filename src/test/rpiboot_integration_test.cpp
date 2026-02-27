/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Hardware integration tests for rpiboot.
 * Gated by the RPIBOOT_TEST_DEVICE environment variable.
 * Run with: RPIBOOT_TEST_DEVICE=1 ctest -R rpiboot_integration
 */

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/libusb_transport.h"
#include "rpiboot/rpiboot_protocol.h"
#include "rpiboot/firmware_manager.h"

#include <atomic>
#include <cstdlib>

using namespace rpiboot;

static bool hardwareTestEnabled()
{
    const char* env = std::getenv("RPIBOOT_TEST_DEVICE");
    return env && std::string(env) != "0";
}

TEST_CASE("Scan for rpiboot devices", "[.hardware][rpiboot]")
{
    if (!hardwareTestEnabled()) {
        SKIP("Set RPIBOOT_TEST_DEVICE=1 to run hardware tests");
    }

    LibusbContext ctx;
    auto devices = ctx.scanBootDevices();

    // We just check the scan doesn't crash -- actual device presence is optional
    INFO("Found " << devices.size() << " rpiboot device(s)");

    for (const auto& dev : devices) {
        INFO("  Bus " << static_cast<int>(dev.busNumber)
             << " Addr " << static_cast<int>(dev.deviceAddress)
             << " PID 0x" << std::hex << dev.productId
             << " (" << chipGenerationName(dev.chipGeneration) << ")");
    }
}

TEST_CASE("Fastboot sideload to connected CM", "[.hardware][rpiboot]")
{
    if (!hardwareTestEnabled()) {
        SKIP("Set RPIBOOT_TEST_DEVICE=1 to run hardware tests");
    }

    LibusbContext ctx;
    auto devices = ctx.scanBootDevices();

    REQUIRE(!devices.empty());
    INFO("Using device: PID 0x" << std::hex << devices[0].productId);

    auto transport = ctx.openDevice(devices[0]);
    REQUIRE(transport);
    REQUIRE(transport->isOpen());

    // Get firmware
    FirmwareManager fwMgr;
    std::atomic<bool> cancelled{false};
    auto fwDir = fwMgr.ensureAvailable(SideloadMode::Fastboot, devices[0].chipGeneration,
                                        nullptr, cancelled);

    if (fwDir.empty()) {
        SKIP("Firmware not available: " + fwMgr.lastError());
    }

    // Execute protocol
    RpibootProtocol protocol;
    bool ok = protocol.execute(*transport, devices[0].chipGeneration,
                                SideloadMode::Fastboot, fwDir,
                                nullptr, cancelled);

    CHECK(ok);

    if (ok) {
        INFO("Metadata collected:");
        auto& meta = protocol.metadata();
        if (meta.serialNumber)
            INFO("  Serial: " << *meta.serialNumber);
        if (meta.macAddress)
            INFO("  MAC: " << *meta.macAddress);
    }
}
