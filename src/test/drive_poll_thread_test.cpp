/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The thread that keeps the drive chooser up to date.
 *
 * It polls once a second normally, once every five seconds in slow mode, and
 * not at all while paused -- the imager pauses it during a write so the scan
 * does not contend with the device it is writing to.
 *
 * The failures are all things a user reports as the application misbehaving
 * rather than as an error. A thread that does not resume after a write leaves
 * the chooser frozen on a stale list, so the card they just wrote never
 * reappears and one they unplugged is still offered. A thread that does not
 * pause competes with the write for the same device. A thread that will not
 * stop hangs the application on quit.
 *
 * Nothing here depends on what is plugged into the machine: the assertions
 * are about when the list is delivered, not what is in it.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "drivelistmodelpollthread.h"
#include "drivelist/drivelist.h"
#include "rpiboot/libusb_transport.h"
#include "rpiboot/rpiboot_types.h"
#include "rpiboot/test/mock_usb_transport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace {

using ScanMode = DriveListModelPollThread::ScanMode;

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Counts drive lists as they arrive. The thread emits from its own thread, so
// the connection is bound to a main-thread context and delivered queued.
class ListCounter : public QObject
{
public:
    explicit ListCounter(DriveListModelPollThread *thread)
    {
        QObject::connect(thread, &DriveListModelPollThread::newDriveList, this,
                         [this](std::vector<Drivelist::DeviceDescriptor>) { ++_count; });
    }
    int count() const { return _count; }
    void reset() { _count = 0; }

private:
    int _count = 0;
};

class ModeLog : public QObject
{
public:
    explicit ModeLog(DriveListModelPollThread *thread)
    {
        QObject::connect(thread, &DriveListModelPollThread::scanModeChanged, this,
                         [this](ScanMode m) { _modes.push_back(m); });
    }
    int count() const { return int(_modes.size()); }
    ScanMode at(int i) const { return _modes.at(size_t(i)); }

private:
    std::vector<ScanMode> _modes;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("A poll thread starts in normal mode", "[drivepoll]")
{
    DriveListModelPollThread t;
    CHECK(t.scanMode() == ScanMode::Normal);
    CHECK_FALSE(t.isRunning());
}

TEST_CASE("A change of scan mode is announced once", "[drivepoll]")
{
    DriveListModelPollThread t;
    ModeLog log(&t);

    t.setScanMode(ScanMode::Slow);
    spin(50);
    REQUIRE(log.count() == 1);
    CHECK(log.at(0) == ScanMode::Slow);

    // Setting the mode it is already in is not a change; announcing it would
    // have the model rebuild for nothing.
    t.setScanMode(ScanMode::Slow);
    spin(50);
    CHECK(log.count() == 1);
}

TEST_CASE("Pausing and resuming move between the two modes", "[drivepoll]")
{
    DriveListModelPollThread t;

    t.pause();
    CHECK(t.scanMode() == ScanMode::Paused);

    t.resume();
    CHECK(t.scanMode() == ScanMode::Normal);
}

TEST_CASE("A running thread reports the drive list", "[drivepoll]")
{
    DriveListModelPollThread t;
    ListCounter counter(&t);

    t.start();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    t.stop();
    REQUIRE(t.wait(5000));

    // Without this the chooser is empty for as long as the application runs.
    CHECK(counter.count() > 0);
}

TEST_CASE("A paused thread stops reporting", "[drivepoll]")
{
    DriveListModelPollThread t;
    ListCounter counter(&t);

    t.start();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    REQUIRE(counter.count() > 0);

    // This is what happens for the duration of a write.
    t.pause();
    spin(500);          // let any scan already under way finish
    counter.reset();
    spin(2500);
    CHECK(counter.count() == 0);

    t.stop();
    REQUIRE(t.wait(5000));
}

TEST_CASE("Resuming after a pause delivers again", "[drivepoll]")
{
    DriveListModelPollThread t;
    ListCounter counter(&t);

    t.start();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    REQUIRE(counter.count() > 0);

    t.pause();
    spin(500);
    counter.reset();

    // A thread that does not wake leaves the chooser frozen on the list from
    // before the write -- the card just written never reappears.
    t.resume();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    CHECK(counter.count() > 0);

    t.stop();
    REQUIRE(t.wait(5000));
}

TEST_CASE("Slow mode polls less often than normal", "[drivepoll]")
{
    DriveListModelPollThread normal;
    ListCounter normalCount(&normal);
    normal.start();
    spin(4500);
    normal.stop();
    REQUIRE(normal.wait(5000));

    DriveListModelPollThread slow;
    slow.setScanMode(ScanMode::Slow);
    ListCounter slowCount(&slow);
    slow.start();
    spin(4500);
    slow.stop();
    REQUIRE(slow.wait(5000));

    // Slow mode exists to stop the scan competing with a write for the bus.
    INFO("normal: " << normalCount.count() << ", slow: " << slowCount.count());
    CHECK(normalCount.count() > slowCount.count());
}

TEST_CASE("A stopped thread finishes promptly", "[drivepoll]")
{
    DriveListModelPollThread t;
    t.start();
    spin(200);

    QElapsedTimer timer;
    timer.start();
    t.stop();
    REQUIRE(t.wait(10000));

    // Quitting must not wait out a whole poll interval, let alone hang.
    INFO("stop took " << timer.elapsed() << "ms");
    CHECK(timer.elapsed() < 8000);
}

TEST_CASE("Destroying a paused thread does not hang", "[drivepoll]")
{
    QElapsedTimer timer;
    timer.start();
    {
        DriveListModelPollThread t;
        t.start();
        spin(200);
        t.pause();
        spin(200);
        // The destructor has to wake the condition variable the paused loop
        // is blocked on, or teardown waits for the 2s fallback and then
        // terminates the thread outright.
    }
    INFO("teardown took " << timer.elapsed() << "ms");
    CHECK(timer.elapsed() < 5000);
}

TEST_CASE("A thread can be restarted after being stopped", "[drivepoll]")
{
    DriveListModelPollThread t;
    ListCounter counter(&t);

    t.start();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    t.stop();
    REQUIRE(t.wait(5000));
    REQUIRE(counter.count() > 0);

    // start() clears the terminate flag; without that the second run returns
    // immediately and the chooser never updates again.
    counter.reset();
    t.start();
    for (int i = 0; i < 60 && counter.count() == 0; ++i)
        spin(100);
    CHECK(counter.count() > 0);

    t.stop();
    REQUIRE(t.wait(5000));
}

// ══════════════════════════════════════════════════════════════
// Which fastboot devices are offered as somewhere to write
//
// The Pi's fastboot gadget borrows Google's 18d1:4e40, so an Android phone
// left in fastboot mode enumerates identically. identifyRpiFastboot() is
// the only thing between that phone and the storage picker, and a user who
// picks it writes a Raspberry Pi OS image over their phone's storage.
//
// The three answers are treated differently on purpose. A confirmed Pi is
// queried for storage and listed. A confirmed non-Pi is banked, so it is
// neither listed nor probed again every tick. A device that did not answer
// is left open and retried, because a Pi whose gadget is still coming up
// looks exactly like one that is not there.
// ══════════════════════════════════════════════════════════════

namespace {

rpiboot::UsbDeviceInfo fbDevice(uint8_t bus, uint8_t addr, std::vector<uint8_t> port)
{
    rpiboot::UsbDeviceInfo d{};
    d.busNumber = bus;
    d.deviceAddress = addr;
    d.portPath = std::move(port);
    return d;
}

// A bus the test describes, with one transport behind every device on it.
struct FakeFastbootBus {
    std::vector<rpiboot::UsbDeviceInfo> devices;
    rpiboot::testing::MockUsbTransport *transport = nullptr;
    int scans = 0;
    int opens = 0;
};

class FbTransportView : public rpiboot::IUsbTransport
{
public:
    explicit FbTransportView(rpiboot::testing::MockUsbTransport &t) : _t(t) {}
    bool controlTransfer(uint8_t rt, uint8_t r, uint16_t v, uint16_t i,
                         std::span<const uint8_t> d, int ms) override
    { return _t.controlTransfer(rt, r, v, i, d, ms); }
    int controlTransferIn(uint8_t rt, uint8_t r, uint16_t v, uint16_t i,
                          std::span<uint8_t> b, int ms) override
    { return _t.controlTransferIn(rt, r, v, i, b, ms); }
    int bulkWrite(uint8_t ep, std::span<const uint8_t> d, int ms) override
    { return _t.bulkWrite(ep, d, ms); }
    int bulkRead(uint8_t ep, std::span<uint8_t> b, int ms) override
    { return _t.bulkRead(ep, b, ms); }
    bool isOpen() const override { return _t.isOpen(); }
    std::string interfaceString() const override { return _t.interfaceString(); }
    uint8_t outEndpoint() const override { return _t.outEndpoint(); }
    uint8_t inEndpoint() const override { return _t.inEndpoint(); }
private:
    rpiboot::testing::MockUsbTransport &_t;
};

class FbBusView : public rpiboot::IUsbContext
{
public:
    explicit FbBusView(FakeFastbootBus &bus) : _bus(bus) {}
    std::vector<rpiboot::UsbDeviceInfo> scanBootDevices() const override { return {}; }
    std::vector<rpiboot::UsbDeviceInfo> scanFastbootDevices() const override
    {
        ++_bus.scans;
        return _bus.devices;
    }
    std::unique_ptr<rpiboot::IUsbTransport> openDevice(
        const rpiboot::UsbDeviceInfo &) const override
    {
        ++_bus.opens;
        if (!_bus.transport)
            return nullptr;
        return std::make_unique<FbTransportView>(*_bus.transport);
    }
private:
    FakeFastbootBus &_bus;
};

class ScannablePollThread : public DriveListModelPollThread
{
public:
    using DriveListModelPollThread::appendFastbootDevices;

    FakeFastbootBus bus;

protected:
    std::unique_ptr<rpiboot::IUsbContext> makeUsbContext() override
    {
        return std::make_unique<FbBusView>(bus);
    }
};

// Count the fastboot entries a scan produced.
int fastbootEntries(const std::vector<Drivelist::DeviceDescriptor> &list)
{
    int n = 0;
    for (const auto &d : list)
        if (d.device.rfind("fastboot://", 0) == 0)
            ++n;
    return n;
}

} // namespace

TEST_CASE("A device that is not a Pi is not offered as a write target",
          "[drivepoll][fastboot]")
{
    // A phone in fastboot mode: right VID and PID, wrong interface string,
    // and it fails the RPi-specific getvar.
    rpiboot::testing::MockUsbTransport phone;
    phone.setInterfaceString("Android Fastboot");
    phone.queueBulkReadResponse({'F', 'A', 'I', 'L'});

    ScannablePollThread t;
    t.bus.transport = &phone;
    t.bus.devices = { fbDevice(1, 4, {1, 2}) };

    std::vector<Drivelist::DeviceDescriptor> list;
    t.appendFastbootDevices(list);

    CHECK(fastbootEntries(list) == 0);
}

TEST_CASE("A device that is not a Pi is not probed again",
          "[drivepoll][fastboot]")
{
    // Banked after the first answer. Re-probing a phone on every tick would
    // hammer somebody else's device several times a second.
    rpiboot::testing::MockUsbTransport phone;
    phone.setInterfaceString("Android Fastboot");
    phone.queueBulkReadResponse({'F', 'A', 'I', 'L'});

    ScannablePollThread t;
    t.bus.transport = &phone;
    t.bus.devices = { fbDevice(1, 4, {1, 2}) };

    std::vector<Drivelist::DeviceDescriptor> first, second;
    t.appendFastbootDevices(first);
    const int opensAfterFirst = t.bus.opens;
    t.appendFastbootDevices(second);

    CHECK(t.bus.opens == opensAfterFirst);
    CHECK(fastbootEntries(second) == 0);
}

TEST_CASE("A device that does not answer is asked again",
          "[drivepoll][fastboot]")
{
    // A Pi whose gadget is still coming up looks exactly like a device that
    // is not there. Banking a "no" would leave the board unusable until the
    // application is restarted.
    rpiboot::testing::MockUsbTransport quiet;
    quiet.setInterfaceString("");           // nothing to go on
    // No queued response: the read fails, which is a transport error.

    ScannablePollThread t;
    t.bus.transport = &quiet;
    t.bus.devices = { fbDevice(1, 4, {1, 2}) };

    std::vector<Drivelist::DeviceDescriptor> first, second;
    t.appendFastbootDevices(first);
    const int opensAfterFirst = t.bus.opens;
    t.appendFastbootDevices(second);

    CHECK(t.bus.opens > opensAfterFirst);
    CHECK(fastbootEntries(first) == 0);
}

TEST_CASE("A device that cannot be opened is skipped", "[drivepoll][fastboot]")
{
    ScannablePollThread t;
    t.bus.transport = nullptr;              // openDevice returns nothing
    t.bus.devices = { fbDevice(1, 4, {1, 2}) };

    std::vector<Drivelist::DeviceDescriptor> list;
    REQUIRE_NOTHROW(t.appendFastbootDevices(list));
    CHECK(fastbootEntries(list) == 0);
}

TEST_CASE("An empty bus adds nothing and disturbs nothing",
          "[drivepoll][fastboot]")
{
    ScannablePollThread t;

    std::vector<Drivelist::DeviceDescriptor> list;
    Drivelist::DeviceDescriptor existing;
    existing.device = "/dev/sda";
    list.push_back(existing);

    t.appendFastbootDevices(list);

    CHECK(list.size() == 1);
    CHECK(list[0].device == "/dev/sda");
    CHECK(t.bus.scans == 1);
}
