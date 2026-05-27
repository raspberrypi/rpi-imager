/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Wire-protocol tests for the new EEPROM commands in FastbootProtocol.
 * Drives the mock USB transport, asserts the right bytes go out on
 * bulk OUT for `oem eeprom-update`, `oem eeprom-verify` and
 * `oem eeprom-read`.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "fastboot/fastboot_protocol.h"
#include "rpiboot/test/mock_usb_transport.h"

#include <atomic>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace fastboot;
using namespace rpiboot::testing;

namespace {

std::vector<uint8_t> resp(const std::string& prefix, const std::string& msg = "")
{
    std::string s = prefix + msg;
    return {s.begin(), s.end()};
}

bool firstWriteMatches(const MockUsbTransport& mock, const std::string& expected)
{
    if (mock.capturedBulkWrites().empty()) return false;
    const auto& w = mock.capturedBulkWrites()[0];
    return std::string(w.begin(), w.end()) == expected;
}

// Find a captured bulk-OUT write whose ASCII matches `prefix` at offset 0.
// Useful for picking out command writes mixed in with payload writes.
const std::vector<uint8_t>* findWritePrefix(const MockUsbTransport& mock,
                                             const std::string& prefix)
{
    for (const auto& w : mock.capturedBulkWrites()) {
        if (w.size() >= prefix.size()
            && std::memcmp(w.data(), prefix.data(), prefix.size()) == 0) {
            return &w;
        }
    }
    return nullptr;
}

} // namespace

// ── oem eeprom-update ─────────────────────────────────────────────────

TEST_CASE("updateEeprom downloads image then issues oem eeprom-update",
          "[fastboot][eeprom][wire]")
{
    MockUsbTransport mock;

    std::vector<uint8_t> image(4096, 0xA5);
    char sizeHex[9];
    std::snprintf(sizeHex, sizeof(sizeHex), "%08x", static_cast<unsigned>(image.size()));

    // Wire script: download -> DATA -> OKAY ; oem eeprom-update -> OKAY
    mock.queueBulkReadResponse(resp("DATA", sizeHex));
    mock.queueBulkReadResponse(resp("OKAY", ""));
    mock.queueBulkReadResponse(resp("OKAY", "written 4096 bytes"));

    FastbootProtocol fb;
    std::atomic<bool> cancelled{false};

    bool ok = fb.updateEeprom(mock, std::span<const uint8_t>(image),
                               /*spidev=*/"", nullptr, cancelled);
    CHECK(ok);

    // First write must be the download command with hex size.
    CHECK(firstWriteMatches(mock, std::string("download:") + sizeHex));

    // The oem command must be present, AFTER the payload, with no spidev arg.
    const auto* cmd = findWritePrefix(mock, "oem eeprom-update");
    REQUIRE(cmd != nullptr);
    CHECK(std::string(cmd->begin(), cmd->end()) == "oem eeprom-update");
}

TEST_CASE("updateEeprom appends explicit spidev arg when given",
          "[fastboot][eeprom][wire]")
{
    MockUsbTransport mock;

    std::vector<uint8_t> image(1024, 0x5A);
    char sizeHex[9];
    std::snprintf(sizeHex, sizeof(sizeHex), "%08x", static_cast<unsigned>(image.size()));
    mock.queueBulkReadResponse(resp("DATA", sizeHex));
    mock.queueBulkReadResponse(resp("OKAY", ""));
    mock.queueBulkReadResponse(resp("OKAY", "ok"));

    FastbootProtocol fb;
    std::atomic<bool> cancelled{false};

    bool ok = fb.updateEeprom(mock, std::span<const uint8_t>(image),
                               "/dev/spidev10.0", nullptr, cancelled);
    CHECK(ok);

    const auto* cmd = findWritePrefix(mock, "oem eeprom-update");
    REQUIRE(cmd != nullptr);
    CHECK(std::string(cmd->begin(), cmd->end()) == "oem eeprom-update /dev/spidev10.0");
}

TEST_CASE("updateEeprom propagates FAIL from oem command",
          "[fastboot][eeprom][wire][negative]")
{
    MockUsbTransport mock;

    std::vector<uint8_t> image(64, 0);
    char sizeHex[9];
    std::snprintf(sizeHex, sizeof(sizeHex), "%08x", static_cast<unsigned>(image.size()));
    mock.queueBulkReadResponse(resp("DATA", sizeHex));
    mock.queueBulkReadResponse(resp("OKAY", ""));
    mock.queueBulkReadResponse(resp("FAIL", "SPI write protected"));

    FastbootProtocol fb;
    std::atomic<bool> cancelled{false};

    bool ok = fb.updateEeprom(mock, std::span<const uint8_t>(image), "", nullptr, cancelled);
    CHECK_FALSE(ok);
    CHECK_THAT(fb.lastError(), Catch::Matchers::ContainsSubstring("SPI write protected"));
}

// ── oem eeprom-verify ─────────────────────────────────────────────────

TEST_CASE("verifyEeprom downloads image then issues oem eeprom-verify",
          "[fastboot][eeprom][wire]")
{
    MockUsbTransport mock;

    std::vector<uint8_t> image(2048, 0x77);
    char sizeHex[9];
    std::snprintf(sizeHex, sizeof(sizeHex), "%08x", static_cast<unsigned>(image.size()));
    mock.queueBulkReadResponse(resp("DATA", sizeHex));
    mock.queueBulkReadResponse(resp("OKAY", ""));
    mock.queueBulkReadResponse(resp("OKAY", "match"));

    FastbootProtocol fb;
    std::atomic<bool> cancelled{false};

    bool ok = fb.verifyEeprom(mock, std::span<const uint8_t>(image),
                               "", nullptr, cancelled);
    CHECK(ok);

    const auto* cmd = findWritePrefix(mock, "oem eeprom-verify");
    REQUIRE(cmd != nullptr);
    CHECK(std::string(cmd->begin(), cmd->end()) == "oem eeprom-verify");
}

// ── oem eeprom-read ───────────────────────────────────────────────────

TEST_CASE("readEeprom issues oem eeprom-read then uploads bytes",
          "[fastboot][eeprom][wire]")
{
    MockUsbTransport mock;

    // Wire script:
    //   oem eeprom-read -> OKAY (staged for upload)
    //   upload -> DATA<size> + bytes + OKAY
    std::vector<uint8_t> payload(512, 0x11);
    char sizeHex[9];
    std::snprintf(sizeHex, sizeof(sizeHex), "%08x", static_cast<unsigned>(payload.size()));

    mock.queueBulkReadResponse(resp("OKAY", "512 bytes staged"));
    mock.queueBulkReadResponse(resp("DATA", sizeHex));
    mock.queueBulkReadResponse(payload);
    mock.queueBulkReadResponse(resp("OKAY", ""));

    FastbootProtocol fb;
    std::atomic<bool> cancelled{false};

    auto got = fb.readEeprom(mock, "", nullptr, cancelled);
    REQUIRE(got.size() == payload.size());
    CHECK(got == payload);

    // First write must be the oem command (not the upload command).
    CHECK(firstWriteMatches(mock, "oem eeprom-read"));
    CHECK(findWritePrefix(mock, "upload") != nullptr);
}
