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
#include "rpiboot/test/mock_usb_transport.h"
#include "rpiboot/rpiboot_scanner.h"

#include <QCoreApplication>
#include <QStringList>
#include <QFile>
#include <QTemporaryDir>
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
    // Non-null once a test wants the board to actually open. The mock is
    // owned by the test: openDevice() hands out a view, because the thread
    // opens the device more than once and destroys each transport when the
    // step is done.
    rpiboot::testing::MockUsbTransport *transport = nullptr;
    int opens = 0;

    std::vector<UsbDeviceInfo> fastbootDevices;
    std::vector<UsbDeviceInfo> bootDevices;
    int fastbootScans = 0;
    int bootScans = 0;
    bool throwOnScan = false;
    // Opening the board throws rather than returning nothing: a cable pulled
    // between the scan and the open, which libusb reports as an exception.
    bool throwOnOpen = false;
    // Runs the first time the file server reads from the board, so a test can
    // act at a point the phase has definitely reached.
    std::function<void()> onFirstRead;
    // Runs at the top of every scan, so a test can end the poll rather than
    // waiting out the sixty seconds the real one is willing to spend.
    std::function<void(int)> onScan;
};

// Forwards to a transport the test owns. runPhase() holds each transport in
// a local and destroys it when the step ends, so handing out the mock itself
// would be a use-after-free the second time round.
class TransportView : public rpiboot::IUsbTransport
{
public:
    explicit TransportView(rpiboot::testing::MockUsbTransport &t,
                           std::function<void()> *onFirstRead = nullptr)
        : _t(t), _onFirstRead(onFirstRead) {}

    bool controlTransfer(uint8_t rt, uint8_t r, uint16_t v, uint16_t i,
                         std::span<const uint8_t> d, int ms) override
    { return _t.controlTransfer(rt, r, v, i, d, ms); }
    int controlTransferIn(uint8_t rt, uint8_t r, uint16_t v, uint16_t i,
                          std::span<uint8_t> b, int ms) override
    {
        if (_onFirstRead && *_onFirstRead) {
            auto fire = *_onFirstRead;
            *_onFirstRead = nullptr;
            fire();
        }
        return _t.controlTransferIn(rt, r, v, i, b, ms);
    }
    int bulkWrite(uint8_t ep, std::span<const uint8_t> d, int ms) override
    { return _t.bulkWrite(ep, d, ms); }
    int bulkRead(uint8_t ep, std::span<uint8_t> b, int ms) override
    { return _t.bulkRead(ep, b, ms); }
    bool isOpen() const override { return _t.isOpen(); }
    std::string interfaceString() const override { return _t.interfaceString(); }
    uint8_t outEndpoint() const override { return _t.outEndpoint(); }
    uint8_t inEndpoint() const override { return _t.inEndpoint(); }
    QString initDiagnostics() const override { return QStringLiteral("cfg=1 if=0"); }

private:
    rpiboot::testing::MockUsbTransport &_t;
    std::function<void()> *_onFirstRead = nullptr;
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
        ++_bus.opens;
        if (_bus.throwOnOpen)
            throw std::runtime_error("the cable was pulled");
        if (!_bus.transport)
            return nullptr;
        return std::make_unique<TransportView>(*_bus.transport, &_bus.onFirstRead);
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

TEST_CASE("A bus that cannot be opened ends the wait, and says so",
          "[rpiboot][re-enum]")
{
    // Silently returning false here leaves run() with nothing to emit and
    // the wizard waiting on a device that is never coming. Every other way
    // out of this sequence tells the user why.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.busUnavailable = true;

    SignalLog log;
    log.attach(&t);

    rpiboot::UsbDeviceInfo out{};
    CHECK_FALSE(t.waitForBootDeviceReEnum(out));
    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK(log.errors.last().contains(QStringLiteral("USB")));
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

TEST_CASE("A bus that cannot be opened ends the phase, and says so",
          "[rpiboot][phase]")
{
    // The likeliest real cause is a permissions problem on the usbfs node,
    // which is worth naming: there is nothing else on screen to suggest it.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.busUnavailable = true;

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK(log.errors.last().contains(QStringLiteral("permission")));
}

TEST_CASE("A bootcode that will not upload is reported with its diagnostics",
          "[rpiboot][phase]")
{
    // The board opened, so the problem is the firmware rather than the bus.
    // An empty firmware directory is what a half-written cache looks like.
    // The USB init account goes into the message, because by the time
    // anyone reads it the board is usually gone from the bus.
    rpiboot::testing::MockUsbTransport mock;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.bus.transport = &mock;
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    CHECK(t.bus.opens >= 1);
    CHECK(bcDiag == QStringLiteral("cfg=1 if=0"));
    REQUIRE_FALSE(log.errors.isEmpty());
    CHECK(log.errors.last().contains(QStringLiteral("rpiboot protocol failed")));
}

TEST_CASE("The board on the chosen port is the one opened", "[rpiboot][phase]")
{
    // Two boards on the bus. The scan picks by port path, and the one it
    // picks is what gets opened -- reflashing the wrong module would be the
    // whole failure.
    rpiboot::testing::MockUsbTransport mock;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.bus.transport = &mock;
    t.bus.bootDevices = {
        device(9, 9, {7, 8}, 0),      // somebody else's
        device(1, 4, {1, 2}, 0),      // ours
    };

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
    CHECK(t.bus.bootScans == 1);
    CHECK(t.bus.opens == 1);
}

TEST_CASE("Cancelling during the bootcode upload reports nothing to the user",
          "[rpiboot][phase][cancel]")
{
    // A cancelled upload is the user's own doing. It still stops, but it is
    // not an error to put in front of them.
    rpiboot::testing::MockUsbTransport mock;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::temp_directory_path();
    t.bus.transport = &mock;
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };
    t.bus.onScan = [&t](int) { t.cancel(); };

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));
    CHECK(log.errors.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Getting the bootcode across
// ══════════════════════════════════════════════════════════════

namespace {

// A firmware directory with the file the loader will look for. BCM2712 wants
// bootcode5.bin; the contents are never interpreted here, only pushed.
struct FirmwareDir {
    QTemporaryDir dir;

    FirmwareDir()
    {
        REQUIRE(dir.isValid());
        QFile f(dir.filePath(QStringLiteral("bootcode5.bin")));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, '\x5a'));
        f.close();
    }

    std::filesystem::path path() const
    {
        return std::filesystem::path(dir.path().toStdString());
    }
};

} // namespace

TEST_CASE("The bootcode reaches the board", "[rpiboot][phase][bootcode]")
{
    // The upload itself: header then payload, over the transport. What is
    // checked is that the board was written to at all, and that the payload
    // that went out is the one that was on disk -- pushing the wrong bytes
    // at a compute module in ROM mode is not something it recovers from
    // gracefully.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;

    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = fw.path();
    t.bus.transport = &mock;
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };
    // Board goes away after the upload, then never returns: the phase ends
    // at the re-enumeration wait rather than running on into the file
    // server, which wants a whole firmware tree.
    t.bus.onScan = [&t](int n) {
        if (n >= 2) t.bus.bootDevices.clear();
        if (n >= 4) t.cancel();
    };

    QString fbId, bcDiag, fsDiag;
    t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag);

    const auto &writes = mock.capturedBulkWrites();
    INFO("bulk writes: " << writes.size());
    REQUIRE_FALSE(writes.empty());

    // Somewhere in what went out is the 4 KiB of 0x5a from the file.
    size_t payloadBytes = 0;
    for (const auto &w : writes)
        for (uint8_t b : w)
            if (b == 0x5a)
                ++payloadBytes;
    CHECK(payloadBytes >= 4096);
}

TEST_CASE("An empty bootcode file is refused before anything is sent",
          "[rpiboot][phase][bootcode]")
{
    // A truncated download. Pushing a zero-length payload at a board in ROM
    // mode is worse than not trying.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QFile f(dir.filePath(QStringLiteral("bootcode5.bin")));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.close();

    rpiboot::testing::MockUsbTransport mock;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = std::filesystem::path(dir.path().toStdString());
    t.bus.transport = &mock;
    t.bus.bootDevices = { device(1, 4, {1, 2}, 0) };

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    CHECK(mock.capturedBulkWrites().empty());
    REQUIRE_FALSE(log.errors.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Turning boot-mode devices into drive-list entries
//
// The scanner builds a synthetic path, rpiboot://bus:addr:port:pid, and
// RpibootThread reads the board back out of it. The two have to agree, and
// the pid in it is what selects bootcode4.bin over bootcode5.bin -- get
// that wrong and the wrong first-stage goes to the board.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A port path becomes a dotted string", "[rpiboot][scanner]")
{
    CHECK(rpiboot::portPathToString({}) == "");
    CHECK(rpiboot::portPathToString({1}) == "1");
    CHECK(rpiboot::portPathToString({1, 2}) == "1.2");
    CHECK(rpiboot::portPathToString({1, 2, 3, 4}) == "1.2.3.4");
    CHECK(rpiboot::portPathToString({10, 255}) == "10.255");
}

TEST_CASE("A board on the bus becomes a drive-list entry", "[rpiboot][scanner]")
{
    FakeBus bus;
    bus.bootDevices = { device(1, 4, {1, 2}, 0) };
    BusView view(bus);

    const auto found = rpiboot::scanRpibootDevices(view);
    REQUIRE(found.size() == 1);

    const auto &d = found[0];
    INFO("device path: " << d.device);
    CHECK(d.device.rfind("rpiboot://", 0) == 0);
    CHECK(d.device.find("1:4:1.2:") != std::string::npos);

    // What the drive list needs to know about it.
    CHECK(d.isRpiboot);
    CHECK(d.isUSB);
    CHECK(d.isRemovable);
    CHECK_FALSE(d.isSystem);
    CHECK_FALSE(d.isReadOnly);
    CHECK(d.size == 0);          // nothing to write to yet
    CHECK(d.usbPortPath == std::vector<uint8_t>{1, 2});
}

TEST_CASE("A board with no port path still gets an entry", "[rpiboot][scanner]")
{
    FakeBus bus;
    bus.bootDevices = { device(2, 7, {}, 0) };
    BusView view(bus);

    const auto found = rpiboot::scanRpibootDevices(view);
    REQUIRE(found.size() == 1);
    INFO("device path: " << found[0].device);
    CHECK(found[0].device.find("2:7::") != std::string::npos);
}

TEST_CASE("Every board on the bus is listed", "[rpiboot][scanner]")
{
    FakeBus bus;
    bus.bootDevices = {
        device(1, 4, {1, 2}, 0),
        device(1, 5, {1, 3}, 0),
        device(2, 9, {4}, 0),
    };
    BusView view(bus);

    CHECK(rpiboot::scanRpibootDevices(view).size() == 3);
}

TEST_CASE("An empty bus lists nothing", "[rpiboot][scanner]")
{
    FakeBus bus;
    BusView view(bus);
    CHECK(rpiboot::scanRpibootDevices(view).empty());
}

TEST_CASE("A bus that throws mid-scan lists nothing rather than propagating",
          "[rpiboot][scanner]")
{
    // This runs on the drive-list poll thread. An exception escaping here
    // would take out the polling that populates the whole storage picker,
    // not just the rpiboot part of it.
    FakeBus bus;
    bus.throwOnScan = true;
    BusView view(bus);

    std::vector<Drivelist::DeviceDescriptor> found;
    REQUIRE_NOTHROW(found = rpiboot::scanRpibootDevices(view));
    CHECK(found.empty());
}

// ══════════════════════════════════════════════════════════════
// The order the phases run in
//
// Normally one phase. A CM5 being re-provisioned runs two: secure-boot
// recovery to rewrite the bootloader, then fastboot to make it writable.
// Which signal comes out at the end is what the wizard waits on, so a phase
// that failed must produce none of them.
// ══════════════════════════════════════════════════════════════

namespace {

// Records what run() asked for instead of doing it.
class PhaseRecordingThread : public TestableRpibootThread
{
public:
    using TestableRpibootThread::TestableRpibootThread;
    using RpibootThread::run;

    std::vector<rpiboot::SideloadMode> phasesRun;
    int failAfter = -1;              // -1 never; 0 fails the first phase
    bool cancelAfterFirst = false;

protected:
    bool runPhase(rpiboot::SideloadMode mode, QString &fastbootId,
                  QString &, QString &) override
    {
        phasesRun.push_back(mode);
        fastbootId = QStringLiteral("1:9");
        if (cancelAfterFirst && phasesRun.size() == 1)
            cancel();
        if (failAfter >= 0 && static_cast<int>(phasesRun.size()) > failAfter)
            return false;
        return true;
    }
};

struct TerminalLog {
    bool success = false;
    QString readyId;
    bool ready = false;

    void attach(RpibootThread *t)
    {
        QObject::connect(t, &RpibootThread::success, [this] { success = true; });
        QObject::connect(t, &RpibootThread::fastbootDeviceReady,
                         [this](const QString &id) { ready = true; readyId = id; });
    }
};

} // namespace

TEST_CASE("An ordinary fastboot sideload runs one phase and announces the device",
          "[rpiboot][phases]")
{
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    TerminalLog log;
    log.attach(&t);

    t.run();

    REQUIRE(t.phasesRun.size() == 1);
    CHECK(t.phasesRun[0] == rpiboot::SideloadMode::Fastboot);
    CHECK(log.ready);
    CHECK(log.readyId == QStringLiteral("1:9"));
    CHECK_FALSE(log.success);
}

TEST_CASE("A secure-boot recovery on its own reports success rather than a device",
          "[rpiboot][phases]")
{
    // Nothing is written afterwards, so there is no fastboot device to hand
    // on to the writer -- the job was the EEPROM.
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::SecureBootRecovery};
    TerminalLog log;
    log.attach(&t);

    t.run();

    REQUIRE(t.phasesRun.size() == 1);
    CHECK(t.phasesRun[0] == rpiboot::SideloadMode::SecureBootRecovery);
    CHECK(log.success);
    CHECK_FALSE(log.ready);
}

TEST_CASE("Re-provisioning a CM5 runs recovery before fastboot",
          "[rpiboot][phases]")
{
    // The order is the point: the bootloader is rewritten first, and only
    // then is the board brought up in a mode that can be written to.
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.setReprovisionDevice(true);
    TerminalLog log;
    log.attach(&t);

    t.run();

    REQUIRE(t.phasesRun.size() == 2);
    CHECK(t.phasesRun[0] == rpiboot::SideloadMode::SecureBootRecovery);
    CHECK(t.phasesRun[1] == rpiboot::SideloadMode::Fastboot);
    CHECK(log.ready);
}

TEST_CASE("Re-provisioning is only for BCM2712", "[rpiboot][phases]")
{
    // A CM4 asked to re-provision runs the single phase it was given. The
    // two-phase sequence is specific to the chip that has the fuses.
    RpibootThread::DeviceInfo d = chosenDevice();
    d.chipGeneration = rpiboot::ChipGeneration::BCM2711;

    PhaseRecordingThread t{d, rpiboot::SideloadMode::Fastboot};
    t.setReprovisionDevice(true);

    t.run();

    REQUIRE(t.phasesRun.size() == 1);
    CHECK(t.phasesRun[0] == rpiboot::SideloadMode::Fastboot);
}

TEST_CASE("A failed phase announces nothing", "[rpiboot][phases]")
{
    // runPhase has already told the user what went wrong. A success signal
    // on top of that would have the wizard move on from a board that was
    // never made writable.
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.failAfter = 0;
    TerminalLog log;
    log.attach(&t);

    t.run();

    CHECK(t.phasesRun.size() == 1);
    CHECK_FALSE(log.ready);
    CHECK_FALSE(log.success);
}

TEST_CASE("A failed recovery phase does not go on to fastboot",
          "[rpiboot][phases]")
{
    // If the bootloader was not rewritten, bringing the board up to be
    // written to is the wrong next move.
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.setReprovisionDevice(true);
    t.failAfter = 0;
    TerminalLog log;
    log.attach(&t);

    t.run();

    CHECK(t.phasesRun.size() == 1);
    CHECK(t.phasesRun[0] == rpiboot::SideloadMode::SecureBootRecovery);
    CHECK_FALSE(log.ready);
}

TEST_CASE("Cancelling between phases stops before the second",
          "[rpiboot][phases][cancel]")
{
    PhaseRecordingThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.setReprovisionDevice(true);
    t.cancelAfterFirst = true;
    TerminalLog log;
    log.attach(&t);

    t.run();

    CHECK(t.phasesRun.size() == 1);
    CHECK_FALSE(log.ready);
    CHECK_FALSE(log.success);
}

TEST_CASE("The scanner-thread polls stay quiet when the bus will not open",
          "[rpiboot][fastboot-wait][sbr]")
{
    // Deliberately different from the two above. These run on a detached
    // scanner thread whose return value is discarded -- they report through
    // _nextStageFound, and the main flow times out with its own message.
    // An error from here would be a second one for the same failure.
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.busUnavailable = true;

    SignalLog log;
    log.attach(&t);

    std::atomic<bool> found{false};
    QString id;
    CHECK_FALSE(t.pollForFastbootDevice(found, id));
    CHECK_FALSE(t.pollForRpibootReturn(found, 4));
    CHECK(log.errors.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// The file-server phase, after the board has come back
//
// Everything above stops at the bootcode upload. What follows it is the
// half of the sequence a user is most likely to meet a failure in: the
// board has restarted, and the imager has to open it again and serve it the
// firmware it asks for. A board that is already past the bootcode -- a
// serial index other than 0 or 3 -- goes straight there, which is what
// these use to get at it.
//
// Every exit from here matters more than usual because the device is
// headless and mid-provisioning: a phase that returns without saying
// anything leaves the wizard waiting on a board that is not coming.
// ══════════════════════════════════════════════════════════════

namespace {

// A board that has already had its bootcode: serial index 1, so the phase
// skips the upload and the re-enumeration wait and goes to the file server.
UsbDeviceInfo bootedBoard()
{
    return device(1, 4, {1, 2}, 1);
}

} // namespace

TEST_CASE("A board that will not open for the file server is reported",
          "[rpiboot][phase][fileserver]")
{
    // The board answered a scan and then would not open -- claimed by
    // another process, or gone between the two. Silence here is the worst
    // outcome: the board is sitting in rpiboot waiting to be fed.
    FirmwareDir fw;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = fw.path();
    t.bus.bootDevices = { bootedBoard() };
    t.bus.transport = nullptr;   // scans find it; opening it does not work

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(" | ").toStdString());
    CHECK(log.errors.last().contains(QStringLiteral("re-enumeration")));
}

TEST_CASE("A board that vanishes as it is opened is reported as a USB error",
          "[rpiboot][phase][fileserver]")
{
    // The cable pulled between the scan and the open. libusb throws; the
    // phase has to turn that into a sentence rather than let it out of the
    // thread.
    FirmwareDir fw;
    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = fw.path();
    t.bus.bootDevices = { bootedBoard() };
    t.bus.throwOnOpen = true;

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(" | ").toStdString());
    CHECK(log.errors.last().contains(QStringLiteral("USB error")));
    // The reason libusb gave, not just that there was one.
    CHECK(log.errors.last().contains(QStringLiteral("cable was pulled")));
}

TEST_CASE("A file server the board will not talk to is reported with what went wrong",
          "[rpiboot][phase][fileserver]")
{
    // The board opens and then does not answer. The protocol's own account
    // of it is the only diagnostic there is -- a bare "rpiboot failed" gives
    // nobody anything to go on.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;   // answers nothing

    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = fw.path();
    t.bus.bootDevices = { bootedBoard() };
    t.bus.transport = &mock;

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    REQUIRE_FALSE(log.errors.isEmpty());
    INFO("errors: " << log.errors.join(" | ").toStdString());
    CHECK(log.errors.last().contains(QStringLiteral("rpiboot protocol failed")));
    CHECK(log.errors.last().contains(QStringLiteral("File server")));

    // And the phase recorded which USB configuration it was talking over,
    // which is the first thing anyone reading a failed provisioning asks.
    CHECK_FALSE(fsDiag.isEmpty());
}

TEST_CASE("Cancelling during the file server says nothing to the user",
          "[rpiboot][phase][fileserver][cancel]")
{
    // A cancel is the user's own doing. Reporting it back as a failure puts
    // an error on screen for something they asked for.
    FirmwareDir fw;
    rpiboot::testing::MockUsbTransport mock;

    TestableRpibootThread t{chosenDevice(), rpiboot::SideloadMode::Fastboot};
    t.firmwareDir = fw.path();
    t.bus.bootDevices = { bootedBoard() };
    t.bus.transport = &mock;
    // Cancel once the file server has actually started talking to the board,
    // so the phase is inside the step rather than short-circuiting before it.
    t.bus.onFirstRead = [&t] { t.cancel(); };

    SignalLog log;
    log.attach(&t);

    QString fbId, bcDiag, fsDiag;
    CHECK_FALSE(t.runPhase(rpiboot::SideloadMode::Fastboot, fbId, bcDiag, fsDiag));

    INFO("errors: " << log.errors.join(" | ").toStdString());
    CHECK(log.errors.isEmpty());
}
