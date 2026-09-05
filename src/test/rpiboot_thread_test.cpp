/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * RpibootThread: putting a compute module into a mode that can be written.
 *
 * This is the CM4/CM5 path. The board comes up in USB boot mode, gets a
 * bootcode and a firmware image pushed into it, reboots, and comes back as
 * a fastboot device -- and only then is there anything to write to.
 *
 * Every line of it was unreachable without a compute module in boot mode on
 * the bus, because each step constructed a LibusbContext inline. Behind
 * IUsbContext the sequence can be driven against whatever devices a test
 * says are present.
 *
 * The property that matters most here is the same one that matters in
 * FastbootFlashThread: the imager must act on the board the user chose and
 * nothing else. A phone left in fastboot mode on the same bus is not the
 * device they meant.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "rpibootthread.h"
#include "rpiboot/libusb_transport.h"
#include "rpiboot/firmware_manager.h"

#include <QCoreApplication>
#include <QStringList>
#include <QStandardPaths>

#include <atomic>
#include <functional>
#include <filesystem>
#include <stdexcept>

using rpiboot::UsbDeviceInfo;

namespace {

UsbDeviceInfo device(uint8_t bus, uint8_t addr, std::vector<uint8_t> port,
                     uint8_t serialNumberIndex = 0)
{
    UsbDeviceInfo d{};
    d.busNumber = bus;
    d.deviceAddress = addr;
    d.portPath = std::move(port);
    d.chipGeneration = rpiboot::ChipGeneration::BCM2712;
    // 0 and 3 are what a board reports before the bootcode lands; anything
    // else means the second stage is up and it is a different device to us.
    d.serialNumberIndex = serialNumberIndex;
    return d;
}

// A bus with whatever the test says is plugged into it.
struct FakeBus {
    std::vector<UsbDeviceInfo> fastbootDevices;
    std::vector<UsbDeviceInfo> bootDevices;
    int fastbootScans = 0;
    int bootScans = 0;
    bool throwOnScan = false;
    // Runs at the top of every scan, so a test can end the poll rather than
    // waiting out the sixty seconds the real one is willing to spend.
    std::function<void(int)> onScan;
};

// Handed to the thread; the bus itself is owned by the test and outlives it.
// The thread takes a fresh context per phase and destroys it, so handing it
// the bus directly would be a use-after-free.
class BusView : public rpiboot::IUsbContext
{
public:
    explicit BusView(FakeBus &bus) : _bus(bus) {}

    std::vector<UsbDeviceInfo> scanBootDevices() const override
    {
        ++_bus.bootScans;
        if (_bus.onScan)
            _bus.onScan(_bus.bootScans);
        if (_bus.throwOnScan)
            throw std::runtime_error("bus went away mid-scan");
        return _bus.bootDevices;
    }

    std::vector<UsbDeviceInfo> scanFastbootDevices() const override
    {
        ++_bus.fastbootScans;
        if (_bus.onScan)
            _bus.onScan(_bus.fastbootScans);
        if (_bus.throwOnScan)
            throw std::runtime_error("bus went away mid-scan");
        return _bus.fastbootDevices;
    }

    std::unique_ptr<rpiboot::IUsbTransport> openDevice(const UsbDeviceInfo &) const override
    {
        return nullptr;
    }

private:
    FakeBus &_bus;
};

// Stands in for the download. ensureAvailable() is where runPhase() begins,
// so without this a test gets no further than "Failed to obtain firmware".
class FakeFirmware : public rpiboot::FirmwareManager
{
public:
    std::filesystem::path dir;
    std::string error;

    std::filesystem::path ensureAvailable(rpiboot::SideloadMode,
                                          rpiboot::ChipGeneration,
                                          rpiboot::ProgressCallback,
                                          std::atomic<bool> &) override
    {
        if (!error.empty())
            _reportedError = error;
        return dir;
    }

    const std::string &lastError() const override { return _reportedError; }

private:
    std::string _reportedError;
};

class TestableRpibootThread : public RpibootThread
{
public:
    using RpibootThread::RpibootThread;
    using RpibootThread::pollForFastbootDevice;
    using RpibootThread::waitForBootDeviceReEnum;
    using RpibootThread::pollForRpibootReturn;
    using RpibootThread::runPhase;

    FakeBus bus;
    bool busUnavailable = false;

    // What the firmware step will hand back.
    std::filesystem::path firmwareDir;
    std::string firmwareError;
    bool noFirmwareManager = false;

protected:
    std::unique_ptr<rpiboot::IUsbContext> makeUsbContext() override
    {
        if (busUnavailable)
            return nullptr;
        return std::make_unique<BusView>(bus);
    }

    std::unique_ptr<rpiboot::FirmwareManager> makeFirmwareManager() override
    {
        if (noFirmwareManager)
            return nullptr;
        auto fw = std::make_unique<FakeFirmware>();
        fw->dir = firmwareDir;
        fw->error = firmwareError;
        return fw;
    }
};

// What the thread told the user. The board is headless, so these signals
// are the whole of what a person has to go on when a sideload fails.
struct SignalLog
{
    QStringList errors;
    QStringList status;

    void attach(RpibootThread *t)
    {
        QObject::connect(t, &RpibootThread::error,
                         [this](QString m) { errors << m; });
        QObject::connect(t, &RpibootThread::preparationStatusUpdate,
                         [this](QString m) { status << m; });
    }
};

// The board the user picked, on port path 1.2.
RpibootThread::DeviceInfo chosenDevice(std::vector<uint8_t> port = {1, 2})
{
    RpibootThread::DeviceInfo d{};
    d.busNumber = 1;
    d.deviceAddress = 4;
    d.portPath = std::move(port);
    d.chipGeneration = rpiboot::ChipGeneration::BCM2712;
    return d;
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("rpiboot_thread_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}

// ══════════════════════════════════════════════════════════════
// Waiting for the board to come back as a fastboot device
// ══════════════════════════════════════════════════════════════

TEST_CASE("The board that comes back on the chosen port is the one taken",
          "[rpiboot][fastboot-wait]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.fastbootDevices = { device(1, 9, {1, 2}) };

    std::atomic<bool> found{false};
    QString id;
    REQUIRE(t.pollForFastbootDevice(found, id));

    CHECK(found.load());
    CHECK(id == QStringLiteral("1:9"));
}

TEST_CASE("A fastboot device on a different port is not taken",
          "[rpiboot][fastboot-wait]")
{
    // Somebody's phone, or a second board. Acting on it would reflash a
    // device its owner never offered up.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.fastbootDevices = { device(1, 9, {3, 4}) };
    t.bus.onScan = [&t](int n) { if (n >= 2) t.cancel(); };

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));

    CHECK_FALSE(found.load());
    CHECK(id.isEmpty());
}

TEST_CASE("The right board is found among several fastboot devices",
          "[rpiboot][fastboot-wait]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.fastbootDevices = {
        device(1, 7, {3, 4}),
        device(1, 9, {1, 2}),     // the chosen one
        device(2, 3, {5, 6}),
    };

    std::atomic<bool> found{false};
    QString id;
    REQUIRE(t.pollForFastbootDevice(found, id));
    CHECK(id == QStringLiteral("1:9"));
}

// ── No port path: the identification is weaker, so the rules are stricter ──

TEST_CASE("With no port path a sole fastboot device is taken",
          "[rpiboot][fastboot-wait]")
{
    // Some enumerations give no port path. One device on the bus is
    // unambiguous, so it is accepted.
    TestableRpibootThread t{chosenDevice({}), rpiboot::SideloadMode::Fastboot};
    t.bus.fastbootDevices = { device(1, 9, {}) };

    std::atomic<bool> found{false};
    QString id;
    REQUIRE(t.pollForFastbootDevice(found, id));
    CHECK(id == QStringLiteral("1:9"));
}

TEST_CASE("With no port path and more than one device, none is taken",
          "[rpiboot][fastboot-wait]")
{
    // Nothing distinguishes them, so there is no way to know which board the
    // user meant. Guessing here writes an image to the wrong device.
    TestableRpibootThread t{chosenDevice({}), rpiboot::SideloadMode::Fastboot};
    t.bus.fastbootDevices = { device(1, 9, {}), device(1, 10, {}) };
    t.bus.onScan = [&t](int n) { if (n >= 2) t.cancel(); };

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));
    CHECK_FALSE(found.load());
}

// ── Giving up ──────────────────────────────────────────────────────────

TEST_CASE("Cancelling stops the wait", "[rpiboot][fastboot-wait][cancel]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.cancel();

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));
    // Cancelled before the bus was touched at all.
    CHECK(t.bus.fastbootScans == 0);
}

TEST_CASE("A bus that cannot be opened is reported rather than crashed into",
          "[rpiboot][fastboot-wait]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.busUnavailable = true;

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));
    CHECK_FALSE(found.load());
}

TEST_CASE("A scan that throws does not end the wait",
          "[rpiboot][fastboot-wait]")
{
    // Devices come and go while the board is rebooting, and libusb throws
    // when it enumerates one that has just left. Giving up on the first of
    // those would abandon a board that was about to appear.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.throwOnScan = true;
    t.bus.onScan = [&t](int n) {
        if (n >= 3)
            t.cancel();
    };

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));
    CHECK(t.bus.fastbootScans >= 3);
}

// ══════════════════════════════════════════════════════════════
// Waiting for the board to go away and come back
//
// The bootcode is pushed to a board in ROM mode, which then reboots. The
// same physical board returns on the same port, but as a different USB
// device: it now has a real serial descriptor, where in ROM mode the index
// is 0 or 3. That difference is the only way to tell "it has come back"
// from "it has not left yet" -- and mistaking one for the other either
// hangs the sequence or pushes the second stage at a board still running
// the first.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A board that goes away and returns with a serial is picked up",
          "[rpiboot][re-enum]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};

    // Present in ROM mode, then gone, then back as the second stage.
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };
    t.bus.onScan = [&t](int n) {
        if (n == 2) t.bus.bootDevices.clear();
        if (n >= 3) t.bus.bootDevices = { device(1, 11, {1, 2}, 5) };
    };

    rpiboot::UsbDeviceInfo out{};
    REQUIRE(t.waitForBootDeviceReEnum(out));

    CHECK(out.deviceAddress == 11);
    CHECK(out.serialNumberIndex == 5);
}

TEST_CASE("A board still in ROM mode is not mistaken for the returned one",
          "[rpiboot][re-enum]")
{
    // Serial index 0 is the state it was already in. Accepting it would send
    // the second stage to a board that never restarted.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };
    t.bus.onScan = [&t](int n) {
        if (n == 2) t.bus.bootDevices.clear();
        if (n >= 3) t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };
        if (n >= 6) t.cancel();
    };

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
}

TEST_CASE("Serial index three is also still ROM mode", "[rpiboot][re-enum]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = {};
    t.bus.onScan = [&t](int n) {
        if (n >= 2) t.bus.bootDevices = { device(1, 4, {1, 2}, 3) };
        if (n >= 6) t.cancel();
    };

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
}

TEST_CASE("A different board coming up is not taken for ours",
          "[rpiboot][re-enum]")
{
    // Two compute modules being provisioned side by side. The one on the
    // other port is somebody else's job.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = {};
    t.bus.onScan = [&t](int n) {
        if (n >= 2) t.bus.bootDevices = { device(1, 12, {7, 8}, 5) };
        if (n >= 6) t.cancel();
    };

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
}

TEST_CASE("With no port path the board is identified by bus and address",
          "[rpiboot][re-enum]")
{
    RpibootThread::DeviceInfo d = chosenDevice({});
    d.busNumber = 2;
    d.deviceAddress = 7;
    TestableRpibootThread t{d, rpiboot::SideloadMode::Fastboot};

    t.bus.bootDevices = {};
    t.bus.onScan = [&t](int n) {
        if (n >= 2) t.bus.bootDevices = {
            device(2, 7, {}, 5),      // ours, by bus and address
            device(3, 7, {}, 5),      // same address, wrong bus
        };
    };

    rpiboot::UsbDeviceInfo out{};
    REQUIRE(t.waitForBootDeviceReEnum(out));
    CHECK(out.busNumber == 2);
}

TEST_CASE("Cancelling stops the wait before the bus is touched",
          "[rpiboot][re-enum][cancel]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.cancel();

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
    CHECK(t.bus.bootScans == 0);
}

TEST_CASE("A bus that cannot be opened ends the wait", "[rpiboot][re-enum]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.busUnavailable = true;

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
}

// ══════════════════════════════════════════════════════════════
// Secure-boot reprovision: waiting for the board after an EEPROM write
//
// Unlike the fastboot wait, this one has no fallback. The board is coming
// back from having its bootloader rewritten, so there is no "sole device on
// the bus, must be it" -- without a port path nothing is accepted at all.
// The board is told apart from its pre-reboot self by the address, since a
// fresh enumeration is given a new one.
// ══════════════════════════════════════════════════════════════

TEST_CASE("The board returning on the same port with a new address is taken",
          "[rpiboot][sbr]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = { device(1, 21, {1, 2}) };

    std::atomic<bool> found{false};
    REQUIRE(t.pollForRpibootReturn(found, /*priorDeviceAddress=*/4));
    CHECK(found.load());
}

TEST_CASE("A board still at its old address has not come back yet",
          "[rpiboot][sbr]")
{
    // The reboot has not happened. Treating this as the return would carry
    // on against a board midway through rewriting its own bootloader.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = { device(1, 4, {1, 2}) };
    t.bus.onScan = [&t](int n) { if (n >= 2) t.cancel(); };

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, /*priorDeviceAddress=*/4));
    CHECK_FALSE(found.load());
}

TEST_CASE("A board on another port is not the one that was reprovisioned",
          "[rpiboot][sbr]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = { device(1, 21, {7, 8}) };
    t.bus.onScan = [&t](int n) { if (n >= 2) t.cancel(); };

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
}

TEST_CASE("With no port path nothing is accepted at all", "[rpiboot][sbr]")
{
    // The deliberate difference from the fastboot wait. A board whose fuses
    // are being programmed is not one to guess about, so an enumeration that
    // gave no port path means this path simply cannot proceed -- even with a
    // single device sitting on the bus.
    TestableRpibootThread t{chosenDevice({}), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = { device(1, 21, {}) };
    t.bus.onScan = [&t](int n) { if (n >= 2) t.cancel(); };

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
    CHECK_FALSE(found.load());
}

TEST_CASE("The returning board is found among others on the bus",
          "[rpiboot][sbr]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.bootDevices = {
        device(1, 4, {5, 6}),      // another board, untouched
        device(1, 21, {1, 2}),     // ours, new address
    };

    std::atomic<bool> found{false};
    REQUIRE(t.pollForRpibootReturn(found, 4));
}

TEST_CASE("Cancelling stops the reprovision wait", "[rpiboot][sbr][cancel]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.cancel();

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
    CHECK(t.bus.bootScans == 0);
}

TEST_CASE("A scan that throws does not end the reprovision wait",
          "[rpiboot][sbr]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.bus.throwOnScan = true;
    t.bus.onScan = [&t](int n) { if (n >= 3) t.cancel(); };

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
    CHECK(t.bus.bootScans >= 3);
}

TEST_CASE("A bus that cannot be opened ends the reprovision wait",
          "[rpiboot][sbr]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.busUnavailable = true;

    std::atomic<bool> found{false};
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
}

// ══════════════════════════════════════════════════════════════
// The sequence itself, at the points where it gives up
//
// runPhase() downloads firmware, finds the board, opens it, pushes the
// bootcode, waits for the reboot and serves the second stage. Each of those
// can fail, and what the user is told when one does is the whole of what
// they have to work with -- the board is headless and the imager is the
// only thing that can see it.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Firmware that cannot be obtained is reported to the user",
          "[rpiboot][phase]")
{
    // What an offline user meets. The message has to name the cause, since
    // nothing else on screen will.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir.clear();                    // nothing came back
    t.firmwareError = "network unreachable";

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK(log.errors.last().contains(QStringLiteral("firmware")));
    CHECK(log.errors.last().contains(QStringLiteral("network unreachable")));
}

TEST_CASE("A missing firmware source is reported rather than crashed into",
          "[rpiboot][phase]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.noFirmwareManager = true;

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
    REQUIRE_FALSE(log.errors.isEmpty());
}

TEST_CASE("Cancelling during the firmware step stops the sequence",
          "[rpiboot][phase][cancel]")
{
    // Cancelled after the download but before the bus is touched: the user
    // pressed the button, so nothing should be sent to the board.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.cancel();

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
    CHECK(t.bus.bootScans == 0);
}

TEST_CASE("A device that cannot be opened is reported", "[rpiboot][phase]")
{
    // The board is on the bus but will not open -- no permission on the
    // usbfs node is the usual reason, and it is worth saying so rather than
    // failing silently.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };   // openDevice returns nullptr

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK(log.errors.last().contains(QStringLiteral("open")));
}

TEST_CASE("A bus that cannot be opened ends the phase", "[rpiboot][phase]")
{
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.busUnavailable = true;

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
}
