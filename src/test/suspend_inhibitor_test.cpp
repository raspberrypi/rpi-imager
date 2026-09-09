/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Letting go of the suspend inhibit.
 *
 * While a card is being written the machine must not go to sleep, and the
 * inhibit is held by running the platform's inhibitor tool wrapped around
 * `cat` on a FIFO. Releasing it means closing the write end and waiting for
 * that to unwind.
 *
 * The waiting is the part that has bitten users. Closing the write end is
 * normally enough -- the `cat` sees EOF and exits, the tool exits with it --
 * but not when the write finished before the `cat` reached its open(). A FIFO
 * open for reading blocks until a writer appears, and ours has already gone,
 * so it waits for one that never comes. An unbounded wait here waits with it,
 * and `rpi-imager --cli` then prints that the write succeeded and never
 * returns, which in a provisioning script wedges the pipeline.
 *
 * These drive the real class against tools chosen to behave badly in each of
 * the ways it has to survive: one that ignores the FIFO and lives on, one
 * that ignores being asked to stop, and one that is not there at all. In
 * every case the release has to finish and leave nothing behind.
 *
 * The FIFO lives in /run, which an ordinary user cannot write to, so this
 * runs a probe under `unshare -rm` with a tmpfs mounted over it -- which also
 * keeps the FIFOs out of the real /run, where a concurrent run of the suite
 * would otherwise see them.
 */

#include <catch2/catch_test_macros.hpp>

#include <QProcess>
#include <QString>
#include <QStringList>

namespace {

struct ProbeResult {
    bool ran = false;
    QString out;

    // A KEY=value line, or -1 when it is not there.
    long value(const QString &key) const
    {
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            if (!line.startsWith(key + QLatin1Char('=')))
                continue;
            bool ok = false;
            const long v = line.mid(key.size() + 1).toLong(&ok);
            return ok ? v : -1;
        }
        return -1;
    }
};

bool haveMountNamespaces()
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("true")});
    if (!p.waitForFinished(10000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// Run the probe with the given inhibitor tool and arguments, inside a mount
// namespace whose /run is a tmpfs of its own.
ProbeResult runProbe(const QStringList &toolAndArgs, int timeoutMs = 60000)
{
    ProbeResult r;
    QStringList args{QStringLiteral("-rm"), QStringLiteral("--propagation"),
                     QStringLiteral("private"), QStringLiteral("sh"),
                     QStringLiteral("-c"),
                     QStringLiteral("mount -t tmpfs -o size=1M tmpfs /run "
                                    "&& exec \"$@\""),
                     QStringLiteral("_"),
                     QStringLiteral(SUSPEND_INHIBITOR_PROBE_BINARY)};
    args += toolAndArgs;

    QProcess p;
    p.start(QStringLiteral("unshare"), args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(5000);
        return r;
    }
    r.ran = true;
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    return r;
}

#define REQUIRE_NAMESPACES()                                                   \
    if (!haveMountNamespaces())                                                \
        SKIP("unprivileged mount namespaces are unavailable, so /run cannot "  \
             "be replaced and the FIFO cannot be created")

} // namespace

TEST_CASE("Holding the inhibit creates a FIFO and a process to wait on it",
          "[suspend][inhibit]")
{
    REQUIRE_NAMESPACES();

    // `sh -c 'read x'` reads the FIFO and blocks, which is what the real
    // tools' `cat` does. The class appends "cat <fifo>" of its own, so the
    // shell sees those as $0 and $1 and ignores them.
    const ProbeResult r = runProbe({QStringLiteral("sh"), QStringLiteral("-c"),
                                    QStringLiteral("read x")});
    REQUIRE(r.ran);
    INFO("probe said:\n" << r.out.toStdString());

    // Nothing of ours in a fresh /run, then exactly one FIFO while the
    // inhibit is held.
    CHECK(r.value(QStringLiteral("FIFOS_BEFORE")) == 0);
    CHECK(r.value(QStringLiteral("FIFOS_DURING")) == 1);
    // And a process holding it. Without one nothing is inhibited: the FIFO on
    // its own tells the system nothing.
    CHECK(r.value(QStringLiteral("CHILDREN_DURING")) == 1);
}

TEST_CASE("Releasing the inhibit leaves no FIFO and no process",
          "[suspend][inhibit]")
{
    REQUIRE_NAMESPACES();

    const ProbeResult r = runProbe({QStringLiteral("sh"), QStringLiteral("-c"),
                                    QStringLiteral("read x")});
    REQUIRE(r.ran);
    INFO("probe said:\n" << r.out.toStdString());

    CHECK(r.value(QStringLiteral("FIFOS_AFTER")) == 0);
    // Zero counts zombies too: a child that exited and was never reaped is as
    // much a leak as one still running.
    CHECK(r.value(QStringLiteral("CHILDREN_AFTER")) == 0);
}

TEST_CASE("A tool that never reads the FIFO does not hold the exit open",
          "[suspend][inhibit]")
{
    // The case that hung. The tool is alive and is not waiting on the FIFO at
    // all, so closing the write end tells it nothing. Waiting for it to
    // notice would wait out its whole thirty seconds -- and on the real thing
    // there is no thirty seconds, only a `cat` blocked in open() for ever.
    REQUIRE_NAMESPACES();

    const ProbeResult r = runProbe({QStringLiteral("sh"), QStringLiteral("-c"),
                                    QStringLiteral("sleep 30")});
    REQUIRE(r.ran);
    INFO("probe said:\n" << r.out.toStdString());

    const long ms = r.value(QStringLiteral("CLEANUP_MS"));
    INFO("release took " << ms << "ms");
    REQUIRE(ms >= 0);
    // Well under the thirty seconds the tool would otherwise take, and far
    // enough under that a loaded machine does not make this a coin toss.
    CHECK(ms < 15000);
    CHECK(r.value(QStringLiteral("FIFOS_AFTER")) == 0);
    CHECK(r.value(QStringLiteral("CHILDREN_AFTER")) == 0);
}

TEST_CASE("A tool that ignores being asked to stop is stopped anyway",
          "[suspend][inhibit]")
{
    // Nothing about a suspend inhibitor is worth holding an exit open for, so
    // after asking politely it is killed.
    REQUIRE_NAMESPACES();

    const ProbeResult r = runProbe({QStringLiteral("sh"), QStringLiteral("-c"),
                                    QStringLiteral("trap '' TERM; sleep 30")});
    REQUIRE(r.ran);
    INFO("probe said:\n" << r.out.toStdString());

    const long ms = r.value(QStringLiteral("CLEANUP_MS"));
    INFO("release took " << ms << "ms");
    REQUIRE(ms >= 0);
    CHECK(ms < 15000);
    CHECK(r.value(QStringLiteral("CHILDREN_AFTER")) == 0);
}

TEST_CASE("An inhibitor tool that is not installed leaves nothing behind",
          "[suspend][inhibit]")
{
    // Neither kde-inhibit nor systemd-inhibit is present on every desktop,
    // and the imager asks for both. The child cannot exec, exits, and has to
    // be reaped -- and the FIFO has to go with it.
    REQUIRE_NAMESPACES();

    const ProbeResult r = runProbe({QStringLiteral("rpi-imager-no-such-inhibitor")});
    REQUIRE(r.ran);
    INFO("probe said:\n" << r.out.toStdString());

    CHECK(r.value(QStringLiteral("FIFOS_AFTER")) == 0);
    CHECK(r.value(QStringLiteral("CHILDREN_AFTER")) == 0);
    CHECK(r.value(QStringLiteral("CLEANUP_MS")) < 15000);
}
