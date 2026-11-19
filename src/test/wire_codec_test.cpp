// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The helper protocol's encoders and decoders, which carry everything the
// helper says to the client. Pure functions over bytes, so each malformed
// input can be named rather than hoped for.

#include <catch2/catch_test_macros.hpp>

#include "wire/drive_descriptor_json.h"
#include "wire/frame.h"
#include "wire/handshake.h"
#include "wire/server_message.h"

#include <string>
#include <vector>

namespace wire = rpi_imager::privileged::wire;
namespace proto = rpi_imager::privileged::proto;

namespace {

std::string payloadOf(const std::string& frame) {
    wire::FrameAccumulator acc;
    acc.append(frame.data(), frame.size());
    std::string out;
    bool oversize = false;
    REQUIRE(acc.next(out, oversize));
    return out;
}

Drivelist::DeviceDescriptor sampleDrive() {
    Drivelist::DeviceDescriptor d;
    d.device = "/dev/sdz";
    d.raw = "/dev/sdz";
    d.description = "Card \"reader\"\\\n\r\t";
    d.size = 31914983424ULL;
    d.blockSize = 4096;
    d.logicalBlockSize = 512;
    d.busType = "USB";
    d.busVersion = "3.0";
    d.enumerator = "usb";
    d.devicePath = "/sys/x";
    d.parentDevice = "/dev/sdy";
    d.mountpoints = {"/media/pi/bootfs", "/media/pi/rootfs"};
    d.mountpointLabels = {"bootfs", "rootfs"};
    d.childDevices = {"/dev/sdz1", "/dev/sdz2"};
    d.isReadOnly = true;
    d.isSystem = false;
    d.isVirtual = true;
    d.isRemovable = true;
    d.isCard = true;
    d.isSCSI = true;
    d.isUSB = true;
    d.isUAS = true;
    d.busVersionNull = true;
    d.devicePathNull = true;
    d.isUASNull = true;
    return d;
}

} // namespace

TEST_CASE("A response survives the trip from helper to client", "[wire][codec]")
{
    proto::WireResponse response;
    response.set_request_id(42);
    response.set_payload(std::string("\0bytes\xff", 7));
    response.mutable_error()->set_code(proto::ERROR_DEVICE_BUSY);

    proto::WireResponse got;
    proto::WireEvent event;
    bool is_event = true;
    REQUIRE(wire::decodeServerMessage(payloadOf(wire::encodeServerResponse(response)),
                                      got, event, is_event));
    CHECK_FALSE(is_event);
    CHECK(got.request_id() == 42);
    CHECK(got.payload() == response.payload());
    CHECK(got.error().code() == proto::ERROR_DEVICE_BUSY);
}

TEST_CASE("An event is told apart from a response", "[wire][codec]")
{
    proto::WireEvent event;
    event.set_kind(proto::WIRE_EVENT_DRIVE_CHANGED);
    event.mutable_change()->set_kind(proto::DriveChange::KIND_ADDED);

    proto::WireResponse response;
    proto::WireEvent got;
    bool is_event = false;
    REQUIRE(wire::decodeServerMessage(payloadOf(wire::encodeServerEvent(event)),
                                      response, got, is_event));
    CHECK(is_event);
    CHECK(got.kind() == proto::WIRE_EVENT_DRIVE_CHANGED);
    CHECK(got.change().kind() == proto::DriveChange::KIND_ADDED);
}

TEST_CASE("A frame that is neither a response nor an event is refused", "[wire][codec]")
{
    proto::WireResponse response;
    proto::WireEvent event;
    bool is_event = false;

    SECTION("an empty envelope") {
        CHECK_FALSE(wire::decodeServerMessage(std::string(), response, event, is_event));
    }
    SECTION("bytes that are not protobuf") {
        // Field 1, length-delimited, claiming far more bytes than follow.
        CHECK_FALSE(wire::decodeServerMessage(std::string("\x0a\x7f\x01", 3),
                                              response, event, is_event));
    }
}

TEST_CASE("A message too big for one frame is not sent at all", "[wire][codec]")
{
    proto::WireResponse response;
    response.set_payload(std::string(wire::kMaxFrameBytes + 1, 'x'));
    CHECK(wire::encodeServerResponse(response).empty());
}

TEST_CASE("Frames are reassembled however the bytes arrive", "[wire][frame]")
{
    const std::string first = wire::encodeFrame("one");
    const std::string second = wire::encodeFrame(std::string());
    const std::string stream = first + second;

    wire::FrameAccumulator acc;
    std::string out;
    bool oversize = false;

    // One byte at a time: nothing until the first frame is whole.
    for (std::size_t i = 0; i + 1 < first.size(); ++i) {
        acc.append(&stream[i], 1);
        CHECK_FALSE(acc.next(out, oversize));
        CHECK_FALSE(oversize);
    }
    acc.append(&stream[first.size() - 1], stream.size() - first.size() + 1);

    REQUIRE(acc.next(out, oversize));
    CHECK(out == "one");
    REQUIRE(acc.next(out, oversize));
    CHECK(out.empty());
    CHECK_FALSE(acc.next(out, oversize));
    CHECK(acc.buffered() == 0);
}

TEST_CASE("A length beyond the cap is a protocol violation", "[wire][frame]")
{
    const std::uint32_t n = wire::kMaxFrameBytes + 1;
    const char header[4] = {static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF),
                            static_cast<char>((n >> 16) & 0xFF),
                            static_cast<char>((n >> 24) & 0xFF)};
    wire::FrameAccumulator acc;
    acc.append(header, sizeof(header));
    std::string out;
    bool oversize = false;
    CHECK_FALSE(acc.next(out, oversize));
    CHECK(oversize);
}

TEST_CASE("The handshake accepts only the version it speaks", "[wire][handshake]")
{
    proto::ProtocolVersion pv;
    std::string ser;

    SECTION("the same version") {
        pv.set_version(7);
        pv.SerializeToString(&ser);
        CHECK(wire::checkProtocolVersion(ser, 7).code() == proto::ERROR_NONE);
    }
    SECTION("another version") {
        pv.set_version(6);
        pv.SerializeToString(&ser);
        const auto err = wire::checkProtocolVersion(ser, 7);
        CHECK(err.code() == proto::ERROR_HELPER_VERSION_MISMATCH);
        CHECK(err.detail() == "protocol version mismatch: client=6 helper=7");
    }
    SECTION("not a version at all") {
        CHECK(wire::checkProtocolVersion(std::string("\xff", 1), 7).code()
              == proto::ERROR_UNKNOWN);
    }
}

TEST_CASE("A client on another version sees the helper as mismatched", "[wire][handshake]")
{
    proto::HelperStatus status;
    wire::fillReadyHelperStatus(status, 2);
    CHECK(status.state() == proto::HELPER_STATE_INSTALLED_READY);

    wire::applyClientProtocolVersion(status, 2);
    CHECK(status.state() == proto::HELPER_STATE_INSTALLED_READY);

    wire::applyClientProtocolVersion(status, 3);
    CHECK(status.state() == proto::HELPER_STATE_VERSION_MISMATCH);
    CHECK(status.client_version() == "3");

    // Only a ready helper becomes a mismatch; anything else keeps its state.
    proto::HelperStatus absent;
    absent.set_state(proto::HELPER_STATE_NOT_INSTALLED);
    wire::applyClientProtocolVersion(absent, 3);
    CHECK(absent.state() == proto::HELPER_STATE_NOT_INSTALLED);
}

TEST_CASE("Every drive field survives the JSON round trip", "[wire][drives]")
{
    Drivelist::DeviceDescriptor failed;
    failed.device = "/dev/sdq";
    failed.error = "enumeration failed";

    const auto back = wire::deserializeDeviceDescriptors(
        wire::serializeDeviceDescriptors({sampleDrive(), failed}));

    // A drive that failed to enumerate is not offered at all.
    REQUIRE(back.size() == 1);
    const auto& d = back.front();
    const auto want = sampleDrive();
    CHECK(d.device == want.device);
    CHECK(d.raw == want.raw);
    CHECK(d.description == want.description);
    CHECK(d.size == want.size);
    CHECK(d.blockSize == want.blockSize);
    CHECK(d.logicalBlockSize == want.logicalBlockSize);
    CHECK(d.busType == want.busType);
    CHECK(d.busVersion == want.busVersion);
    CHECK(d.enumerator == want.enumerator);
    CHECK(d.devicePath == want.devicePath);
    CHECK(d.parentDevice == want.parentDevice);
    CHECK(d.mountpoints == want.mountpoints);
    CHECK(d.mountpointLabels == want.mountpointLabels);
    CHECK(d.childDevices == want.childDevices);
    CHECK(d.isReadOnly);
    CHECK_FALSE(d.isSystem);
    CHECK(d.isVirtual);
    CHECK(d.isRemovable);
    CHECK(d.isCard);
    CHECK(d.isSCSI);
    CHECK(d.isUSB);
    CHECK(d.isUAS);
    CHECK(d.busVersionNull);
    CHECK(d.devicePathNull);
    CHECK(d.isUASNull);
}

TEST_CASE("Drive JSON from a newer helper still parses", "[wire][drives]")
{
    const auto back = wire::deserializeDeviceDescriptors(
        R"( [ { "device" : "/dev/sdz", "future" : "x", "list" : ["a"],)"
        R"( "map" : { "k" : "v", "l" : "w" }, "flag" : true, "off" : false,)"
        R"( "count" : 3, "empty" : [ ], "size" : 8 } , { } ] )");
    REQUIRE(back.size() == 2);
    CHECK(back[0].device == "/dev/sdz");
    CHECK(back[0].size == 8);
    CHECK(back[1].device.empty());
}

TEST_CASE("Malformed drive JSON yields only the drives before the fault", "[wire][drives]")
{
    using wire::deserializeDeviceDescriptors;
    CHECK(deserializeDeviceDescriptors("").empty());
    CHECK(deserializeDeviceDescriptors("{}").empty());
    CHECK(deserializeDeviceDescriptors("[]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"device\"").empty());
    CHECK(deserializeDeviceDescriptors("[{\"device\":1}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"size\":-1}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"size\":\"big\"}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"isUSB\":yes}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"mountpoints\":[\"a\" \"b\"]}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"mountpoints\":\"a\"}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"device\":\"/dev/a\" \"size\":1}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"x\":{\"k\":1}}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"x\":{\"k\" \"v\"}}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"x\":").empty());
    CHECK(deserializeDeviceDescriptors("[{\"x\":?}]").empty());
    CHECK(deserializeDeviceDescriptors("[{\"device\":\"unterminated").empty());

    const auto partial =
        deserializeDeviceDescriptors("[{\"device\":\"/dev/a\"},{\"device\":2}]");
    REQUIRE(partial.size() == 1);
    CHECK(partial[0].device == "/dev/a");

    CHECK(deserializeDeviceDescriptors("[{\"device\":\"/dev/a\"} {}]").size() == 1);
}

TEST_CASE("A drive list without rich descriptors is rebuilt from its summaries",
          "[wire][drives]")
{
    proto::DriveList list;
    wire::populateDriveInfoSummary(sampleDrive(), *list.add_drives());

    Drivelist::DeviceDescriptor unlabelled;
    unlabelled.device = "/dev/sdx";
    unlabelled.logicalBlockSize = 0;
    unlabelled.mountpoints = {"/mnt/a"};
    wire::populateDriveInfoSummary(unlabelled, *list.add_drives());

    const auto back = wire::deviceDescriptorsFromDriveList(list);
    REQUIRE(back.size() == 2);
    CHECK(back[0].device == "/dev/sdz");
    CHECK(back[0].raw == "/dev/sdz");
    CHECK(back[0].description == sampleDrive().description);
    CHECK(back[0].size == 31914983424ULL);
    CHECK(back[0].isRemovable);
    CHECK(back[0].isUSB);
    CHECK_FALSE(back[0].isSystem);
    CHECK(back[0].blockSize == 512);
    CHECK(back[0].mountpoints == sampleDrive().mountpoints);
    CHECK(back[0].mountpointLabels == sampleDrive().mountpointLabels);
    // A drive that reports no block size is given the conventional one.
    CHECK(back[1].blockSize == 512);
    CHECK(back[1].mountpointLabels == std::vector<std::string>{""});

    list.set_rich_descriptors_json(wire::serializeDeviceDescriptors({sampleDrive()}));
    const auto rich = wire::deviceDescriptorsFromDriveList(list);
    REQUIRE(rich.size() == 1);
    CHECK(rich[0].busType == "USB");
}

TEST_CASE("A drive list's fingerprint changes with what is plugged in", "[wire][drives]")
{
    proto::DriveList a;
    wire::populateDriveInfoSummary(sampleDrive(), *a.add_drives());
    proto::DriveList b = a;
    CHECK(wire::fingerprintDriveList(a) == wire::fingerprintDriveList(b));

    b.mutable_drives(0)->set_size_bytes(1);
    CHECK(wire::fingerprintDriveList(a) != wire::fingerprintDriveList(b));
    CHECK(wire::fingerprintDriveList(proto::DriveList()).empty());
}
