// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// PerformanceStats is the imager's telemetry recorder: it tracks a session,
// timestamps events, samples throughput per phase and renders the lot as JSON.
// None of it touches a device or the network, and all of it was uncovered.
//
// The parts worth pinning down are the ones with a decision in them rather
// than the plain accessors: how endSession() classifies an outcome from an
// error string, the sample rate limiter, the per-phase sample cap, the
// logarithmic throughput buckets, and what survives into the exported JSON.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "performancestats.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <QUuid>

#include <limits>

using Catch::Matchers::ContainsSubstring;

namespace {

// A scratch directory that removes itself, so two runs of the suite in
// parallel cannot collide and a failing case leaves nothing behind.
class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-perfstats-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }

    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    QString filePath(const QString &name) const { return QDir(_path).filePath(name); }

private:
    QString _path;
};

// Start a session and push it through one download/write cycle, which is the
// shape almost every assertion below wants to start from.
void runShortCycle(PerformanceStats &stats)
{
    stats.startSession(QStringLiteral("raspios.img.xz"), 4ull * 1024 * 1024 * 1024,
                       QStringLiteral("/dev/sdz"));
    stats.recordDownloadProgress(1024 * 1024, 4ull * 1024 * 1024 * 1024);
    stats.recordWriteProgress(2048 * 1024, 4ull * 1024 * 1024 * 1024);
}

QJsonObject exportedSummary(const PerformanceStats &stats)
{
    return stats.exportToJson().object().value(QStringLiteral("summary")).toObject();
}

} // namespace

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceStats starts out empty", "[perfstats]")
{
    PerformanceStats stats;

    CHECK(stats.sessionState() == PerformanceStats::SessionState::NeverStarted);
    CHECK_FALSE(stats.isSessionActive());
    CHECK_FALSE(stats.hasData());
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Idle);
}

TEST_CASE("PerformanceStats startSession marks the session in progress", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    CHECK(stats.sessionState() == PerformanceStats::SessionState::InProgress);
    CHECK(stats.isSessionActive());
    // startSession emits a CycleStart event, so there is data immediately.
    CHECK(stats.hasData());
}

TEST_CASE("PerformanceStats endSession records success", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));
    stats.endSession(true);

    CHECK(stats.sessionState() == PerformanceStats::SessionState::Succeeded);
    CHECK_FALSE(stats.isSessionActive());
}

// The interesting half of endSession(): it infers "the user stopped this" from
// the error text rather than being told, so the substrings it looks for are
// load-bearing.
TEST_CASE("PerformanceStats tells a cancellation apart from a failure", "[perfstats]")
{
    struct Case {
        const char *error;
        PerformanceStats::SessionState expected;
    };

    const Case cases[] = {
        {"Cancelled by user", PerformanceStats::SessionState::Cancelled},
        {"cancel requested", PerformanceStats::SessionState::Cancelled},
        {"Device was removed", PerformanceStats::SessionState::Cancelled},
        {"Aborted by user", PerformanceStats::SessionState::Cancelled},
        {"Input/output error", PerformanceStats::SessionState::Failed},
        {"SHA256 mismatch", PerformanceStats::SessionState::Failed},
        {"", PerformanceStats::SessionState::Failed},
    };

    for (const auto &c : cases) {
        PerformanceStats stats;
        stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));
        stats.endSession(false, QString::fromUtf8(c.error));

        INFO("error text: " << c.error);
        CHECK(stats.sessionState() == c.expected);
    }
}

TEST_CASE("PerformanceStats endSession without a session is a no-op", "[perfstats]")
{
    PerformanceStats stats;
    stats.endSession(true);

    CHECK(stats.sessionState() == PerformanceStats::SessionState::NeverStarted);
    CHECK_FALSE(stats.hasData());
}

TEST_CASE("PerformanceStats reset clears everything", "[perfstats]")
{
    PerformanceStats stats;
    runShortCycle(stats);
    stats.endSession(true);
    REQUIRE(stats.hasData());

    stats.reset();

    CHECK_FALSE(stats.hasData());
    CHECK_FALSE(stats.isSessionActive());
    CHECK(stats.sessionState() == PerformanceStats::SessionState::NeverStarted);
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Idle);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceStats pairs beginEvent with endEvent", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    const int id = stats.beginEvent(PerformanceStats::EventType::CacheLookup,
                                    QStringLiteral("looking for raspios"));
    CHECK(id >= 0);
    stats.endEvent(id, true, QStringLiteral(" -> hit"));

    CHECK(stats.hasData());
}

TEST_CASE("PerformanceStats endEvent with an unknown id does not fall over", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    // Nothing has begun, so no id is valid. This must be survivable: callers
    // end events on error paths where the begin may never have run.
    stats.endEvent(-1, false);
    stats.endEvent(99999, true);

    CHECK(stats.isSessionActive());
}

TEST_CASE("PerformanceStats records one-shot and transfer events", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    stats.recordEvent(PerformanceStats::EventType::DriveUnmount, 42, true,
                      QStringLiteral("2 partitions"));
    stats.recordEvent(PerformanceStats::EventType::DirectIOAttempt, 1, false,
                      QStringLiteral("EINVAL"));
    stats.recordTransferEvent(PerformanceStats::EventType::CacheWrite, 250,
                              8ull * 1024 * 1024, true);

    CHECK(stats.hasData());

    const QJsonObject root = stats.exportToJson().object();
    REQUIRE(root.contains(QStringLiteral("events")));
    CHECK(root.value(QStringLiteral("events")).toArray().size() >= 3);
}

TEST_CASE("PerformanceStats addEvent accepts a prebuilt event", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    PerformanceStats::TimedEvent event;
    event.type = PerformanceStats::EventType::HashComputation;
    event.startMs = 5;
    event.durationMs = 120;
    event.metadata = QStringLiteral("sha256");
    event.success = true;
    event.bytesTransferred = 1024;
    stats.addEvent(event);

    CHECK(stats.hasData());
}

// ---------------------------------------------------------------------------
// Phases and sampling
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceStats follows the phase through a cycle", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));

    stats.recordDownloadProgress(10, 1024);
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Downloading);

    stats.recordDecompressProgress(20, 1024);
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Decompressing);

    stats.recordWriteProgress(30, 1024);
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Writing);

    stats.recordVerifyProgress(40, 1024);
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Verifying);

    stats.recordFinalising();
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Finalising);
}

TEST_CASE("PerformanceStats ignores progress outside a session", "[perfstats]")
{
    PerformanceStats stats;

    stats.recordDownloadProgress(10, 1024);
    stats.recordWriteProgress(20, 1024);

    CHECK_FALSE(stats.hasData());
    CHECK(stats.currentPhase() == PerformanceStats::Phase::Idle);
}

// The rate limiter is the reason a 2GB write does not produce a million
// samples. A burst inside one interval must collapse to a single sample.
//
// Note the first-sample behaviour this pins down: a phase transition resets
// the phase's clock to 0, so a sample taken less than MIN_SAMPLE_INTERVAL_MS
// into the *session* is dropped outright -- the phase does not get a free
// first sample. Anything under 100ms of a session records nothing at all.
TEST_CASE("PerformanceStats rate-limits samples within a phase", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024 * 1024, QStringLiteral("/dev/null"));

    // Past the interval, so this one lands.
    QThread::msleep(140);
    stats.recordWriteProgress(1ull * 1024 * 1024, 512ull * 1024 * 1024);

    // A burst inside the same interval: every one of these is dropped.
    for (int i = 0; i < 200; ++i)
        stats.recordWriteProgress((2ull + i) * 1024 * 1024, 512ull * 1024 * 1024);

    QThread::msleep(140);
    stats.recordWriteProgress(64ull * 1024 * 1024, 512ull * 1024 * 1024);

    const QJsonArray write = stats.exportToJson()
                                 .object()
                                 .value(QStringLiteral("histograms"))
                                 .toObject()
                                 .value(QStringLiteral("write"))
                                 .toArray();

    // Two surviving samples make exactly one throughput window.
    REQUIRE(write.size() == 1);

    // [timestampMs, min, max, avg, bucket0..bucket11]
    const QJsonArray slice = write.at(0).toArray();
    REQUIRE(slice.size() == 16);

    const qint64 avgKBps = slice.at(3).toInteger();
    CHECK(avgKBps > 0);

    // Exactly one throughput reading was derived, so exactly one bucket is
    // populated -- proof the burst collapsed rather than being recorded.
    int populated = 0;
    for (int i = 4; i < slice.size(); ++i)
        populated += slice.at(i).toInt();
    CHECK(populated == 1);
}

TEST_CASE("PerformanceStats keeps samples for each phase apart", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024 * 1024, QStringLiteral("/dev/null"));

    // Each phase needs two samples past the rate limiter before it can yield
    // a throughput window of its own.
    const quint64 total = 512ull * 1024 * 1024;
    for (int round = 1; round <= 2; ++round) {
        QThread::msleep(140);
        stats.recordDownloadProgress(static_cast<quint64>(round) * 8 * 1024 * 1024, total);
        QThread::msleep(140);
        stats.recordDecompressProgress(static_cast<quint64>(round) * 16 * 1024 * 1024, total);
        QThread::msleep(140);
        stats.recordWriteProgress(static_cast<quint64>(round) * 32 * 1024 * 1024, total);
        QThread::msleep(140);
        stats.recordVerifyProgress(static_cast<quint64>(round) * 64 * 1024 * 1024, total);
    }

    const QJsonObject histograms =
        stats.exportToJson().object().value(QStringLiteral("histograms")).toObject();

    for (const auto &phase : {"download", "decompress", "write", "verify"}) {
        INFO("phase: " << phase);
        const QString key = QString::fromUtf8(phase);
        REQUIRE(histograms.contains(key));
        CHECK_FALSE(histograms.value(key).toArray().isEmpty());
    }
}

// The buckets are logarithmic in MB/s, so a slow phase and a fast one must
// land in different ones. This is the only place getThroughputBucket's
// boundaries are observable from outside the class.
TEST_CASE("PerformanceStats sorts throughput into different buckets", "[perfstats]")
{
    auto bucketOf = [](quint64 bytesPerInterval) {
        PerformanceStats stats;
        stats.startSession(QStringLiteral("test.img"), 1024, QStringLiteral("/dev/null"));
        QThread::msleep(140);
        stats.recordWriteProgress(bytesPerInterval, bytesPerInterval * 4);
        QThread::msleep(140);
        stats.recordWriteProgress(bytesPerInterval * 2, bytesPerInterval * 4);

        const QJsonArray write = stats.exportToJson()
                                     .object()
                                     .value(QStringLiteral("histograms"))
                                     .toObject()
                                     .value(QStringLiteral("write"))
                                     .toArray();
        REQUIRE(write.size() == 1);
        const QJsonArray slice = write.at(0).toArray();
        REQUIRE(slice.size() == 16);
        for (int i = 4; i < slice.size(); ++i) {
            if (slice.at(i).toInt() > 0)
                return i - 4;
        }
        return -1;
    };

    // ~0.5 MB per 140ms is a few MB/s; ~64 MB per 140ms is a few hundred.
    const int slow = bucketOf(512ull * 1024);
    const int fast = bucketOf(64ull * 1024 * 1024);

    CHECK(slow >= 0);
    CHECK(fast > slow);
}

// ---------------------------------------------------------------------------
// Naming helpers
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceStats names every session state", "[perfstats]")
{
    const PerformanceStats::SessionState states[] = {
        PerformanceStats::SessionState::NeverStarted,
        PerformanceStats::SessionState::InProgress,
        PerformanceStats::SessionState::Succeeded,
        PerformanceStats::SessionState::Failed,
        PerformanceStats::SessionState::Cancelled,
    };

    for (auto state : states) {
        const QString name = PerformanceStats::sessionStateName(state);
        INFO("state: " << static_cast<int>(state));
        CHECK_FALSE(name.isEmpty());
    }
}

TEST_CASE("PerformanceStats names event types without gaps", "[perfstats]")
{
    // Walk the whole enum range rather than a hand-picked few: a name table
    // that has fallen behind the enum is exactly the bug worth catching, and
    // it is invisible until something exports.
    const PerformanceStats::EventType probes[] = {
        PerformanceStats::EventType::OsListFetch,
        PerformanceStats::EventType::NetworkRetry,
        PerformanceStats::EventType::DriveOpen,
        PerformanceStats::EventType::DriveFormat,
        PerformanceStats::EventType::CacheLookup,
        PerformanceStats::EventType::CacheFlush,
        PerformanceStats::EventType::MemoryAllocation,
        PerformanceStats::EventType::ImageExtraction,
        PerformanceStats::EventType::HashComputation,
        PerformanceStats::EventType::CycleStart,
        PerformanceStats::EventType::CycleEnd,
        PerformanceStats::EventType::ProgressStall,
        PerformanceStats::EventType::Customisation,
        PerformanceStats::EventType::FinalSync,
        PerformanceStats::EventType::DeviceClose,
    };

    for (auto type : probes) {
        const QString name = PerformanceStats::eventTypeName(type);
        INFO("event type: " << static_cast<int>(type));
        CHECK_FALSE(name.isEmpty());
        CHECK(name != QStringLiteral("Unknown"));
    }
}

// ---------------------------------------------------------------------------
// System information and export
// ---------------------------------------------------------------------------

TEST_CASE("PerformanceStats carries system info into the export", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("raspios.img.xz"), 2048, QStringLiteral("/dev/sdz"));

    PerformanceStats::SystemInfo info;
    info.totalMemoryBytes = 16ull * 1024 * 1024 * 1024;
    info.availableMemoryBytes = 8ull * 1024 * 1024 * 1024;
    info.devicePath = QStringLiteral("/dev/sdz");
    info.deviceSizeBytes = 64ull * 1024 * 1024 * 1024;
    info.deviceDescription = QStringLiteral("USB Mass Storage Device");
    info.deviceIsUsb = true;
    info.deviceIsRemovable = true;
    info.osName = QStringLiteral("Raspberry Pi OS");
    info.osVersion = QStringLiteral("13");
    info.cpuArchitecture = QStringLiteral("arm64");
    info.cpuCoreCount = 4;
    info.imagerVersion = QStringLiteral("v2.0.0");
    info.directIOEnabled = false;
    stats.setSystemInfo(info);

    // The real caller learns the direct-I/O answer only once the device is
    // open, so it overwrites the guess afterwards.
    stats.updateDirectIOEnabled(true);
    stats.endSession(true);

    const QJsonObject root = stats.exportToJson().object();
    REQUIRE(root.contains(QStringLiteral("version")));
    REQUIRE(root.contains(QStringLiteral("summary")));

    const QString serialised = QString::fromUtf8(stats.exportToJson().toJson());
    CHECK_THAT(serialised.toStdString(), ContainsSubstring("USB Mass Storage Device"));
    CHECK_THAT(serialised.toStdString(), ContainsSubstring("arm64"));
}

TEST_CASE("PerformanceStats export summarises the session", "[perfstats]")
{
    PerformanceStats stats;
    runShortCycle(stats);
    stats.endSession(false, QStringLiteral("Cancelled by user"));

    const QJsonObject summary = exportedSummary(stats);
    REQUIRE_FALSE(summary.isEmpty());

    const QString serialised = QString::fromUtf8(stats.exportToJson().toJson());
    CHECK_THAT(serialised.toStdString(), ContainsSubstring("raspios.img.xz"));
    // The cancellation must survive into the report, not be flattened to a
    // generic failure.
    CHECK_THAT(serialised.toStdString(), ContainsSubstring("Cancelled"));
}

TEST_CASE("PerformanceStats exports valid JSON even with no session", "[perfstats]")
{
    PerformanceStats stats;

    const QJsonDocument doc = stats.exportToJson();
    CHECK(doc.isObject());
    CHECK(doc.object().contains(QStringLiteral("version")));
}

TEST_CASE("PerformanceStats writes an export file that parses back", "[perfstats]")
{
    ScratchDir scratch;
    const QString path = scratch.filePath(QStringLiteral("stats.json"));

    PerformanceStats stats;
    runShortCycle(stats);
    stats.endSession(true);

    REQUIRE(stats.exportToFile(path));

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray written = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument reparsed = QJsonDocument::fromJson(written, &error);
    INFO("parse error: " << error.errorString().toStdString());
    CHECK(error.error == QJsonParseError::NoError);
    CHECK(reparsed.isObject());
    CHECK(reparsed.object().value(QStringLiteral("version")) ==
          stats.exportToJson().object().value(QStringLiteral("version")));
}

TEST_CASE("PerformanceStats reports a failed export rather than pretending", "[perfstats]")
{
    PerformanceStats stats;
    runShortCycle(stats);

    // A directory that does not exist cannot be opened for writing.
    CHECK_FALSE(stats.exportToFile(
        QStringLiteral("/nonexistent-rpi-imager-dir/deeper/still/stats.json")));
}

TEST_CASE("PerformanceStats distinguishes imaging data from any data", "[perfstats]")
{
    PerformanceStats bare;
    CHECK_FALSE(bare.hasImagingData());

    PerformanceStats imaged;
    runShortCycle(imaged);
    imaged.endSession(true);
    CHECK(imaged.hasData());
    CHECK(imaged.hasImagingData());
}

// ---------------------------------------------------------------------------
// Multiple cycles and accumulated state
// ---------------------------------------------------------------------------
//
// A capture session can hold several imaging cycles -- the user writes one
// card, then another -- and reset() is what separates a fresh session from an
// accumulating one. The paths that distinguish them were untouched.

TEST_CASE("PerformanceStats accumulates several cycles in one session", "[perfstats]")
{
    PerformanceStats stats;

    for (int i = 0; i < 3; ++i) {
        stats.startSession(QStringLiteral("image-%1.img").arg(i), 1024,
                           QStringLiteral("/dev/sd%1").arg(QChar('a' + i)));
        stats.recordWriteProgress(512, 1024);
        stats.endSession(i != 1, i == 1 ? QStringLiteral("I/O error") : QString());
    }

    // The last cycle's outcome is the session state, but the earlier cycles
    // must still be in the export -- that is the point of a capture session.
    CHECK(stats.sessionState() == PerformanceStats::SessionState::Succeeded);

    const QJsonArray events =
        stats.exportToJson().object().value(QStringLiteral("events")).toArray();
    int cycleStarts = 0;
    int cycleEnds = 0;
    for (const QJsonValue &v : events) {
        const QString type = v.toObject().value(QStringLiteral("type")).toString();
        if (type.contains(QStringLiteral("CycleStart")))
            ++cycleStarts;
        if (type.contains(QStringLiteral("CycleEnd")))
            ++cycleEnds;
    }
    INFO("cycle starts: " << cycleStarts << " ends: " << cycleEnds);
    CHECK(events.size() >= 6);
}

TEST_CASE("PerformanceStats starting a second session does not clear the first",
          "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("first.img"), 1024, QStringLiteral("/dev/sda"));
    stats.endSession(true);

    const int afterFirst =
        stats.exportToJson().object().value(QStringLiteral("events")).toArray().size();
    REQUIRE(afterFirst > 0);

    stats.startSession(QStringLiteral("second.img"), 1024, QStringLiteral("/dev/sdb"));
    stats.endSession(true);

    // startSession() explicitly does not reset; only reset() does. Losing the
    // first cycle here would silently halve the data in a support report.
    const int afterSecond =
        stats.exportToJson().object().value(QStringLiteral("events")).toArray().size();
    CHECK(afterSecond > afterFirst);
}

TEST_CASE("PerformanceStats reset between sessions starts clean", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("first.img"), 1024, QStringLiteral("/dev/sda"));
    stats.endSession(true);
    stats.reset();

    stats.startSession(QStringLiteral("second.img"), 1024, QStringLiteral("/dev/sdb"));
    stats.endSession(true);

    const QString serialised = QString::fromUtf8(stats.exportToJson().toJson());
    CHECK_THAT(serialised.toStdString(), ContainsSubstring("second.img"));
    // The first cycle is gone, which is what reset() is for.
    CHECK(serialised.indexOf(QStringLiteral("first.img")) < 0);
}

TEST_CASE("PerformanceStats records an event during every phase", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("test.img"), 1024 * 1024, QStringLiteral("/dev/null"));

    // Events are timestamped against the phase in progress, so recording one
    // in each phase covers the phase-attribution path rather than just the
    // idle case.
    stats.recordDownloadProgress(1024, 1024 * 1024);
    stats.recordEvent(PerformanceStats::EventType::NetworkLatency, 12, true);

    stats.recordDecompressProgress(2048, 1024 * 1024);
    stats.recordEvent(PerformanceStats::EventType::ImageDecompressInit, 8, true);

    stats.recordWriteProgress(4096, 1024 * 1024);
    stats.recordEvent(PerformanceStats::EventType::PeriodicSync, 30, true);

    stats.recordVerifyProgress(8192, 1024 * 1024);
    stats.recordEvent(PerformanceStats::EventType::HashComputation, 40, true);

    stats.recordFinalising();
    stats.recordEvent(PerformanceStats::EventType::FinalSync, 900, true);
    stats.endSession(true);

    const QJsonArray events =
        stats.exportToJson().object().value(QStringLiteral("events")).toArray();
    CHECK(events.size() >= 7);
}

TEST_CASE("PerformanceStats survives an enormous byte count", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("huge.img"), std::numeric_limits<quint64>::max(),
                       QStringLiteral("/dev/null"));

    // A bogus total from a device that misreports its size must not overflow
    // the percentage arithmetic or wedge the sampler.
    stats.recordWriteProgress(std::numeric_limits<quint64>::max() / 2,
                              std::numeric_limits<quint64>::max());
    stats.endSession(true);

    CHECK(stats.hasData());
    CHECK(stats.exportToJson().isObject());
}

TEST_CASE("PerformanceStats survives a total smaller than the progress", "[perfstats]")
{
    PerformanceStats stats;
    stats.startSession(QStringLiteral("odd.img"), 1024, QStringLiteral("/dev/null"));

    // Decompressed output routinely exceeds the archive size the caller first
    // reported, so "more done than there is to do" is a real state.
    stats.recordDecompressProgress(4096, 1024);
    stats.endSession(true);

    CHECK(stats.exportToJson().isObject());
}

// ---------------------------------------------------------------------------
// The names events are exported under
//
// The performance report is what somebody attaches to a support thread when a
// write was slow or failed, and every event in it is identified by the string
// below rather than by its enum value. An event with no name of its own is
// exported as "unknown", and two events sharing a name are merged into one row
// by the summary -- in both cases the report reads as though the thing that
// went wrong never happened.

TEST_CASE("Every event type is exported under a name of its own",
          "[performancestats]")
{
    QSet<QString> seen;
    for (int i = 0; i < static_cast<int>(PerformanceStats::EventType::_Count); ++i) {
        const auto type = static_cast<PerformanceStats::EventType>(i);
        const QString name = PerformanceStats::eventTypeName(type);

        INFO("EventType " << i << " is named \"" << name.toStdString() << "\"");
        CHECK_FALSE(name.isEmpty());
        // "unknown" is the fallback for a value with no entry in the table.
        CHECK(name != QStringLiteral("unknown"));
        // Shared names are silently merged in the exported summary, which is
        // keyed by name.
        CHECK_FALSE(seen.contains(name));
        seen.insert(name);
    }

    CHECK(seen.size() == static_cast<int>(PerformanceStats::EventType::_Count));
}
