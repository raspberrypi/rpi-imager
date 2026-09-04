/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for the rpiboot protocol core: bootcode loader, file server,
 * and protocol orchestrator.  Uses MockUsbTransport -- no hardware needed.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "rpiboot/test/mock_usb_transport.h"
#include "rpiboot/rpiboot_types.h"
#include "rpiboot/bootcode_loader.h"
#include "rpiboot/file_server.h"
#include <vector>
#include <cstdint>
#include "rpiboot/rpiboot_protocol.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <QTemporaryDir>

using namespace rpiboot;
using namespace rpiboot::testing;

// ── Helper: create a temporary firmware directory ──────────────────────

class TempFirmwareDir {
public:
    TempFirmwareDir() {
        _path = std::filesystem::temp_directory_path() / ("rpiboot_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(_path);
    }

    ~TempFirmwareDir() {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
    }

    void writeFile(const std::string& name, const std::vector<uint8_t>& data) {
        auto filePath = _path / name;
        std::filesystem::create_directories(filePath.parent_path());
        std::ofstream f(filePath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    void writeFile(const std::string& name, const std::string& content) {
        writeFile(name, std::vector<uint8_t>(content.begin(), content.end()));
    }

    const std::filesystem::path& path() const { return _path; }

private:
    std::filesystem::path _path;
};

// ── Helper: create a FileMessage ───────────────────────────────────────

static std::vector<uint8_t> makeFileMessage(FileCommand cmd, const std::string& filename)
{
    FileMessage msg{};
    msg.command = cmd;
    std::memset(msg.filename.data(), 0, msg.filename.size());
    std::memcpy(msg.filename.data(), filename.data(),
                std::min(filename.size(), msg.filename.size() - 1));

    std::vector<uint8_t> bytes(sizeof(msg));
    std::memcpy(bytes.data(), &msg, sizeof(msg));
    return bytes;
}

// ────────────────────────────────────────────────────────────────────────
// Bootcode loader tests
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("BootcodeLoader selects correct bootcode filename", "[rpiboot][bootcode]")
{
    CHECK(BootcodeLoader::bootcodeFilename(ChipGeneration::BCM2836_7) == "bootcode.bin");
    CHECK(BootcodeLoader::bootcodeFilename(ChipGeneration::BCM2711) == "bootcode4.bin");
    CHECK(BootcodeLoader::bootcodeFilename(ChipGeneration::BCM2712) == "bootcode5.bin");
}

TEST_CASE("BootcodeLoader sends BootMessage header and payload", "[rpiboot][bootcode]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;

    // Create a small fake bootcode binary
    std::vector<uint8_t> bootcode(256, 0xAB);
    fw.writeFile("bootcode4.bin", bootcode);

    std::atomic<bool> cancelled{false};
    BootcodeLoader loader;

    REQUIRE(loader.send(mock, ChipGeneration::BCM2711, fw.path(), cancelled));

    // Should have sent two zero-data control transfers (ep_write protocol):
    // 1st announces the BootMessage size (24), 2nd announces the bootcode size
    REQUIRE(mock.capturedControlTransfers().size() == 2);

    auto& ct0 = mock.capturedControlTransfers()[0];
    CHECK(ct0.requestType == VENDOR_REQUEST_TYPE);
    CHECK(ct0.request == VENDOR_REQUEST);
    CHECK(ct0.wValue == sizeof(BootMessage));
    CHECK(ct0.wIndex == 0);
    CHECK(ct0.data.empty());

    auto& ct1 = mock.capturedControlTransfers()[1];
    CHECK(ct1.requestType == VENDOR_REQUEST_TYPE);
    CHECK(ct1.request == VENDOR_REQUEST);
    CHECK(ct1.wValue == static_cast<uint16_t>(bootcode.size() & 0xFFFF));
    CHECK(ct1.wIndex == static_cast<uint16_t>((bootcode.size() >> 16) & 0xFFFF));
    CHECK(ct1.data.empty());

    // Bulk writes carry both the BootMessage (24 bytes) and bootcode payload
    size_t totalBulkBytes = 0;
    for (const auto& w : mock.capturedBulkWrites())
        totalBulkBytes += w.size();
    CHECK(totalBulkBytes == sizeof(BootMessage) + bootcode.size());
}

TEST_CASE("BootcodeLoader fails when file is missing", "[rpiboot][bootcode]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    // No bootcode file written

    std::atomic<bool> cancelled{false};
    BootcodeLoader loader;

    CHECK_FALSE(loader.send(mock, ChipGeneration::BCM2711, fw.path(), cancelled));
    CHECK_THAT(loader.lastError(), Catch::Matchers::ContainsSubstring("Cannot open"));
}

TEST_CASE("BootcodeLoader respects cancellation", "[rpiboot][bootcode]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("bootcode.bin", std::vector<uint8_t>(1024, 0xCC));

    std::atomic<bool> cancelled{true};
    BootcodeLoader loader;

    // Should return false without sending anything
    CHECK_FALSE(loader.send(mock, ChipGeneration::BCM2836_7, fw.path(), cancelled));
    CHECK(mock.capturedControlTransfers().empty());
    CHECK(mock.capturedBulkWrites().empty());
}

// ────────────────────────────────────────────────────────────────────────
// File server tests
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("FileServer handles GetFileSize request", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("config.txt", "enable_uart=1\n");

    // Queue: GetFileSize("config.txt"), then Done
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::GetFileSize, "config.txt"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, fw.path(), nullptr, cancelled));

    // Two control transfers: the file size, then the zero-length acknowledgement
    // the server sends for the empty filename that signals "done" — an empty name
    // is the device's end-of-transfer marker whatever command accompanies it, and
    // acknowledging it matches upstream rpiboot.
    REQUIRE(mock.capturedControlTransfers().size() == 2);

    // Size is encoded in wValue (low 16 bits) / wIndex (high 16 bits), no data
    // payload. "enable_uart=1\n" is 14 bytes.
    auto& sizeReply = mock.capturedControlTransfers()[0];
    CHECK(sizeReply.data.empty());
    CHECK(sizeReply.wValue == 14);
    CHECK(sizeReply.wIndex == 0);

    // The done acknowledgement carries no payload and no size.
    auto& doneAck = mock.capturedControlTransfers()[1];
    CHECK(doneAck.data.empty());
    CHECK(doneAck.wValue == 0);
    CHECK(doneAck.wIndex == 0);
}

TEST_CASE("FileServer handles ReadFile request", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    std::string content = "test file data for read";
    fw.writeFile("test.bin", content);

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "test.bin"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, fw.path(), nullptr, cancelled));

    // Should have sent a control transfer (size header) + bulk writes (data)
    REQUIRE(mock.capturedControlTransfers().size() >= 1);

    size_t totalBulkBytes = 0;
    for (const auto& w : mock.capturedBulkWrites())
        totalBulkBytes += w.size();
    CHECK(totalBulkBytes == content.size());
}

TEST_CASE("FileServer handles Done command cleanly", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    CHECK(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));
}

TEST_CASE("FileServer parses star-prefixed metadata", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;

    // Upstream format: "*PROPERTY*VALUE" (see write_metadata_file).
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*USER_SERIAL_NUM*ABC123"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*MAC_ADDR*B8:27:EB:AA:BB:CC"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));

    CHECK(server.metadata().serialNumber.value_or("") == "ABC123");
    CHECK(server.metadata().macAddress.value_or("") == "B8:27:EB:AA:BB:CC");
    // Every reported field is captured verbatim, in order.
    REQUIRE(server.metadata().fields.size() == 2);
    CHECK(server.metadata().fields[0].first == "USER_SERIAL_NUM");
    CHECK(server.metadata().fields[1].first == "MAC_ADDR");
}

TEST_CASE("FileServer captures provisioning status metadata", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*EEPROM_UPDATE*success"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*SECURE_BOOT_PROVISION*success"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));

    const std::string* eeprom = server.metadata().find("EEPROM_UPDATE");
    const std::string* secure = server.metadata().find("SECURE_BOOT_PROVISION");
    REQUIRE(eeprom != nullptr);
    REQUIRE(secure != nullptr);
    CHECK(*eeprom == "success");
    CHECK(*secure == "success");
}

TEST_CASE("FileServer respects cancellation", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    // Queue a request that won't be processed because we're already cancelled
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "big.bin"));

    std::atomic<bool> cancelled{true};
    FileServer server;

    CHECK_FALSE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));
}

TEST_CASE("FileServer uses custom resolver", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    std::string customContent = "resolved-content";

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "virtual.txt"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    FileResolver resolver = [&](const std::string& name) -> std::vector<uint8_t> {
        if (name == "virtual.txt")
            return {customContent.begin(), customContent.end()};
        return {};
    };

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled, resolver));

    // Verify the custom content was sent via bulk writes
    size_t totalBulkBytes = 0;
    for (const auto& w : mock.capturedBulkWrites())
        totalBulkBytes += w.size();
    CHECK(totalBulkBytes == customContent.size());
}

// ────────────────────────────────────────────────────────────────────────
// ChipGeneration helpers
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("chipGenerationFromPid returns correct enum", "[rpiboot][types]")
{
    CHECK_FALSE(chipGenerationFromPid(0x2763).has_value()); // BCM2835 not supported
    CHECK(chipGenerationFromPid(0x2764) == ChipGeneration::BCM2836_7);
    CHECK(chipGenerationFromPid(0x2711) == ChipGeneration::BCM2711);
    CHECK(chipGenerationFromPid(0x2712) == ChipGeneration::BCM2712);
    CHECK_FALSE(chipGenerationFromPid(0x1234).has_value());
}

TEST_CASE("chipGenerationName returns readable names", "[rpiboot][types]")
{
    CHECK(chipGenerationName(ChipGeneration::BCM2711) == "BCM2711");
    CHECK(chipGenerationName(ChipGeneration::BCM2712) == "BCM2712");
}

// ────────────────────────────────────────────────────────────────────────
// BootMessage layout
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("BootMessage is 24 bytes with correct layout", "[rpiboot][types]")
{
    BootMessage msg{};
    msg.length = 0x12345678;

    auto ptr = reinterpret_cast<const uint8_t*>(&msg);

    // First 4 bytes should be the length in little-endian
    int32_t readBack;
    std::memcpy(&readBack, ptr, sizeof(readBack));
    CHECK(readBack == 0x12345678);

    // Next 20 bytes should be the signature (zeroed)
    for (int i = 4; i < 24; ++i)
        CHECK(ptr[i] == 0);
}

// ────────────────────────────────────────────────────────────────────────
// Transport failure tests — BootcodeLoader
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("BootcodeLoader fails when control transfer fails", "[rpiboot][bootcode][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(256, 0xAB));

    mock.failNextControlTransfers(1);

    std::atomic<bool> cancelled{false};
    BootcodeLoader loader;

    CHECK_FALSE(loader.send(mock, ChipGeneration::BCM2711, fw.path(), cancelled));
    CHECK_THAT(loader.lastError(), Catch::Matchers::ContainsSubstring("Control transfer failed"));
}

TEST_CASE("BootcodeLoader fails when bulk write fails", "[rpiboot][bootcode][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(256, 0xAB));

    // Let control transfers succeed, fail the first bulk write
    // (the BootMessage bulk transfer in the first epWrite call)
    mock.failNextBulkWrites(1);

    std::atomic<bool> cancelled{false};
    BootcodeLoader loader;

    CHECK_FALSE(loader.send(mock, ChipGeneration::BCM2711, fw.path(), cancelled));
    CHECK_THAT(loader.lastError(), Catch::Matchers::ContainsSubstring("Bulk write stalled"));
}

TEST_CASE("BootcodeLoader fails for empty bootcode file", "[rpiboot][bootcode][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    // Write a zero-length file
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>{});

    std::atomic<bool> cancelled{false};
    BootcodeLoader loader;

    CHECK_FALSE(loader.send(mock, ChipGeneration::BCM2711, fw.path(), cancelled));
    CHECK_THAT(loader.lastError(), Catch::Matchers::ContainsSubstring("empty"));
}

// ────────────────────────────────────────────────────────────────────────
// Transport failure tests — FileServer
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("FileServer fails when control transfer fails on GetFileSize", "[rpiboot][fileserver][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("test.bin", "data");

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::GetFileSize, "test.bin"));

    // Fail the control transfer that sends the file size
    mock.failNextControlTransfers(1);

    std::atomic<bool> cancelled{false};
    FileServer server;

    CHECK_FALSE(server.run(mock, fw.path(), nullptr, cancelled));
    CHECK_THAT(server.lastError(), Catch::Matchers::ContainsSubstring("Failed to send file size"));
}

TEST_CASE("FileServer fails when bulk write fails on ReadFile", "[rpiboot][fileserver][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("test.bin", "some data to send");

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "test.bin"));

    // Bulk write for the data payload should fail
    mock.failNextBulkWrites(1);

    std::atomic<bool> cancelled{false};
    FileServer server;

    CHECK_FALSE(server.run(mock, fw.path(), nullptr, cancelled));
    CHECK_THAT(server.lastError(), Catch::Matchers::ContainsSubstring("Bulk write failed"));
}

TEST_CASE("FileServer handles missing file gracefully", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    // Don't create any files

    // Request a file that doesn't exist, then Done
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "nonexistent.bin"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    // Should succeed — missing file sends zero-length response, does not error
    CHECK(server.run(mock, fw.path(), nullptr, cancelled));

    // Should have sent a control transfer with size=0 for the missing file
    // Size is encoded in wValue/wIndex, no data payload
    REQUIRE(!mock.capturedControlTransfers().empty());
    CHECK(mock.capturedControlTransfers()[0].wValue == 0);
    CHECK(mock.capturedControlTransfers()[0].wIndex == 0);
    CHECK(mock.capturedControlTransfers()[0].data.empty());
}

TEST_CASE("FileServer treats a failed message read as a fatal disconnect", "[rpiboot][fileserver][negative]")
{
    MockUsbTransport mock;
    // No responses queued, so controlTransferIn returns -1 (LIBUSB_ERROR_IO).
    // FileMessages arrive over control IN rather than bulk IN, matching upstream
    // rpiboot's ep_read().

    std::atomic<bool> cancelled{false};
    FileServer server;

    // IO (-1) and NO_DEVICE (-4) mean the device has gone, so the server fails
    // immediately instead of spending its retry budget — again matching upstream,
    // which breaks out of its loop on both. Other error codes are retried; that
    // path reports "Failed to read FileMessage" once the retries are exhausted.
    CHECK_FALSE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));
    CHECK_THAT(server.lastError(),
               Catch::Matchers::ContainsSubstring("Device disconnected (libusb error -1)"));
}

TEST_CASE("FileServer fails when control transfer fails on ReadFile size header", "[rpiboot][fileserver][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("test.bin", "content");

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "test.bin"));

    // Fail the control transfer that sends the size header before bulk data
    mock.failNextControlTransfers(1);

    std::atomic<bool> cancelled{false};
    FileServer server;

    CHECK_FALSE(server.run(mock, fw.path(), nullptr, cancelled));
    CHECK_THAT(server.lastError(), Catch::Matchers::ContainsSubstring("Failed to send ReadFile size header"));
}

// ────────────────────────────────────────────────────────────────────────
// FileServer metadata edge cases
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("FileServer parses board-rev metadata as hex", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*USER_BOARDREV*c03111"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));
    REQUIRE(server.metadata().boardRevision.has_value());
    CHECK(*server.metadata().boardRevision == 0xc03111);
}

TEST_CASE("FileServer decodes FACTORY_UUID C40 metadata", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;

    // C40-encoded form of "001000911006186073" (the decoded FACTORY_UUID
    // documented in usbboot's secure-boot-recovery5 README). Each 16-bit
    // half-word encodes 3 characters; words are underscore-separated hex.
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile,
                                               "*FACTORY_UUID*19a519a6_19ab520e_1ac0212b"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));
    const std::string* uuid = server.metadata().find("FACTORY_UUID");
    REQUIRE(uuid != nullptr);
    CHECK(*uuid == "001000911006186073");
}

TEST_CASE("FileServer captures unknown metadata keys generically", "[rpiboot][fileserver]")
{
    MockUsbTransport mock;

    mock.queueBulkReadResponse(makeFileMessage(FileCommand::ReadFile, "*UNKNOWN_KEY*someValue"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    FileServer server;

    REQUIRE(server.run(mock, std::filesystem::temp_directory_path(), nullptr, cancelled));

    // Unknown keys don't populate the typed accessors...
    CHECK_FALSE(server.metadata().serialNumber.has_value());
    CHECK_FALSE(server.metadata().macAddress.has_value());
    CHECK_FALSE(server.metadata().otpState.has_value());
    CHECK_FALSE(server.metadata().boardRevision.has_value());
    // ...but are still recorded verbatim, like upstream's metadata JSON.
    const std::string* v = server.metadata().find("UNKNOWN_KEY");
    REQUIRE(v != nullptr);
    CHECK(*v == "someValue");
}

// ────────────────────────────────────────────────────────────────────────
// resolveSideloadDir tests
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("resolveSideloadDir returns fastboot/ for Fastboot mode", "[rpiboot][protocol]")
{
    TempFirmwareDir fw;
    std::filesystem::create_directories(fw.path() / "fastboot");

    RpibootProtocol protocol;
    std::atomic<bool> cancelled{false};
    MockUsbTransport mock;

    // We can't call resolveSideloadDir directly (it's private), but we can
    // verify through execute() behavior. Instead, test the directory resolution
    // indirectly: set up firmwareDir with fastboot/ subdirectory containing
    // the Done-signaling setup, and verify the file server reads from it.

    // Create a file only in the fastboot/ subdirectory
    fw.writeFile("fastboot/gadget.bin", "fastboot-payload");

    // Queue: dummy return value for bootcode ep_read, then file server messages
    mock.queueBulkReadResponse({0, 0, 0, 0});  // bootcode return value (consumed by BootcodeLoader)
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::GetFileSize, "gadget.bin"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    // Set up bootcode file for the execute() prerequisite
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(64, 0xAA));

    bool ok = protocol.execute(mock, ChipGeneration::BCM2711,
                                SideloadMode::Fastboot, fw.path(),
                                nullptr, cancelled);
    CHECK(ok);

    // Verify a control transfer was made for GetFileSize of gadget.bin
    // with a non-zero size (the file was found in fastboot/ subdir).
    // File-server responses encode size in wValue/wIndex with no data payload.
    // First two control transfers are from bootcode ep_write; file-server transfers follow.
    REQUIRE(!mock.capturedControlTransfers().empty());
    bool foundNonZeroSize = false;
    for (const auto& ct : mock.capturedControlTransfers()) {
        if (ct.data.empty() && ct.wValue == 16) {  // "fastboot-payload" is 16 bytes
            foundNonZeroSize = true;
            break;
        }
    }
    CHECK(foundNonZeroSize);
}

TEST_CASE("resolveSideloadDir prefers secure-boot-recovery5/ for BCM2712", "[rpiboot][protocol]")
{
    TempFirmwareDir fw;
    // Create both directories — BCM2712 should prefer the "5" variant
    std::filesystem::create_directories(fw.path() / "secure-boot-recovery");
    std::filesystem::create_directories(fw.path() / "secure-boot-recovery5");

    fw.writeFile("secure-boot-recovery/recovery.bin", "old");
    fw.writeFile("secure-boot-recovery5/recovery.bin", "new-for-2712");
    fw.writeFile("bootcode5.bin", std::vector<uint8_t>(64, 0xBB));

    MockUsbTransport mock;
    mock.queueBulkReadResponse({0, 0, 0, 0});  // bootcode return value
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::GetFileSize, "recovery.bin"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    RpibootProtocol protocol;

    bool ok = protocol.execute(mock, ChipGeneration::BCM2712,
                                SideloadMode::SecureBootRecovery, fw.path(),
                                nullptr, cancelled);
    CHECK(ok);

    // The file size should be 12 ("new-for-2712") not 3 ("old")
    bool found2712Size = false;
    for (const auto& ct : mock.capturedControlTransfers()) {
        if (ct.data.empty() && ct.wValue == 12) {
            found2712Size = true;
            break;
        }
    }
    CHECK(found2712Size);
}

TEST_CASE("resolveSideloadDir falls back to base dir when subdir missing", "[rpiboot][protocol]")
{
    TempFirmwareDir fw;
    // No fastboot/ subdirectory — should fall back to base firmware dir

    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(64, 0xCC));
    fw.writeFile("fallback.bin", "in-base-dir");

    MockUsbTransport mock;
    mock.queueBulkReadResponse({0, 0, 0, 0});  // bootcode return value
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::GetFileSize, "fallback.bin"));
    mock.queueBulkReadResponse(makeFileMessage(FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    RpibootProtocol protocol;

    bool ok = protocol.execute(mock, ChipGeneration::BCM2711,
                                SideloadMode::Fastboot, fw.path(),
                                nullptr, cancelled);
    CHECK(ok);

    // The file should have been found in the base directory (11 bytes)
    bool foundFile = false;
    for (const auto& ct : mock.capturedControlTransfers()) {
        if (ct.data.empty() && ct.wValue == 11) {  // "in-base-dir"
            foundFile = true;
            break;
        }
    }
    CHECK(foundFile);
}

// ────────────────────────────────────────────────────────────────────────
// RpibootProtocol execute() error paths
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("RpibootProtocol execute fails when bootcode upload fails", "[rpiboot][protocol][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    // No bootcode file — bootcode loader will fail

    std::atomic<bool> cancelled{false};
    RpibootProtocol protocol;

    CHECK_FALSE(protocol.execute(mock, ChipGeneration::BCM2711,
                                  SideloadMode::Fastboot, fw.path(),
                                  nullptr, cancelled));
    CHECK_THAT(protocol.lastError(), Catch::Matchers::ContainsSubstring("Bootcode upload failed"));
}

TEST_CASE("RpibootProtocol execute fails when transport closes after bootcode", "[rpiboot][protocol][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(64, 0xDD));

    std::atomic<bool> cancelled{false};
    RpibootProtocol protocol;

    // After bootcode upload, the protocol waits for the transport to reopen.
    // If the device never re-enumerates, it should time out.
    mock.setOpen(false);  // Will fail after bootcode sends (control transfer succeeds but then isOpen returns false)

    // But: control transfers and bulk writes don't check isOpen in the mock.
    // The bootcode will send successfully, then the wait-for-reenumeration loop
    // will time out because isOpen() returns false.
    CHECK_FALSE(protocol.execute(mock, ChipGeneration::BCM2711,
                                  SideloadMode::Fastboot, fw.path(),
                                  nullptr, cancelled));
    CHECK_THAT(protocol.lastError(), Catch::Matchers::ContainsSubstring("re-enumerate"));
}

TEST_CASE("RpibootProtocol execute respects cancellation", "[rpiboot][protocol][negative]")
{
    MockUsbTransport mock;
    TempFirmwareDir fw;
    fw.writeFile("bootcode4.bin", std::vector<uint8_t>(64, 0xEE));

    std::atomic<bool> cancelled{true};  // Pre-cancelled
    RpibootProtocol protocol;

    CHECK_FALSE(protocol.execute(mock, ChipGeneration::BCM2711,
                                  SideloadMode::Fastboot, fw.path(),
                                  nullptr, cancelled));
}

// ── File requests from the device ───────────────────────────────────────────
//
// The rpiboot file server answers file requests made by whatever is plugged
// in. The filename comes off the wire and the contents go straight back to
// the requester, so the device chooses what gets read. The only check on the
// way in is that the name is printable ASCII, which a traversal satisfies
// perfectly well. The imager frequently runs elevated so that it can write to
// block devices, so "any file this process can read" is a wide set.

TEST_CASE("Firmware files inside the directory are served", "[rpiboot][fileserver]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();

    {
        std::ofstream f(base / "bootcode4.bin", std::ios::binary);
        f << "firmware-bytes";
    }
    std::filesystem::create_directories(base / "2712");
    {
        std::ofstream f(base / "2712" / "bootcode5.bin", std::ios::binary);
        f << "chip-specific";
    }

    const auto plain = rpiboot::FileServer::readFileFromDisk(base, "bootcode4.bin");
    CHECK(std::string(plain.begin(), plain.end()) == "firmware-bytes");

    // Chip subdirectories are a normal request and must keep working.
    const auto nested = rpiboot::FileServer::readFileFromDisk(base, "2712/bootcode5.bin");
    CHECK(std::string(nested.begin(), nested.end()) == "chip-specific");
}

TEST_CASE("A file request that climbs out of the firmware directory is refused",
          "[rpiboot][fileserver]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();
    std::filesystem::create_directories(base / "firmware");

    // A secret next to the firmware directory, standing in for anything the
    // imager's process can read.
    {
        std::ofstream f(base / "secret.txt", std::ios::binary);
        f << "not-for-the-device";
    }

    for (const char *escape : {"../secret.txt",
                               "./../secret.txt",
                               "sub/../../secret.txt",
                               "../../../../../../etc/passwd"}) {
        INFO("request: " << escape);
        const auto data = rpiboot::FileServer::readFileFromDisk(base / "firmware", escape);
        CHECK(data.empty());
    }
}

TEST_CASE("An absolute file request is refused", "[rpiboot][fileserver]")
{
    // std::filesystem::path's operator/ throws away the base when the right
    // side is absolute, so this is a plain read of the named file unless it
    // is checked for.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();

    const auto data = rpiboot::FileServer::readFileFromDisk(base, "/etc/passwd");
    CHECK(data.empty());
    CHECK(std::string(data.begin(), data.end()).find("root:") == std::string::npos);
}

TEST_CASE("A metadata request is not treated as a file", "[rpiboot][fileserver]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();

    CHECK(rpiboot::FileServer::readFileFromDisk(base, "*").empty());
    CHECK(rpiboot::FileServer::readFileFromDisk(base, "*BOARD").empty());
}

TEST_CASE("A request for something that is not there yields nothing",
          "[rpiboot][fileserver]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();

    CHECK(rpiboot::FileServer::readFileFromDisk(base, "absent.bin").empty());
}

TEST_CASE("A request that names a directory rather than a file is refused",
          "[rpiboot][fileserver]")
{
    // An empty filename resolves to the firmware directory itself, and a
    // device can send one: the caller's garbage check walks the characters of
    // the name, so a name with no characters passes it unexamined.
    //
    // A directory opens perfectly well through ifstream on Linux, and the
    // size it then reports is nonsense -- large enough that reserving a
    // buffer for it throws std::bad_alloc, out of a call with no handler
    // anywhere above it. It surfaced here as a test that passed alone and
    // failed under `ctest -j4`, because whether the allocation throws depends
    // on what else is running.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path base = dir.path().toStdString();
    std::filesystem::create_directories(base / "subdir");

    CHECK_NOTHROW(rpiboot::FileServer::readFileFromDisk(base, ""));
    CHECK(rpiboot::FileServer::readFileFromDisk(base, "").empty());

    CHECK_NOTHROW(rpiboot::FileServer::readFileFromDisk(base, "subdir"));
    CHECK(rpiboot::FileServer::readFileFromDisk(base, "subdir").empty());

    CHECK_NOTHROW(rpiboot::FileServer::readFileFromDisk(base, "."));
    CHECK(rpiboot::FileServer::readFileFromDisk(base, ".").empty());
}

// ── The file-serving loop ───────────────────────────────────────────────────
//
// FileServer::run() is what a Compute Module talks to while it boots: the
// device asks for files by name and the host serves them until it says Done.
// Failures here are the ones where a board sits at a blank screen -- it asked
// for something it needed and did not get it -- so what matters is that the
// loop serves what it can, declines what it cannot, and terminates.
//
// Driven through the mock transport: requests are queued as the 260-byte
// messages the protocol expects.

namespace {

// One request as it arrives from the device.
std::vector<uint8_t> fileRequest(rpiboot::FileCommand cmd, const std::string &name)
{
    std::vector<uint8_t> msg(sizeof(rpiboot::FileMessage), 0);
    const auto c = static_cast<int32_t>(cmd);
    std::memcpy(msg.data(), &c, sizeof(c));
    const size_t n = std::min(name.size(), size_t(255));
    std::memcpy(msg.data() + sizeof(int32_t), name.data(), n);
    return msg;
}

// A firmware directory holding one file.
struct FirmwareDir
{
    FirmwareDir()
    {
        REQUIRE(dir.isValid());
        path = std::filesystem::path(dir.path().toStdString());
        std::ofstream f(path / "bootcode4.bin", std::ios::binary);
        f << "bootcode contents";
    }
    QTemporaryDir dir;
    std::filesystem::path path;
};

} // namespace

TEST_CASE("The file server stops when the device says it is done",
          "[rpiboot][fileserver]")
{
    // The ordinary ending: the board has what it needs and signals Done.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    mock.queueBulkReadResponse(fileRequest(rpiboot::FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("error: " << server.lastError());
    CHECK(ok);
}

TEST_CASE("The file server answers a size request", "[rpiboot][fileserver]")
{
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    mock.queueBulkReadResponse(
        fileRequest(rpiboot::FileCommand::GetFileSize, "bootcode4.bin"));
    mock.queueBulkReadResponse(fileRequest(rpiboot::FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("error: " << server.lastError());
    CHECK(ok);

    // The size goes back as a zero-data vendor control transfer with the
    // value split across wValue and wIndex -- not as a bulk write. Checking
    // the number reaches the device matters: the board allocates from it,
    // so a wrong size is a truncated or over-read transfer rather than a
    // clean failure.
    REQUIRE_FALSE(mock.capturedControlTransfers().empty());
    const auto &ct = mock.capturedControlTransfers().front();
    const uint32_t reported = uint32_t(ct.wValue) | (uint32_t(ct.wIndex) << 16);
    INFO("reported size: " << reported);
    CHECK(reported == std::string("bootcode contents").size());
}

TEST_CASE("The file server serves a file the device asks for",
          "[rpiboot][fileserver]")
{
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    mock.queueBulkReadResponse(
        fileRequest(rpiboot::FileCommand::ReadFile, "bootcode4.bin"));
    mock.queueBulkReadResponse(fileRequest(rpiboot::FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("error: " << server.lastError());
    CHECK(ok);

    // The contents went out on the wire.
    bool sawContents = false;
    for (const auto &w : mock.capturedBulkWrites()) {
        const std::string s(w.begin(), w.end());
        if (s.find("bootcode contents") != std::string::npos)
            sawContents = true;
    }
    CHECK(sawContents);
}

TEST_CASE("A request for a file that is not there does not end the boot",
          "[rpiboot][fileserver]")
{
    // The device probes for optional files it may not need. Treating a miss
    // as fatal would abort a boot that would otherwise have worked.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    mock.queueBulkReadResponse(
        fileRequest(rpiboot::FileCommand::ReadFile, "not-present.bin"));
    mock.queueBulkReadResponse(
        fileRequest(rpiboot::FileCommand::ReadFile, "bootcode4.bin"));
    mock.queueBulkReadResponse(fileRequest(rpiboot::FileCommand::Done, ""));

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("error: " << server.lastError());
    CHECK(ok);
}

TEST_CASE("A cancelled file server stops rather than serving on",
          "[rpiboot][fileserver]")
{
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    for (int i = 0; i < 8; ++i)
        mock.queueBulkReadResponse(
            fileRequest(rpiboot::FileCommand::ReadFile, "bootcode4.bin"));

    std::atomic<bool> cancelled{true};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("ok=" << ok << " error: " << server.lastError());
    // Whether it reports success or failure, it must not have kept serving.
    CHECK(mock.capturedBulkWrites().size() < 8);
}

TEST_CASE("A custom resolver is preferred over the firmware directory",
          "[rpiboot][fileserver]")
{
    // How the bootfiles archive is served: the resolver answers from memory
    // and only falls back to disk. A resolver that is ignored means the
    // signed gadget is never sent and the board boots the unsigned one.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    mock.queueBulkReadResponse(
        fileRequest(rpiboot::FileCommand::ReadFile, "bootcode4.bin"));
    mock.queueBulkReadResponse(fileRequest(rpiboot::FileCommand::Done, ""));

    bool resolverAsked = false;
    auto resolver = [&resolverAsked](const std::string &) -> std::vector<uint8_t> {
        resolverAsked = true;
        const std::string data = "from the resolver";
        return {data.begin(), data.end()};
    };

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    server.run(mock, fw.path, nullptr, cancelled, resolver);

    CHECK(resolverAsked);

    bool sawResolverData = false;
    for (const auto &w : mock.capturedBulkWrites()) {
        const std::string s(w.begin(), w.end());
        if (s.find("from the resolver") != std::string::npos)
            sawResolverData = true;
    }
    CHECK(sawResolverData);
}

TEST_CASE("Garbage from the device does not wedge the file server",
          "[rpiboot][fileserver]")
{
    // After the board reboots into the next stage, reads return whatever is
    // left in the pipe. The loop has to bound that rather than interpreting
    // it as filenames for ever.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;
    for (int i = 0; i < 6; ++i)
        mock.queueBulkReadResponse(std::vector<uint8_t>(260, 0xA5));

    std::atomic<bool> cancelled{false};
    rpiboot::FileServer server;
    const bool ok = server.run(mock, fw.path, nullptr, cancelled);

    INFO("ok=" << ok << " error: " << server.lastError());
    // It returned, which is the whole point.
    SUCCEED("run() terminated on garbage input");
}
