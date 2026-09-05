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

#include <QCoreApplication>
#include <QStandardPaths>

#include <atomic>
#include <functional>
#include <stdexcept>

using rpiboot::UsbDeviceInfo;

namespace {

UsbDeviceInfo device(uint8_t bus, uint8_t addr, std::vector<uint8_t> port)
{
    UsbDeviceInfo d{};
    d.busNumber = bus;
    d.deviceAddress = addr;
    d.portPath = std::move(port);
    d.chipGeneration = rpiboot::ChipGeneration::BCM2712;
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

class TestableRpibootThread : public RpibootThread
{
public:
    using RpibootThread::RpibootThread;
    using RpibootThread::pollForFastbootDevice;

    FakeBus bus;
    bool busUnavailable = false;

protected:
    std::unique_ptr<rpiboot::IUsbContext> makeUsbContext() override
    {
        if (busUnavailable)
            return nullptr;
        return std::make_unique<BusView>(bus);
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
