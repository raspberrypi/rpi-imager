/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The watchdog that decides whether a write that has stopped moving is
 * recovered quietly or reported to the user as a failure.
 *
 * This is the component behind the two worst outcomes the writer has. If it
 * fires when it should not, a slow-but-healthy card is torn down mid-write
 * and the user is told their storage is broken. If it fails to fire, the
 * progress bar sits at some percentage forever and nothing ever says why.
 * Neither shows up in a log as an error.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "writeprogresswatchdog.h"
#include "downloadthread.h"
#include "signal_log.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>

#include <atomic>

using rpi_test::SignalLog;

namespace {

// A thread that reports the progress a case asks for and remembers what
// recovery the watchdog attempted. It is constructed but never started, so
// none of DownloadThread's own machinery runs.
class FakeThread : public DownloadThread
{
public:
    FakeThread() : DownloadThread(QByteArray("http://localhost/nothing")) {}

    uint64_t bytesWritten() override { return written.load(); }
    uint64_t dlNow() override { return downloaded.load(); }
    uint64_t verifyNow() override { return verified.load(); }
    int pendingAsyncWrites() const override { return pending.load(); }

    void forcePollAsyncCompletions() override
    {
        ++polls;
        int p = pending.load();
        pending.store(p > pollRecovers ? p - pollRecovers : 0);
    }

    int getAsyncQueueDepth() const override { return depth; }

    bool reduceAsyncQueueDepth(int newDepth) override
    {
        ++reductions;
        lastDepthRequested = newDepth;
        if (!allowReduce)
            return false;
        depth = newDepth;
        return true;
    }

    bool drainAndSwitchToSync(int timeoutSeconds) override
    {
        ++drains;
        drainTimeout = timeoutSeconds;
        if (drainSucceeds)
            pending.store(0);
        return drainSucceeds;
    }

    std::atomic<uint64_t> written{0};
    std::atomic<uint64_t> downloaded{0};
    std::atomic<uint64_t> verified{0};
    std::atomic<int> pending{0};

    int depth = 8;
    int pollRecovers = 0;
    bool allowReduce = true;
    bool drainSucceeds = false;

    int polls = 0;
    int reductions = 0;
    int drains = 0;
    int lastDepthRequested = -1;
    int drainTimeout = -1;
};

// The shipped ladder, in seconds after progress stops:
//
//     30  reduce queue depth
//     60  drain and hot-swap to sync
//     90  stall warning        (half of the 180s timeout)
//    120  recommend a restart
//    180  hard timeout
//
// These keep that order and those relative gaps, in milliseconds. Getting the
// scaling wrong is easy and quietly turns every ordering assertion below into
// a statement about the constants here rather than about the shipped ones.
class FastWatchdog : public WriteProgressWatchdog
{
public:
    FastWatchdog()
    {
        CHECK_INTERVAL_MS = 10;
        REDUCE_DEPTH_THRESHOLD_MS = 100;   // drain follows at 2x this
        RESTART_THRESHOLD_MS = 400;
        STALL_TIMEOUT_MS = 600;            // warning at half: 300
        ASYNC_TIMEOUT_MS = 600;            // shipped: equal to STALL
        DRAIN_STALL_TIMEOUT_SECONDS = 1;
    }

    // The shipped normal and async timeouts are both 180s, so the branch in
    // getEffectiveTimeoutMs() has no observable effect until they diverge.
    void setAsyncTimeout(int ms) { ASYNC_TIMEOUT_MS = ms; }
    void setRestartThreshold(int ms) { RESTART_THRESHOLD_MS = ms; }
};

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("write_progress_watchdog_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}

// ── Starting and stopping ────────────────────────

TEST_CASE("A watchdog that was never started is not running", "[watchdog]")
{
    FastWatchdog dog;
    CHECK_FALSE(dog.isRunning());
}

TEST_CASE("Stopping a watchdog that never started is harmless", "[watchdog]")
{
    FastWatchdog dog;
    dog.stop();
    CHECK_FALSE(dog.isRunning());
}

TEST_CASE("Monitoring runs between start and stop", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;

    dog.start(&thread);
    CHECK(dog.isRunning());

    dog.stop();
    CHECK_FALSE(dog.isRunning());
}

// ── What counts as progress ──────────────────────
//
// Any one of these moving is enough. Getting this wrong in either direction
// is a user-visible failure: too strict and a healthy write is aborted, too
// lax and a stuck write is never noticed.

TEST_CASE("Bytes written count as progress", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    QTimer feed;
    QObject::connect(&feed, &QTimer::timeout, [&] { thread.written += 4096; });
    feed.start(20);

    dog.start(&thread);
    spin(900);

    CHECK(timedOut.isEmpty());
    CHECK(dog.isRunning());
}

TEST_CASE("Bytes downloaded count as progress", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    QTimer feed;
    QObject::connect(&feed, &QTimer::timeout, [&] { thread.downloaded += 4096; });
    feed.start(20);

    dog.start(&thread);
    spin(900);

    CHECK(timedOut.isEmpty());
}

TEST_CASE("Bytes verified count as progress", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    QTimer feed;
    QObject::connect(&feed, &QTimer::timeout, [&] { thread.verified += 4096; });
    feed.start(20);

    dog.start(&thread);
    spin(900);

    // Verification is the last phase of a write and moves no other counter.
    // If it did not count, every verified image would end in a stall error.
    CHECK(timedOut.isEmpty());
}

TEST_CASE("A falling pending-write count is progress", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(500);
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    QTimer feed;
    QObject::connect(&feed, &QTimer::timeout, [&] {
        int p = thread.pending.load();
        if (p > 0) thread.pending.store(p - 1);
    });
    feed.start(20);

    dog.start(&thread);
    spin(900);

    // A device draining its queue writes no new bytes while it does so.
    CHECK(timedOut.isEmpty());
}

// ── The two timeouts ─────────────────────────────

TEST_CASE("A stall with no writes pending times out at the shorter limit", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(900);   // past STALL_TIMEOUT_MS, well short of ASYNC_TIMEOUT_MS

    REQUIRE(timedOut.count() == 1);
    CHECK_FALSE(timedOut.at(0).at(0).toString().isEmpty());
    // The watchdog stops itself; nothing else is going to.
    CHECK_FALSE(dog.isRunning());
}

TEST_CASE("A stall with writes pending is given the longer limit", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.depth = 2;                 // no queue-depth rung
    dog.setAsyncTimeout(1400);        // diverge the two timeouts
    dog.setRestartThreshold(9000);    // and keep the restart rung out of it
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(900);

    // Same elapsed time as the case above, opposite outcome: a device with
    // writes outstanding is still draining and gets longer.
    CHECK(timedOut.isEmpty());
}

TEST_CASE("The hard timeout fires only once", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(1200);

    CHECK(timedOut.count() == 1);
}

// ── Recovery phase 1: polling ────────────────────

TEST_CASE("Polling that retrieves completions holds off the timeout", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(10000);
    thread.pollRecovers = 1;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);

    dog.start(&thread);
    spin(900);

    CHECK(thread.polls > 0);
    CHECK(timedOut.isEmpty());
    CHECK(restart.isEmpty());
    CHECK(thread.drains == 0);
}

TEST_CASE("Polling that retrieves nothing does not hold off escalation", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.pollRecovers = 0;
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);

    dog.start(&thread);
    spin(600);

    CHECK(thread.polls > 0);
    CHECK(restart.count() == 1);
}

TEST_CASE("A device with no writes pending is not polled", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;

    dog.start(&thread);
    spin(300);

    CHECK(thread.polls == 0);
}

// ── Recovery phase 2: queue depth ────────────────

TEST_CASE("Queue depth is halved once when progress stops", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.depth = 8;

    dog.start(&thread);
    spin(180);   // past REDUCE_DEPTH_THRESHOLD_MS, short of the drain at 2x

    CHECK(thread.reductions == 1);
    CHECK(thread.lastDepthRequested == 4);
    CHECK(thread.drains == 0);
}

TEST_CASE("Queue depth is not reduced before the threshold", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);

    dog.start(&thread);
    spin(60);

    CHECK(thread.reductions == 0);
}

TEST_CASE("A queue already at minimum depth is left alone", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.depth = 2;

    dog.start(&thread);
    spin(180);

    CHECK(thread.reductions == 0);
}

TEST_CASE("Depth reduction never asks for fewer than two", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.depth = 3;

    dog.start(&thread);
    spin(180);

    REQUIRE(thread.reductions == 1);
    CHECK(thread.lastDepthRequested == 2);
}

TEST_CASE("A refused depth reduction is retried", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.allowReduce = false;

    dog.start(&thread);
    spin(180);

    // The attempt is only remembered when the thread accepts it, so a backend
    // that cannot reduce depth is asked again rather than skipped forever.
    CHECK(thread.reductions > 1);
}

// ── Recovery phase 3: drain and hot-swap ─────────

TEST_CASE("A successful drain hot-swaps to sync instead of restarting", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.drainSucceeds = true;
    SignalLog swapped(&dog, &WriteProgressWatchdog::switchedToSyncMode);
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);

    dog.start(&thread);
    spin(600);

    REQUIRE(swapped.count() == 1);
    CHECK_FALSE(swapped.at(0).at(0).toString().isEmpty());
    // The whole point of the hot-swap: the write keeps its progress.
    CHECK(restart.isEmpty());
    CHECK(dog.isRunning());
}

TEST_CASE("The drain is given the configured per-completion timeout", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.drainSucceeds = true;

    dog.start(&thread);
    spin(400);

    REQUIRE(thread.drains == 1);
    CHECK(thread.drainTimeout == 1);
}

TEST_CASE("The drain is attempted only once", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.drainSucceeds = false;

    dog.start(&thread);
    spin(600);

    CHECK(thread.drains == 1);
}

// ── Recovery phase 4: restart ────────────────────

TEST_CASE("A failed drain escalates to a restart", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.drainSucceeds = false;
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(600);

    REQUIRE(restart.count() == 1);
    CHECK_FALSE(restart.at(0).at(0).toString().isEmpty());
    // Restart supersedes the hard timeout; the user gets one message, not two.
    CHECK(timedOut.isEmpty());
    CHECK_FALSE(dog.isRunning());
}

TEST_CASE("A restart is recommended only once", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);

    dog.start(&thread);
    spin(900);

    CHECK(restart.count() == 1);
}

TEST_CASE("A stall with no writes pending is not restartable", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(900);

    // Nothing is outstanding, so there is nothing a restart would drain --
    // the user is told instead.
    CHECK(restart.isEmpty());
    CHECK(timedOut.count() == 1);
}

// ── Warnings ─────────────────────────────────────

TEST_CASE("A stall warning precedes the timeout", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog warned(&dog, &WriteProgressWatchdog::stallWarning);

    dog.start(&thread);
    spin(500);   // past half of STALL_TIMEOUT_MS, short of all of it

    REQUIRE(warned.count() > 0);
    CHECK(warned.at(0).at(1).toInt() == 0);   // pending writes
}

TEST_CASE("The stall warning reports the pending write count", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(7);
    thread.depth = 2;          // keep the queue-depth rung out of the way
    SignalLog warned(&dog, &WriteProgressWatchdog::stallWarning);

    dog.start(&thread);
    spin(350);

    REQUIRE(warned.count() > 0);
    CHECK(warned.at(0).at(1).toInt() == 7);
}

TEST_CASE("The warning arrives before the restart is recommended", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(7);
    thread.depth = 2;
    SignalLog warned(&dog, &WriteProgressWatchdog::stallWarning);
    SignalLog restart(&dog, &WriteProgressWatchdog::restartNeeded);

    dog.start(&thread);
    spin(350);

    // Warning at half the timeout (90s shipped), restart at 120s. The user
    // sees the write described as struggling before anything is torn down.
    CHECK(warned.count() > 0);
    REQUIRE(restart.isEmpty());

    spin(250);
    CHECK(restart.count() == 1);
}

// ── Stopping mid-stall ───────────────────────────

TEST_CASE("Stopping a stalled watchdog silences it", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    SignalLog timedOut(&dog, &WriteProgressWatchdog::hardTimeout);

    dog.start(&thread);
    spin(200);
    dog.stop();
    spin(900);

    // Cancelling a write must not produce a stall error after the fact.
    CHECK(timedOut.isEmpty());
}

TEST_CASE("Restarting monitoring clears the previous recovery state", "[watchdog]")
{
    FakeThread thread;
    FastWatchdog dog;
    thread.pending.store(4);
    thread.drainSucceeds = false;

    dog.start(&thread);
    spin(600);
    REQUIRE(thread.drains == 1);

    dog.start(&thread);
    spin(600);

    // A second write must get the full recovery ladder, not the leftovers of
    // the first one's.
    CHECK(thread.drains == 2);
}
