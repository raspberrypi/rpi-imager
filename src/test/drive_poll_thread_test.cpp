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

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

#include <atomic>

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
