/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Tests for the real libusb transport, against a real USB device.
 *
 * LibusbContext and LibusbTransport cannot be tested with a fake. They exist
 * to talk to libusb, libusb talks to the kernel, and the kernel wants a
 * device. Substituting an IUsbContext -- which is what the rest of the
 * rpiboot code is written against, and what rpiboot_thread_test uses --
 * tests the callers and skips these two classes entirely. They were at 5%.
 *
 * So these tests give the kernel a device to talk about: a USB gadget bound
 * to a software device controller (usbip-vudc) and attached back to this
 * machine through a software host controller (vhci-hcd). It appears in
 * lsusb and in /dev/bus/usb, and libusb cannot tell it from hardware,
 * because as far as the USB stack is concerned it is not pretending to be a
 * device -- it is one. data/usb_gadget_emulator.sh sets it up and explains
 * the mechanism; the descriptors come from configfs, so the vendor ID,
 * product ID and interface count are ours to choose.
 *
 * That last part is the point. What this file can test that a mock cannot is
 * the code that reads a descriptor and decides what the device *is* -- which
 * Compute Module generation, therefore which firmware gets sent to it.
 *
 * Requires root (the gadget lives in configfs) and the usbip tools. Skips
 * with a reason when it does not have them.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "rpiboot/libusb_transport.h"
#include "rpiboot/rpiboot_types.h"

#include <libusb.h>

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace rpiboot;

namespace {

constexpr uint16_t NOT_A_COMPUTE_MODULE_PID = 0x9999;

std::string emulatorScriptPath()
{
    return std::string(IMAGER_TEST_DATA_DIR "/usb_gadget_emulator.sh");
}

// Run the emulator script as root, capturing stdout. Goes straight to the
// script when already root and via sudo -n otherwise, for the same reason
// disk_formatter_test does: sudo drops the ambient capability set in a
// namespaced container, and -n keeps an unattended run off a password prompt.
int runEmulator(const std::vector<std::string> &args, std::string *out = nullptr)
{
    const std::string script = emulatorScriptPath();
    std::vector<std::string> owned;
    if (geteuid() != 0) {
        owned.push_back("sudo");
        owned.push_back("-n");
    }
    owned.push_back(script);
    for (const auto &a : args)
        owned.push_back(a);

    std::vector<const char *> argv;
    for (const auto &s : owned)
        argv.push_back(s.c_str());
    argv.push_back(nullptr);

    const char *binary = (geteuid() == 0) ? script.c_str() : "/usr/bin/sudo";

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        execv(binary, const_cast<char *const *>(argv.data()));
        _exit(127);
    }

    close(pipefd[1]);
    std::string captured;
    char buf[256];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof buf)) > 0)
        captured.append(buf, static_cast<size_t>(n));
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (out)
        *out = captured;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Why this machine cannot emulate a USB device, or empty when it can.
// Checked once: it involves loading kernel modules.
const std::string &emulatorUnavailableReason()
{
    static const std::string reason = [] {
        std::string out;
        int rc = runEmulator({"check"}, &out);
        while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
            out.pop_back();
        if (rc == 0 && out == "ok")
            return std::string();
        return out.empty() ? std::string("emulator check failed") : out;
    }();
    return reason;
}

std::string hex16(uint16_t v)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "0x%04x", v);
    return buf;
}

// Is this device on the bus? Asked of libusb directly, so that a test which
// expects the scan to reject a device can first establish that the device is
// there to be rejected.
bool waitUntilOnTheBus(uint16_t vid, uint16_t pid)
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(15);
    while (steady_clock::now() < deadline) {
        libusb_context *ctx = nullptr;
        if (libusb_init(&ctx) == LIBUSB_SUCCESS) {
            libusb_device **list = nullptr;
            const ssize_t count = libusb_get_device_list(ctx, &list);
            bool present = false;
            for (ssize_t i = 0; i < count && !present; ++i) {
                libusb_device_descriptor desc{};
                if (libusb_get_device_descriptor(list[i], &desc) == 0)
                    present = desc.idVendor == vid && desc.idProduct == pid;
            }
            if (count >= 0)
                libusb_free_device_list(list, 1);
            libusb_exit(ctx);
            if (present)
                return true;
        }
        std::this_thread::sleep_for(milliseconds(100));
    }
    return false;
}

// An emulated USB device, present for as long as this object is alive.
//
// Enumeration is asynchronous and udev grants the calling user access to the
// device node a moment after the kernel creates it, so construction waits for
// the device to become openable rather than merely present. A real Compute
// Module behaves the same way; the app's own scan loop exists for this reason.
class EmulatedDevice {
public:
    EmulatedDevice(uint16_t vid, uint16_t pid, int interfaces)
        : _vid(vid), _pid(pid)
    {
        std::string out;
        _up = runEmulator({"up", hex16(vid), hex16(pid), std::to_string(interfaces)}, &out) == 0;
    }

    ~EmulatedDevice()
    {
        if (_up)
            runEmulator({"down"});
    }

    EmulatedDevice(const EmulatedDevice &) = delete;
    EmulatedDevice &operator=(const EmulatedDevice &) = delete;

    bool up() const { return _up; }

    // Take the device away without tearing this object down, so a test can
    // ask what the code does with a device that has just been unplugged.
    void unplug()
    {
        if (_up) {
            runEmulator({"down"});
            _up = false;
        }
    }

    // Wait for the device to appear in the given scan and be openable.
    // Returns the scan result, which is empty if it never arrived.
    template <typename ScanFn>
    std::vector<UsbDeviceInfo> waitUntilOpenable(const IUsbContext &ctx, ScanFn scan) const
    {
        using namespace std::chrono;
        const auto deadline = steady_clock::now() + seconds(15);
        while (steady_clock::now() < deadline) {
            auto found = scan(ctx);
            if (!found.empty()) {
                auto transport = ctx.openDevice(found.front());
                if (transport && transport->isOpen())
                    return found;
            }
            std::this_thread::sleep_for(milliseconds(100));
        }
        return {};
    }

    std::vector<UsbDeviceInfo> waitForBootDevice(const IUsbContext &ctx) const
    {
        return waitUntilOpenable(ctx, [](const IUsbContext &c) { return c.scanBootDevices(); });
    }

    uint16_t vid() const { return _vid; }
    uint16_t pid() const { return _pid; }

private:
    uint16_t _vid;
    uint16_t _pid;
    bool _up = false;
};

// Every test here needs the emulator, and none of them can say anything
// without it.
#define REQUIRE_EMULATOR()                                                     \
    do {                                                                       \
        const std::string &why = emulatorUnavailableReason();                  \
        if (!why.empty())                                                      \
            SKIP("USB device emulation unavailable: " + why);                  \
    } while (0)

} // namespace

TEST_CASE("A Compute Module in USB boot mode is discovered", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);

    REQUIRE(found.size() == 1);
    CHECK(found.front().vendorId == BROADCOM_VID);
    CHECK(found.front().productId == static_cast<uint16_t>(ChipGeneration::BCM2711));
    CHECK(found.front().chipGeneration == ChipGeneration::BCM2711);
    CHECK(deviceDescription(found.front().chipGeneration) == "Compute Module 4 (USB Boot)");
}

TEST_CASE("Each Compute Module generation is identified from its product ID", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // The generation chosen here decides which firmware directory is sent to
    // the device. Reporting a CM5 as a CM4 would sideload the wrong bootcode.
    struct Case {
        ChipGeneration generation;
        const char *description;
    };
    auto c = GENERATE(
        Case{ChipGeneration::BCM2836_7, "Compute Module 3 (USB Boot)"},
        Case{ChipGeneration::BCM2711,   "Compute Module 4 (USB Boot)"},
        Case{ChipGeneration::BCM2712,   "Compute Module 5 (USB Boot)"});

    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(c.generation), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);

    REQUIRE(found.size() == 1);
    CHECK(found.front().chipGeneration == c.generation);
    CHECK(deviceDescription(found.front().chipGeneration) == c.description);
}

TEST_CASE("A Broadcom device that is not a Compute Module is left alone", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // Broadcom's vendor ID covers a great deal besides Compute Modules. The
    // scan matching on vendor alone would offer the user a wireless adapter
    // as something to sideload firmware onto.
    EmulatedDevice device(BROADCOM_VID, NOT_A_COMPUTE_MODULE_PID, 1);
    REQUIRE(device.up());

    // Watch the bus directly rather than through the code under test: an
    // empty scan proves nothing if the device simply had not arrived yet.
    REQUIRE(waitUntilOnTheBus(BROADCOM_VID, NOT_A_COMPUTE_MODULE_PID));

    LibusbContext ctx;
    CHECK(ctx.scanBootDevices().empty());
}

TEST_CASE("An opened Compute Module claims its interface", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);
    CHECK(transport->isOpen());

    // initDiagnostics is what a failed sideload reports back to the user, so
    // it has to record what actually happened rather than a fixed string.
    const QString diag = transport->initDiagnostics();
    INFO("initDiagnostics: " << diag.toStdString());
    CHECK_THAT(diag.toStdString(), Catch::Matchers::ContainsSubstring("claim=OK"));
    CHECK_THAT(diag.toStdString(), Catch::Matchers::ContainsSubstring("ifaces=1"));
}

TEST_CASE("Endpoints are chosen from the descriptor's interface count", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // Upstream rpiboot's Initialize_Device() picks interface 0 on a
    // single-interface device and interface 1 otherwise, with different bulk
    // endpoints for each. Get this wrong and every transfer goes nowhere.
    SECTION("a single-interface device uses interface 0")
    {
        EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
        REQUIRE(device.up());

        LibusbContext ctx;
        auto found = device.waitForBootDevice(ctx);
        REQUIRE(found.size() == 1);

        auto transport = ctx.openDevice(found.front());
        REQUIRE(transport);
        CHECK(transport->outEndpoint() == 0x01);
        CHECK(transport->inEndpoint() == 0x82);
    }

    SECTION("a device with more than one interface uses interface 1")
    {
        EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2712), 2);
        REQUIRE(device.up());

        LibusbContext ctx;
        auto found = device.waitForBootDevice(ctx);
        REQUIRE(found.size() == 1);

        auto transport = ctx.openDevice(found.front());
        REQUIRE(transport);
        CHECK(transport->outEndpoint() == 0x03);
        CHECK(transport->inEndpoint() == 0x84);
        CHECK_THAT(transport->initDiagnostics().toStdString(),
                   Catch::Matchers::ContainsSubstring("ifaces=2"));
    }
}

TEST_CASE("The interface descriptor string is read from the device", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // interfaceString() is how a fastboot gadget is positively identified
    // (rpiboot::FASTBOOT_INTERFACE_DESCRIPTOR). If it silently returned
    // nothing, the app would fall back to matching on vendor and product ID
    // alone and could address the wrong device.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);

    // The gadget's Loopback function advertises this string; the value does
    // not matter, only that the descriptor is fetched rather than assumed.
    CHECK(transport->interfaceString() == "loop input to output");
}

TEST_CASE("Data written to a Compute Module reads back", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // The gadget's Loopback function returns on its IN endpoint whatever was
    // sent to its OUT endpoint, so a round trip proves both halves of the
    // transport move real bytes over a real bulk pipe.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);

    std::vector<uint8_t> sent(64);
    for (size_t i = 0; i < sent.size(); ++i)
        sent[i] = static_cast<uint8_t>(i);

    CHECK(transport->bulkWrite(transport->outEndpoint(), sent, 2000)
          == static_cast<int>(sent.size()));

    std::vector<uint8_t> received(sent.size(), 0);
    // bulkRead sets the IN direction bit itself, so this reads endpoint 0x81.
    CHECK(transport->bulkRead(0x01, received, 2000) == static_cast<int>(sent.size()));
    CHECK(received == sent);
}

TEST_CASE("A transfer to an endpoint the device lacks fails rather than hanging", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);

    // bulkWrite retries transient errors up to three times with a 50ms pause,
    // so this also proves the retry loop terminates instead of spinning.
    std::vector<uint8_t> data(16, 0xA5);
    const auto started = std::chrono::steady_clock::now();
    const int rc = transport->bulkWrite(0x07, data, 300);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(rc < 0);
    CHECK(elapsed < std::chrono::seconds(10));
}

TEST_CASE("A Compute Module already in use reports why it could not be claimed", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // Two things reaching for the same Compute Module is ordinary: a second
    // copy of the app, or a previous run that has not let go yet. The kernel
    // gives the interface to whoever asked first and refuses the rest.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto firstHolder = ctx.openDevice(found.front());
    REQUIRE(firstHolder);
    REQUIRE_THAT(firstHolder->initDiagnostics().toStdString(),
                 Catch::Matchers::ContainsSubstring("claim=OK"));

    auto second = ctx.openDevice(found.front());
    REQUIRE(second);

    // What the second one reports is the whole value of initDiagnostics: the
    // transport is constructed either way, so this string is the only record
    // of the interface not having been claimed.
    const std::string diag = second->initDiagnostics().toStdString();
    INFO("second initDiagnostics: " << diag);
    CHECK_THAT(diag, !Catch::Matchers::ContainsSubstring("claim=OK"));
    CHECK_THAT(diag, Catch::Matchers::ContainsSubstring("claim="));

    // Documenting rather than endorsing: isOpen() answers for the handle, not
    // for the interface, so a transport that lost the claim still reports
    // itself open and will fail later on a transfer instead of here.
    CHECK(second->isOpen());
}

TEST_CASE("An interface that advertises no string yields no string", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // The two-interface gadget's second interface carries iInterface=0, which
    // is what a device with nothing to say about itself looks like. The
    // fastboot check compares this against a known descriptor, so an empty
    // answer has to mean "not that device" rather than crash or garbage.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2712), 2);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);
    // Interface 1 is the one selected for a multi-interface device.
    CHECK(transport->interfaceString().empty());
}

TEST_CASE("A control read returns the bytes the device sent", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // The rpiboot handshake is control transfers: this is how the boot
    // message and its acknowledgements travel. Asking for the device
    // descriptor is a request every USB device must answer, and its length
    // is fixed by the specification, so the reply can be checked exactly.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);

    constexpr uint8_t DEVICE_TO_HOST_STANDARD = 0x80;
    constexpr uint8_t GET_DESCRIPTOR = 0x06;
    constexpr uint16_t DESCRIPTOR_TYPE_DEVICE = 0x0100;
    constexpr int DEVICE_DESCRIPTOR_LENGTH = 18;

    std::vector<uint8_t> buffer(64, 0);
    const int n = transport->controlTransferIn(DEVICE_TO_HOST_STANDARD, GET_DESCRIPTOR,
                                               DESCRIPTOR_TYPE_DEVICE, 0, buffer, 2000);

    REQUIRE(n == DEVICE_DESCRIPTOR_LENGTH);
    CHECK(buffer[0] == DEVICE_DESCRIPTOR_LENGTH);   // bLength
    CHECK(buffer[1] == 0x01);                        // bDescriptorType: DEVICE

    // idVendor and idProduct are little-endian at offsets 8 and 10, and are
    // the two fields the whole device-identification path turns on.
    const uint16_t vid = static_cast<uint16_t>(buffer[8] | (buffer[9] << 8));
    const uint16_t pid = static_cast<uint16_t>(buffer[10] | (buffer[11] << 8));
    CHECK(vid == BROADCOM_VID);
    CHECK(pid == static_cast<uint16_t>(ChipGeneration::BCM2711));
}

TEST_CASE("A control write the device refuses is reported as a failure", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // controlTransfer returns a bool, so a refused request has exactly one
    // way to be noticed. If it came back true the sideload would carry on
    // having sent nothing, and report success for a device it never reached.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);

    auto transport = ctx.openDevice(found.front());
    REQUIRE(transport);

    constexpr uint8_t HOST_TO_DEVICE_VENDOR = 0x40;
    constexpr uint8_t REQUEST_THE_DEVICE_DOES_NOT_IMPLEMENT = 0x99;

    const std::vector<uint8_t> payload{1, 2, 3, 4};
    CHECK_FALSE(transport->controlTransfer(HOST_TO_DEVICE_VENDOR,
                                           REQUEST_THE_DEVICE_DOES_NOT_IMPLEMENT,
                                           0, 0, payload, 2000));
}

TEST_CASE("A Compute Module that has been unplugged cannot be opened", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // Someone pulling the cable mid-sideload is the ordinary case, not the
    // exotic one. openDevice has to return nothing rather than a handle to a
    // device that is no longer there.
    EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
    REQUIRE(device.up());

    LibusbContext ctx;
    auto found = device.waitForBootDevice(ctx);
    REQUIRE(found.size() == 1);
    const UsbDeviceInfo stale = found.front();

    device.unplug();

    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(15);
    while (steady_clock::now() < deadline && !ctx.scanBootDevices().empty())
        std::this_thread::sleep_for(milliseconds(100));

    CHECK(ctx.scanBootDevices().empty());
    CHECK(ctx.openDevice(stale) == nullptr);
}

TEST_CASE("The boot scan and the fastboot scan do not claim each other's devices", "[rpiboot][usbdevice]")
{
    REQUIRE_EMULATOR();

    // A device in fastboot mode and one in USB boot mode need different
    // treatment entirely, and the two scans run against the same bus.
    SECTION("a device in USB boot mode is not offered to fastboot")
    {
        EmulatedDevice device(BROADCOM_VID, static_cast<uint16_t>(ChipGeneration::BCM2711), 1);
        REQUIRE(device.up());

        LibusbContext ctx;
        REQUIRE(device.waitForBootDevice(ctx).size() == 1);
        CHECK(ctx.scanFastbootDevices().empty());
    }

    SECTION("a device in fastboot mode is not offered to rpiboot")
    {
        EmulatedDevice device(FASTBOOT_VID, FASTBOOT_PID, 1);
        REQUIRE(device.up());

        LibusbContext ctx;
        auto found = device.waitUntilOpenable(
            ctx, [](const IUsbContext &c) { return c.scanFastbootDevices(); });
        REQUIRE(found.size() == 1);
        CHECK(found.front().vendorId == FASTBOOT_VID);
        CHECK(found.front().productId == FASTBOOT_PID);
        CHECK(ctx.scanBootDevices().empty());
    }
}
