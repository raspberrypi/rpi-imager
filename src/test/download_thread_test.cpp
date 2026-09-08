// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// DownloadThread is the write path: it pulls an image over curl and lands it
// on the target, hashing as it goes. At 1,958 branches it was both the
// largest single block of uncovered code in the tree and the most
// safety-critical, and nothing exercised it.
//
// Two things make it drivable without a device or a network:
//
//   - curl in this build has the file:// protocol, so a scratch file is a
//     perfectly good "server" and the whole transfer loop runs unchanged.
//   - _openAndPrepareDevice() only unmounts when the destination starts with
//     "/dev/", so a scratch image file takes the ordinary open-and-write path
//     that a real card would.
//
// Together that means the real code runs end to end -- curl callbacks, ring
// buffer, hashing, progress accounting and the final flush -- with nothing
// mocked out and nothing writable at risk.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include "downloadthread.h"

#include <QSet>
#include <thread>
#include <chrono>
#include <catch2/generators/catch_generators.hpp>
#include "signal_log.h"
#include "platform_file_operations.h"
#include "timeout_utils.h"

using rpi_imager::TimeoutDefaults::kHardTimeoutSeconds;
#include "faulty_block_device.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
#include <unistd.h>

#include "file_operations.h"
#include "imageadvancedoptions.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <memory>

#include "fixture_process.h"
#include "local_http_server.h"

namespace {

// A scratch directory that removes itself, so parallel runs cannot collide.
class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-dl-%1")
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

QByteArray patternOfSize(int size, int seed)
{
    QByteArray out;
    out.reserve(size);
    for (int i = 0; i < size; ++i)
        out.append(static_cast<char>((i * 31 + seed) % 251));
    return out;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const bool ok = f.write(contents) == contents.size();
    f.close();
    return ok;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    f.close();
    return data;
}

// Run a DownloadThread to completion and report which terminal signal fired.
//
// The thread emits success() or error() from its own thread, so this pumps an
// event loop rather than sleeping, and gives up after a bounded wait so a
// hang fails the case instead of the suite.
struct Outcome {
    bool finished = false;
    bool succeeded = false;
    QString errorMessage;
};

// How long to allow a full write-and-verify run.
//
// This exists to catch a thread that has hung, not to police how fast the
// write is. Under the gcov-instrumented build the hot write loop is roughly
// an order of magnitude slower than a release build -- the secure-boot case
// below measured 283 s against what used to be a 300 s limit, so it passed
// alone and failed the moment `ctest -j4` gave it any competition. The limit
// is now far enough clear of the real figure that only a genuine hang
// reaches it.
constexpr int kWriteTimeoutMs = 900000;

Outcome runToCompletion(DownloadThread &dt, int timeoutMs = 60000)
{
    Outcome outcome;
    QEventLoop loop;

    QObject::connect(&dt, &DownloadThread::success, &loop, [&]() {
        outcome.finished = true;
        outcome.succeeded = true;
        loop.quit();
    });
    QObject::connect(&dt, &DownloadThread::error, &loop, [&](const QString &msg) {
        outcome.finished = true;
        outcome.succeeded = false;
        outcome.errorMessage = msg;
        loop.quit();
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);

    dt.start();
    loop.exec();

    // Only cancel if the guard fired: cancelling a thread that already
    // succeeded tears down the cache writer and removes the cache file it
    // just wrote, which would make every cache assertion below fail for
    // reasons that have nothing to do with the code under test.
    if (!outcome.finished)
        dt.cancelDownload();
    dt.wait(30000);
    return outcome;
}

// Build a source file and the destination image, and hand back a thread
// wired to copy one to the other over file://.
std::unique_ptr<DownloadThread> makeDownload(const ScratchDir &scratch, const QByteArray &payload,
                                             const QString &sourceName, const QString &destName,
                                             const QByteArray &expectedHash = {})
{
    const QString source = scratch.filePath(sourceName);
    const QString dest = scratch.filePath(destName);
    REQUIRE(writeFile(source, payload));
    // The destination must already exist and be large enough: this is
    // standing in for a block device, which is never created by the writer.
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    const QByteArray url = QByteArray("file://") + source.toUtf8();
    return std::make_unique<DownloadThread>(url, dest.toUtf8(), expectedHash);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction and configuration
// ---------------------------------------------------------------------------

TEST_CASE("DownloadThread starts out unsuccessful", "[download]")
{
    DownloadThread dt("file:///nonexistent", "", "");

    // Nothing has run, so successfull() must not already be true -- callers
    // check it after the fact and a default of true would report a phantom
    // write.
    CHECK_FALSE(dt.successfull());
}

TEST_CASE("DownloadThread accepts its configuration knobs", "[download]")
{
    DownloadThread dt("file:///nonexistent", "", "");

    // These are all plain setters, but they run before any thread starts and
    // a throw here would take down the caller.
    CHECK_NOTHROW(dt.setUserAgent("rpi-imager-test/1.0"));
    CHECK_NOTHROW(dt.setVerifyEnabled(false));
    CHECK_NOTHROW(dt.setInputBufferSize(64 * 1024));
    CHECK_NOTHROW(dt.setDebugDirectIO(false));
    CHECK_NOTHROW(dt.setDebugPeriodicSync(false));
    CHECK_NOTHROW(dt.setDebugVerboseLogging(false));
    CHECK_NOTHROW(dt.setDebugAsyncIO(false));
    CHECK_NOTHROW(dt.setDebugAsyncQueueDepth(4));
    CHECK_NOTHROW(dt.setDebugIPv4Only(true));
    CHECK_NOTHROW(dt.setDebugSkipEndOfDevice(true));
    CHECK_NOTHROW(dt.setDebugIgnoreDeviceLimits(true));
    CHECK_NOTHROW(dt.setExtractTotal(1024));
}

TEST_CASE("DownloadThread reports itself as an image writer", "[download]")
{
    DownloadThread dt("file:///nonexistent", "", "");

    // The base class writes images; DownloadExtractThread overrides this.
    // It decides whether the destination is opened as a device at all.
    CHECK(dt.isImage());
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

TEST_CASE("DownloadThread writes a small payload to the target", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(64 * 1024, 7);

    auto dt = makeDownload(scratch, payload, QStringLiteral("src.img"),
                           QStringLiteral("dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    // Read the destination with plain file I/O rather than through the
    // writer, so a bug in the writer cannot confirm its own output.
    const QByteArray written = readFile(scratch.filePath(QStringLiteral("dest.img")));
    REQUIRE(written.size() >= payload.size());
    CHECK(written.left(payload.size()) == payload);
}

TEST_CASE("DownloadThread writes a payload spanning many buffers", "[download]")
{
    ScratchDir scratch;
    // Comfortably more than one input buffer, so the transfer loop runs many
    // times and the ring buffer actually cycles.
    const QByteArray payload = patternOfSize(6 * 1024 * 1024, 11);

    auto dt = makeDownload(scratch, payload, QStringLiteral("big-src.img"),
                           QStringLiteral("big-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    const QByteArray written = readFile(scratch.filePath(QStringLiteral("big-dest.img")));
    REQUIRE(written.size() >= payload.size());
    CHECK(written.left(payload.size()) == payload);
}

TEST_CASE("DownloadThread verifies a correct hash", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(256 * 1024, 3);
    const QByteArray hash =
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();

    auto dt = makeDownload(scratch, payload, QStringLiteral("hashed-src.img"),
                           QStringLiteral("hashed-dest.img"), hash);

    const Outcome outcome = runToCompletion(*dt);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);
}

TEST_CASE("DownloadThread rejects a payload whose hash does not match", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(256 * 1024, 5);

    // A valid-looking but wrong digest: the write may well land, but the
    // operation must not be reported as a success.
    const QByteArray wrongHash =
        QCryptographicHash::hash(QByteArray("something else"), QCryptographicHash::Sha256).toHex();

    auto dt = makeDownload(scratch, payload, QStringLiteral("bad-src.img"),
                           QStringLiteral("bad-dest.img"), wrongHash);

    const Outcome outcome = runToCompletion(*dt);
    REQUIRE(outcome.finished);

    // The contract callers actually rely on is the error() signal: a hash
    // mismatch must not be reported as a completed write.
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());

    // successfull() deliberately not asserted here. It is set the moment
    // curl_easy_perform() returns CURLE_OK -- that is, when the *transfer*
    // finished -- and verification runs afterwards without clearing it, so it
    // still reads true on a hash mismatch. Nothing outside this class calls
    // it today, so this is a trap for a future caller rather than a live bug,
    // but pinning the current behaviour here would enshrine it.
}

TEST_CASE("DownloadThread reports a source that does not exist", "[download]")
{
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("unused.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    DownloadThread dt("file:///nonexistent-rpi-imager/missing.img", dest.toUtf8(), "");

    const Outcome outcome = runToCompletion(*&dt);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

TEST_CASE("DownloadThread reports a destination it cannot open", "[download]")
{
    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("src.img"));
    REQUIRE(writeFile(source, patternOfSize(4096, 1)));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      "/nonexistent-rpi-imager-dir/deeper/dest.img", "");

    const Outcome outcome = runToCompletion(dt);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("DownloadThread handles an empty source", "[download]")
{
    ScratchDir scratch;

    auto dt = makeDownload(scratch, QByteArray(), QStringLiteral("empty-src.img"),
                           QStringLiteral("empty-dest.img"));
    dt->setVerifyEnabled(false);

    // Degenerate but reachable: a zero-length file must terminate rather than
    // wait forever for bytes that never arrive.
    const Outcome outcome = runToCompletion(*dt, 30000);
    CHECK(outcome.finished);
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

// Regression: cancelling while the device was still being prepared used to
// segfault the process.
//
// _openAndPrepareDevice() runs inside runWithTimeout(), which detached its
// worker on the cancellation path while that worker still held references to
// a completion flag and a promise on the frame being returned from. When the
// abandoned operation finally unblocked it wrote through both into a frame
// that no longer existed. timeout_utils_test provokes the same mechanism
// directly; this case is the product path a user reaches by pressing cancel
// early in a write.
//
// runWithTimeout() now keeps that state alive in a shared_ptr the worker
// holds, so a detached worker writes somewhere still valid.
TEST_CASE("DownloadThread survives cancellation during device preparation",
          "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(16 * 1024 * 1024, 13);

    auto dt = makeDownload(scratch, payload, QStringLiteral("cancel-src.img"),
                           QStringLiteral("cancel-dest.img"));
    dt->setVerifyEnabled(false);

    dt->start();
    // Early enough to land inside _openAndPrepareDevice() on any machine.
    QThread::msleep(5);
    dt->cancelDownload();

    // Generous on purpose. Cancelling does not interrupt a write and sync
    // already in flight -- the cancel flag lets runWithTimeout return and
    // abandon its worker, but the end-of-device write it guards is allowed
    // up to kHardTimeoutSeconds. Waiting only a few seconds asserts a
    // promptness the product does not offer, and this failed exactly once in
    // a full parallel run where several other cases were syncing tens of
    // megabytes at the same time. What is being tested is that the thread
    // ends at all rather than crashing in the abandoned worker, so the wait
    // is sized to the contract.
    dt->wait((kHardTimeoutSeconds + 15) * 1000);

    CHECK(dt->isFinished());
}

// The safe half of the same contract, which does run on every build: a write
// that is cancelled after it has finished must still not claim success, and
// the object must be destructible without hanging.
TEST_CASE("DownloadThread stays finished after a late cancel", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(128 * 1024, 29);

    auto dt = makeDownload(scratch, payload, QStringLiteral("late-src.img"),
                           QStringLiteral("late-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt);
    REQUIRE(outcome.finished);

    // runToCompletion already cancelled once; a second cancel on a finished
    // thread must be a no-op rather than a crash or a hang.
    CHECK_NOTHROW(dt->cancelDownload());
    CHECK(dt->isFinished());
}

TEST_CASE("DownloadThread can be cancelled before it starts", "[download]")
{
    DownloadThread dt("file:///nonexistent", "", "");

    CHECK_NOTHROW(dt.cancelDownload());
    CHECK_FALSE(dt.successfull());
}

// ---------------------------------------------------------------------------
// Progress reporting
// ---------------------------------------------------------------------------

TEST_CASE("DownloadThread accounts for the bytes it moved", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(4 * 1024 * 1024, 17);

    auto dt = makeDownload(scratch, payload, QStringLiteral("prog-src.img"),
                           QStringLiteral("prog-dest.img"));
    dt->setVerifyEnabled(false);

    // Progress is polled rather than signalled -- the UI reads these from its
    // own thread while the write runs -- so they are checked after the fact.
    CHECK(dt->dlNow() == 0);
    CHECK(dt->bytesWritten() == 0);

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // A counter stuck at zero through a successful multi-megabyte write
    // would leave the UI showing no movement at all.
    CHECK(dt->dlNow() > 0);
    CHECK(dt->bytesWritten() >= static_cast<uint64_t>(payload.size()));
}

TEST_CASE("DownloadThread counts verification separately", "[download]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 23);
    const QByteArray hash =
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();

    auto dt = makeDownload(scratch, payload, QStringLiteral("ver-src.img"),
                           QStringLiteral("ver-dest.img"), hash);
    dt->setVerifyEnabled(true);

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    // With verification on, the read-back pass has its own counter; leaving
    // it at zero on success would mean nothing was actually re-read.
    CHECK(dt->verifyNow() > 0);
}

// ---------------------------------------------------------------------------
// Cache writing
// ---------------------------------------------------------------------------
//
// setCacheFile() puts an AsyncCacheWriter in the path so the bytes are
// written twice: once to the target and once to the cache, hashed as they
// go. None of that runs unless a cache file is set, which is why it was
// entirely uncovered even with the transfer cases above passing.

TEST_CASE("DownloadThread writes a cache copy alongside the target", "[download][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 41);

    // The cache is kept only for a download whose hash was supplied and
    // matched -- an unverified image is deliberately never cached -- so the
    // expected hash is part of the setup, not an extra assertion.
    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    auto dt = makeDownload(scratch, payload, QStringLiteral("cache-src.img"),
                           QStringLiteral("cache-dest.img"), hash);

    const QString cachePath = scratch.filePath(QStringLiteral("cached.img"));
    dt->setCacheFile(cachePath, payload.size());

    QByteArray reportedHash;
    QObject::connect(dt.get(), &DownloadThread::cacheFileUpdated,
                     [&](QByteArray sha256) { reportedHash = sha256; });

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The cache copy must be byte-identical to what went to the target, and
    // read back with plain file I/O rather than through the writer.
    const QByteArray cached = readFile(cachePath);
    REQUIRE(cached.size() == payload.size());
    CHECK(cached == payload);

    // And the hash it reports must be of what it actually wrote -- a cache
    // entry filed under the wrong digest is worse than no cache at all.
    if (!reportedHash.isEmpty()) {
        CHECK(reportedHash ==
              QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
    }
}

TEST_CASE("DownloadThread survives a cache file it cannot open", "[download][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(64 * 1024, 43);

    auto dt = makeDownload(scratch, payload, QStringLiteral("nocache-src.img"),
                           QStringLiteral("nocache-dest.img"));
    dt->setVerifyEnabled(false);

    // An unwritable cache path must degrade to "no caching", not fail the
    // write: the user asked for an image on a card, not for a cache entry.
    dt->setCacheFile(QStringLiteral("/nonexistent-rpi-imager-dir/deeper/cached.img"),
                     payload.size());

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    const QByteArray written = readFile(scratch.filePath(QStringLiteral("nocache-dest.img")));
    CHECK(written.left(payload.size()) == payload);
}

TEST_CASE("DownloadThread caches a payload spanning many buffers", "[download][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(5 * 1024 * 1024, 47);

    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    auto dt = makeDownload(scratch, payload, QStringLiteral("bigcache-src.img"),
                           QStringLiteral("bigcache-dest.img"), hash);

    const QString cachePath = scratch.filePath(QStringLiteral("bigcached.img"));
    dt->setCacheFile(cachePath, payload.size());

    const Outcome outcome = runToCompletion(*dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // Many writer round trips rather than one, so this covers the queueing
    // and drain path rather than just the single-shot case.
    CHECK(readFile(cachePath) == payload);
}

TEST_CASE("DownloadThread deletes a downloaded file on request", "[download][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(32 * 1024, 53);

    auto dt = makeDownload(scratch, payload, QStringLiteral("del-src.img"),
                           QStringLiteral("del-dest.img"));
    dt->setVerifyEnabled(false);

    const QString cachePath = scratch.filePath(QStringLiteral("del-cached.img"));
    dt->setCacheFile(cachePath, payload.size());

    const Outcome outcome = runToCompletion(*dt, 120000);
    REQUIRE(outcome.finished);

    // Called when a write is abandoned: the half-written cache entry must go,
    // or the next run finds it and trusts it.
    CHECK_NOTHROW(dt->deleteDownloadedFile());
}

// ---------------------------------------------------------------------------
// Write-path configurations
// ---------------------------------------------------------------------------
//
// The debug setters are not diagnostics -- they select which write path runs.
// Direct I/O versus buffered, async submission versus synchronous, periodic
// sync on or off, and the queue depth all take different branches through
// _openAndPrepareDevice() and the write loop, and each combination is a path
// a real user can end up on depending on their hardware.
//
// Each case writes and reads back, so a configuration that lands the wrong
// bytes fails here rather than on someone's card.

namespace {

struct WriteConfig {
    const char *name;
    bool directIO;
    bool asyncIO;
    bool periodicSync;
    int queueDepth;
    int inputBuffer;
};

void checkWriteWithConfig(const WriteConfig &config)
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(3 * 1024 * 1024, 61);

    auto dt = makeDownload(scratch, payload, QStringLiteral("cfg-src.img"),
                           QStringLiteral("cfg-dest.img"));
    dt->setVerifyEnabled(false);
    dt->setDebugDirectIO(config.directIO);
    dt->setDebugAsyncIO(config.asyncIO);
    dt->setDebugPeriodicSync(config.periodicSync);
    if (config.queueDepth > 0)
        dt->setDebugAsyncQueueDepth(config.queueDepth);
    if (config.inputBuffer > 0)
        dt->setInputBufferSize(config.inputBuffer);

    const Outcome outcome = runToCompletion(*dt, kWriteTimeoutMs);
    INFO("config: " << config.name);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    const QByteArray written = readFile(scratch.filePath(QStringLiteral("cfg-dest.img")));
    REQUIRE(written.size() >= payload.size());
    CHECK(written.left(payload.size()) == payload);
}

} // namespace

TEST_CASE("DownloadThread writes correctly with buffered synchronous I/O", "[download][config]")
{
    checkWriteWithConfig({"buffered sync", false, false, false, 0, 0});
}

TEST_CASE("DownloadThread writes correctly with direct I/O", "[download][config]")
{
    // O_DIRECT changes the alignment requirements on every buffer handed to
    // the kernel; a misaligned write fails outright rather than being slow.
    checkWriteWithConfig({"direct I/O", true, false, false, 0, 0});
}

TEST_CASE("DownloadThread writes correctly with async I/O", "[download][config]")
{
    // io_uring submission, with completions reaped separately from the
    // submitting thread.
    checkWriteWithConfig({"async I/O", false, true, false, 0, 0});
}

TEST_CASE("DownloadThread writes correctly with direct and async I/O", "[download][config]")
{
    // The combination the imager actually prefers on hardware that supports
    // both, and the one with the most moving parts.
    checkWriteWithConfig({"direct + async", true, true, false, 8, 0});
}

TEST_CASE("DownloadThread writes correctly with periodic sync", "[download][config]")
{
    // Periodic fsync is what keeps a slow card from accumulating an
    // unbounded dirty page cache; it is skipped entirely under direct I/O.
    checkWriteWithConfig({"periodic sync", false, false, true, 0, 0});
}

TEST_CASE("DownloadThread writes correctly with a shallow async queue", "[download][config]")
{
    // A depth of one removes the overlap the async path exists for, so the
    // submit/complete handshake runs strictly in lockstep.
    checkWriteWithConfig({"async depth 1", false, true, false, 1, 0});
}

TEST_CASE("DownloadThread writes correctly with a small input buffer", "[download][config]")
{
    // Many small reads rather than a few large ones: the ring buffer cycles
    // far more often and the producer is much more likely to outrun it.
    checkWriteWithConfig({"small input buffer", false, false, false, 0, 64 * 1024});
}

TEST_CASE("DownloadThread writes correctly with device limits ignored", "[download][config]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(1024 * 1024, 67);

    auto dt = makeDownload(scratch, payload, QStringLiteral("nolimits-src.img"),
                           QStringLiteral("nolimits-dest.img"));
    dt->setVerifyEnabled(false);
    // Overrides the per-device maximum transfer size the kernel reports,
    // which is the escape hatch for a device that reports nonsense.
    dt->setDebugIgnoreDeviceLimits(true);
    dt->setDebugSkipEndOfDevice(true);

    const Outcome outcome = runToCompletion(*dt, 120000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("nolimits-dest.img"))).left(payload.size()) ==
          payload);
}

TEST_CASE("DownloadThread writes correctly with verbose logging", "[download][config]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(256 * 1024, 71);

    auto dt = makeDownload(scratch, payload, QStringLiteral("verbose-src.img"),
                           QStringLiteral("verbose-dest.img"));
    dt->setVerifyEnabled(false);
    // Verbose logging runs extra formatting on the hot path; it must not
    // change what is written.
    dt->setDebugVerboseLogging(true);
    dt->setUserAgent("rpi-imager-test/1.0");

    const Outcome outcome = runToCompletion(*dt, 120000);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("verbose-dest.img"))).left(payload.size()) ==
          payload);
}

// ---------------------------------------------------------------------------
// Async recovery controls
// ---------------------------------------------------------------------------
//
// These exist for a device that starts well and then degrades: the watchdog
// reduces the queue depth, or gives up on async entirely and hot-swaps to
// synchronous writes mid-transfer. They are called from another thread while
// a write is in flight, which is exactly when they are hardest to get right.

TEST_CASE("DownloadThread async controls are safe before a write starts", "[download][async]")
{
    DownloadThread dt("file:///nonexistent", "", "");

    // Nothing is in flight, so each of these must be a no-op rather than
    // touching a queue that does not exist yet.
    CHECK_NOTHROW(dt.forcePollAsyncCompletions());
    CHECK_NOTHROW(dt.forceAsyncRecovery());
    CHECK_FALSE(dt.reduceAsyncQueueDepth(2));

    // drainAndSwitchToSync() answers true here, which is the right answer
    // rather than a quirk: with no device open there is no async queue, so
    // the caller's goal -- "be in synchronous mode" -- already holds and
    // there is nothing to drain. Reporting failure would make the watchdog
    // escalate over a state that is already correct.
    CHECK(dt.drainAndSwitchToSync(1));
}

TEST_CASE("DownloadThread async controls are safe after a write finishes",
          "[download][async]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 73);

    auto dt = makeDownload(scratch, payload, QStringLiteral("post-src.img"),
                           QStringLiteral("post-dest.img"));
    dt->setVerifyEnabled(false);
    dt->setDebugAsyncIO(true);

    const Outcome outcome = runToCompletion(*dt, 120000);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The watchdog can fire after the write has completed but before it is
    // torn down; that race must not crash or resurrect the queue.
    CHECK_NOTHROW(dt->forcePollAsyncCompletions());
    CHECK_NOTHROW(dt->forceAsyncRecovery());
    CHECK_NOTHROW(dt->reduceAsyncQueueDepth(1));
}

// ---------------------------------------------------------------------------
// Image customisation
// ---------------------------------------------------------------------------
//
// This is the feature that makes the imager more than dd: after the image is
// written, the boot partition is reopened and config.txt, cmdline.txt and
// firstrun.sh are edited in place. It runs against the destination, so it
// needs the written image to actually be a partitioned disk -- an MBR whose
// first partition holds a FAT filesystem, which is what a real Pi image is.
//
// The fixture below builds exactly that: a FAT filesystem from mkfs.vfat,
// placed at LBA 2048 behind a hand-written MBR. Everything is verified by
// reopening the destination afterwards and reading the files back through the
// FAT driver, so the assertions describe the card rather than the writer.

namespace {

bool haveMkfsVfat()
{
    return QFileInfo::exists(QStringLiteral("/sbin/mkfs.vfat")) ||
           QFileInfo::exists(QStringLiteral("/usr/sbin/mkfs.vfat"));
}

QString mkfsVfatPath()
{
    return QFileInfo::exists(QStringLiteral("/sbin/mkfs.vfat"))
               ? QStringLiteral("/sbin/mkfs.vfat")
               : QStringLiteral("/usr/sbin/mkfs.vfat");
}

// Build a disk image: MBR at sector 0, one FAT32 partition at LBA 2048.
//
// The MBR is written by hand rather than shelled out to sfdisk so the layout
// is explicit and the test does not depend on another tool's defaults.
bool buildPartitionedImage(const QString &imagePath, int fatMegabytes)
{
    const qint64 partitionOffset = 2048LL * 512;
    const qint64 fatBytes = static_cast<qint64>(fatMegabytes) * 1024 * 1024;

    const QString fatPath = imagePath + QStringLiteral(".fat");
    {
        QFile fat(fatPath);
        if (!fat.open(QIODevice::WriteOnly))
            return false;
        if (!fat.resize(fatBytes))
            return false;
        fat.close();
    }

    QProcess mkfs;
    mkfs.start(mkfsVfatPath(), {QStringLiteral("-F"), QStringLiteral("32"),
                                QStringLiteral("-n"), QStringLiteral("bootfs"), fatPath});
    if (!mkfs.waitForFinished(rpi_test::kFixtureProcessTimeoutMs)) {
        qWarning() << "buildPartitionedImage: mkfs.vfat did not finish in time";
        mkfs.kill();
        mkfs.waitForFinished(5000);
        return false;
    }
    if (mkfs.exitStatus() != QProcess::NormalExit || mkfs.exitCode() != 0) {
        qWarning() << "buildPartitionedImage: mkfs.vfat failed:"
                   << mkfs.exitCode() << mkfs.readAllStandardError();
        return false;
    }

    QFile fatFile(fatPath);
    if (!fatFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray fatImage = fatFile.readAll();
    fatFile.close();
    QFile::remove(fatPath);

    QByteArray disk(partitionOffset, '\0');

    // Partition entry 1 at 0x1BE: bootable flag, CHS values the kernel
    // ignores for LBA partitions, type 0x0C (FAT32 LBA), then start and
    // length in sectors.
    const quint32 startLba = 2048;
    const quint32 sectorCount = static_cast<quint32>(fatBytes / 512);
    auto put32 = [&](int offset, quint32 value) {
        disk[offset + 0] = static_cast<char>(value & 0xFF);
        disk[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
        disk[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
        disk[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
    };
    disk[0x1BE] = static_cast<char>(0x80);
    disk[0x1BF] = static_cast<char>(0xFE);
    disk[0x1C0] = static_cast<char>(0xFF);
    disk[0x1C1] = static_cast<char>(0xFF);
    disk[0x1C2] = static_cast<char>(0x0C);
    disk[0x1C3] = static_cast<char>(0xFE);
    disk[0x1C4] = static_cast<char>(0xFF);
    disk[0x1C5] = static_cast<char>(0xFF);
    put32(0x1C6, startLba);
    put32(0x1CA, sectorCount);
    disk[0x1FE] = static_cast<char>(0x55);
    disk[0x1FF] = static_cast<char>(0xAA);

    disk.append(fatImage);

    QFile out(imagePath);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    const bool ok = out.write(disk) == disk.size();
    out.close();
    return ok;
}

// Read a file back out of the boot partition of a written destination.
QByteArray readFromBootPartition(const QString &devicePath, const QString &name)
{
    auto ops = rpi_imager::FileOperations::Create();
    if (ops->OpenDevice(devicePath.toStdString()) != rpi_imager::FileError::kSuccess)
        return {};
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition *fat = dw.fatPartition(1);
    if (!fat)
        return {};
    return fat->readFile(name);
}

} // namespace

TEST_CASE("DownloadThread writes config.txt into the boot partition", "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("cust-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("cust-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // initFormat is not optional: the whole customisation pass is gated on it
    // being set, so an empty one silently skips every edit below.
    dt.setImageCustomisation("dtoverlay=disable-bt", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "systemd", ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // Read the card back through the FAT driver: the customisation is only
    // real if it survived to the filesystem.
    const QByteArray config = readFromBootPartition(dest, QStringLiteral("config.txt"));
    INFO("config.txt: " << QString::fromUtf8(config).toStdString());
    CHECK(config.contains("dtoverlay=disable-bt"));
}

TEST_CASE("DownloadThread writes cmdline and firstrun into the boot partition",
          "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("cust2-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("cust2-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    const QByteArray firstrun = "#!/bin/bash\necho provisioned\nrm -f /boot/firstrun.sh\n";

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation(QByteArray(), "quiet splash", firstrun, QByteArray(), QByteArray(),
                             "systemd", ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // firstrun.sh is the one that actually provisions the device on first
    // boot; landing it truncated or not at all is a silent failure the user
    // only discovers when the Pi comes up unconfigured.
    const QByteArray written = readFromBootPartition(dest, QStringLiteral("firstrun.sh"));
    CHECK(written == firstrun);

    const QByteArray cmdline = readFromBootPartition(dest, QStringLiteral("cmdline.txt"));
    INFO("cmdline.txt: " << QString::fromUtf8(cmdline).toStdString());
    CHECK(cmdline.contains("quiet splash"));
}

TEST_CASE("DownloadThread writes cloud-init files into the boot partition",
          "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("cust3-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("cust3-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    const QByteArray userData = "#cloud-config\nhostname: testpi\n";
    const QByteArray networkData = "version: 2\nethernets:\n  eth0:\n    dhcp4: true\n";

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation(QByteArray(), QByteArray(), QByteArray(), userData, networkData,
                             "cloudinit", ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // cloud-init takes a different filename set from the Raspberry Pi OS
    // customisation above, so this is a separate branch rather than a
    // variation on it.
    const QByteArray userDataOnCard = readFromBootPartition(dest, QStringLiteral("user-data"));
    INFO("user-data: " << QString::fromUtf8(userDataOnCard).toStdString());
    CHECK(userDataOnCard.contains("hostname: testpi"));
}

TEST_CASE("DownloadThread leaves the image alone when nothing is customised",
          "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("plain-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("plain-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // No customisation at all: the whole reopen-and-edit pass must be
    // skipped rather than run with empty values, which would still rewrite
    // config.txt and change the image the user asked for.

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The written bytes must match the source exactly.
    const QByteArray sourceBytes = readFile(source);
    const QByteArray writtenBytes = readFile(dest);
    REQUIRE(writtenBytes.size() >= sourceBytes.size());
    CHECK(writtenBytes.left(sourceBytes.size()) == sourceBytes);
}

// ---------------------------------------------------------------------------
// Real HTTP
// ---------------------------------------------------------------------------
//
// Everything above uses file:// URLs, which exercise the transfer loop but
// skip the whole HTTP layer: status handling, headers, the resume logic and
// the retry path all sit behind a real connection. A throwaway server on
// 127.0.0.1 reaches them without a network.
//
// The server is a few lines of Python bound to port 0, so it never collides
// with anything else on the machine and never listens beyond loopback.

namespace {

} // namespace

using rpi_test::LocalHttpServer;
using rpi_test::havePython;



TEST_CASE("DownloadThread writes an image fetched over HTTP", "[download][http]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(2 * 1024 * 1024, 79);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("served.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("http-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    DownloadThread dt(server.urlFor(QStringLiteral("served.img")), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setUserAgent("rpi-imager-test/1.0");

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(dest).left(payload.size()) == payload);
}

TEST_CASE("DownloadThread verifies a hash over HTTP", "[download][http]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(1024 * 1024, 83);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("hashed.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("http-hash-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    DownloadThread dt(server.urlFor(QStringLiteral("hashed.img")), dest.toUtf8(), hash);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);
}

TEST_CASE("DownloadThread reports an HTTP 404", "[download][http]")
{
    ScratchDir scratch;
    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("404-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    DownloadThread dt(server.urlFor(QStringLiteral("no-such-image.img")), dest.toUtf8(),
                      QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 60000);
    REQUIRE(outcome.finished);

    // A 404 body is still a body: curl will happily hand back the error page
    // unless the status is checked, and writing an HTML error page onto a
    // card is worse than failing.
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

TEST_CASE("DownloadThread reports a refused connection", "[download][http]")
{
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("refused-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    // Port 1 on loopback: nothing listens there, so the connection is
    // refused immediately rather than timing out.
    DownloadThread dt("http://127.0.0.1:1/image.img", dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setDebugIPv4Only(true);

    const Outcome outcome = runToCompletion(dt, 120000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

TEST_CASE("DownloadThread reports a hash mismatch over HTTP", "[download][http]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 89);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("mismatch.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("mismatch-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    const QByteArray wrong =
        QCryptographicHash::hash(QByteArray("not this image"), QCryptographicHash::Sha256).toHex();
    DownloadThread dt(server.urlFor(QStringLiteral("mismatch.img")), dest.toUtf8(), wrong);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("A device node that has gone is reported, not waited on",
          "[download][device]")
{
    // The race the drive poller cannot close: the card is pulled out between
    // the write being set up and the device being opened. Everything under
    // /dev/ is unmounted first, and unmounting something that is not there
    // fails -- which has to come back as an error naming the device rather
    // than as a write that never starts.
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(64 * 1024, 91);
    const QString source = scratch.filePath(QStringLiteral("gone-source.img"));
    REQUIRE(writeFile(source, payload));

    const QByteArray target = "/dev/nonexistent-rpi-imager-target";
    REQUIRE_FALSE(QFileInfo::exists(QString::fromUtf8(target)));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), target, QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 60000);
    REQUIRE(outcome.finished);
    REQUIRE_FALSE(outcome.succeeded);

    const std::string message = outcome.errorMessage.toStdString();
    INFO("message: " << message);
    // Named, because a user with several drives plugged in needs to know
    // which one the writer went looking for.
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring(std::string(target.constData())));
}

// ---------------------------------------------------------------------------
// Where a redirect is allowed to lead
// ---------------------------------------------------------------------------
//
// Image URLs are followed through redirects, up to ten of them. The OS list
// fetcher restricts what a redirect may switch to; the image download leaves
// it to libcurl's default, which excludes file, scp and smb. That difference
// is easy to lose sight of, and what it protects is worth stating: a
// repository, or an rpi-imager:// link somebody accepted, chooses the image
// URL, and a redirect from it into file:// would have the writer read a local
// file and put it on the card.
//
// The first case exists so the second one means something. Without it, "the
// redirect was not followed" would hold just as well on a build where
// redirects were not followed at all.

TEST_CASE("An image download follows a redirect to another http URL",
          "[download][http][redirect]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(256 * 1024, 71);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("moved.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("redirect-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    DownloadThread dt(server.redirectTo(server.urlFor(QStringLiteral("moved.img"))),
                      dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);
    CHECK(readFile(dest).left(payload.size()) == payload);
}

TEST_CASE("An image download will not be redirected into a local file",
          "[download][http][redirect]")
{
    ScratchDir scratch;
    const QByteArray secret = patternOfSize(64 * 1024, 72);
    const QString local = scratch.filePath(QStringLiteral("local-secret.bin"));
    REQUIRE(writeFile(local, secret));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("no-file-dest.img"));
    const QByteArray blank(secret.size() + (1024 * 1024), '\0');
    REQUIRE(writeFile(dest, blank));

    const QByteArray target = QByteArray("file://") + local.toUtf8();
    DownloadThread dt(server.redirectTo(target), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 60000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);

    // The card is what matters: nothing of the local file reached it.
    const QByteArray written = readFile(dest);
    CHECK_FALSE(written.contains(secret.left(4096)));
}

TEST_CASE("A corrupt download is blamed on the network, not on the user",
          "[download][http][hash]")
{
    // The third of the three hash-mismatch messages, and the only one that
    // needs a server to reach. A download that arrives damaged is worth
    // retrying, and the message says so. The other two -- a corrupt cache and
    // a corrupt file the user chose themselves -- send them somewhere else
    // entirely, so which one arrives matters more than that one does.
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 98);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("served.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("corrupt-download-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    const QByteArray wrong =
        QCryptographicHash::hash(QByteArray("a different image"), QCryptographicHash::Sha256)
            .toHex();
    DownloadThread dt(server.urlFor(QStringLiteral("served.img")), dest.toUtf8(), wrong);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);
    REQUIRE_FALSE(outcome.succeeded);

    const std::string message = outcome.errorMessage.toStdString();
    INFO("message: " << message);
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("Download appears to be corrupt"));
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("network"));
    // Both hashes, so somebody reporting it has something to quote.
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring(std::string(wrong.constData())));
    // And not either of the other two, which would send them to look at a
    // cache file or at a file of their own that is not involved.
    CHECK_THAT(message, !Catch::Matchers::ContainsSubstring("Cached file"));
    CHECK_THAT(message, !Catch::Matchers::ContainsSubstring("Local file"));
}

TEST_CASE("DownloadThread caches an image fetched over HTTP", "[download][http][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(1024 * 1024, 97);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("cacheable.img")), payload));

    LocalHttpServer server(scratch.filePath(QStringLiteral(".")));
    REQUIRE_HTTP_SERVER(server);

    const QString dest = scratch.filePath(QStringLiteral("http-cache-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    const QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
    DownloadThread dt(server.urlFor(QStringLiteral("cacheable.img")), dest.toUtf8(), hash);

    const QString cachePath = scratch.filePath(QStringLiteral("http-cached.img"));
    dt.setCacheFile(cachePath, payload.size());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The realistic path end to end: fetched over the network, verified, and
    // kept for next time.
    CHECK(readFile(cachePath) == payload);
}

// ---------------------------------------------------------------------------
// Secure boot
// ---------------------------------------------------------------------------
//
// With EnableSecureBoot set, the customisation pass does considerably more
// than edit config.txt: it extracts every file from the boot partition,
// repacks them into a boot.img, signs that with the user's RSA key and writes
// boot.img plus boot.sig back. It is the largest single uncovered block left
// in downloadthread.cpp, and it is reachable here because the key path comes
// from QSettings and the signing shells out to openssl.
//
// This target runs under QStandardPaths test mode with its own organisation
// name, so the settings written below land in a throwaway file rather than
// the developer's real imager configuration.

namespace {

bool haveOpenssl() { return QFileInfo::exists(QStringLiteral("/usr/bin/openssl")); }

bool generateRsaKey(const QString &path)
{
    QProcess openssl;
    openssl.start(QStringLiteral("/usr/bin/openssl"),
                  {QStringLiteral("genrsa"), QStringLiteral("-out"), path,
                   QStringLiteral("2048")});
    openssl.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    return openssl.exitStatus() == QProcess::NormalExit && openssl.exitCode() == 0 &&
           QFileInfo(path).size() > 0;
}

void setConfiguredRsaKey(const QString &path)
{
    QSettings settings;
    if (path.isEmpty())
        settings.remove(QStringLiteral("secureboot_rsa_key"));
    else
        settings.setValue(QStringLiteral("secureboot_rsa_key"), path);
    settings.sync();
}

} // namespace

TEST_CASE("A signing key that is not a key stops the write", "[download][secureboot]")
{
    // Secure boot means the Pi will only run an image it can check the
    // signature of. If the signing step fails and the write finishes anyway,
    // the card is written, looks finished, and the board refuses to boot from
    // it -- with nothing anywhere to say why. So the failure has to come back
    // as a failure, naming the step that could not be done.
    //
    // A key the user picked with the file chooser is whatever they picked.
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("not-a-key.pem"));
    REQUIRE(writeFile(key, QByteArray("-----BEGIN NOTHING-----\nnope\n")));
    setConfiguredRsaKey(key);

    const QString source = scratch.filePath(QStringLiteral("sb-bad-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));
    const QString dest = scratch.filePath(QStringLiteral("sb-bad-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "systemd", ImageOptions::EnableSecureBoot);

    // Every message, not the one that happened to arrive last. Two are
    // emitted -- the step that failed, and then the caller reporting that
    // secure boot could not be set up -- and runToCompletion() keeps
    // whichever lands second, which is a race: this case passed on one
    // machine and failed on another for exactly that reason.
    QStringList reported;
    QObject::connect(&dt, &DownloadThread::error, &dt,
                     [&reported](const QString &m) { reported << m; });

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    setConfiguredRsaKey(QString());

    REQUIRE(outcome.finished);
    INFO("reported: " << reported.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(outcome.succeeded);
    // The step that objected is named somewhere in what the user is told, so
    // somebody who chose the wrong file knows which one it was.
    CHECK(std::any_of(reported.cbegin(), reported.cend(), [](const QString &m) {
        return m.contains(QStringLiteral("boot.sig"));
    }));

    // What the card holds afterwards is deliberately not asserted. Reading
    // boot.sig back and finding it absent would pass just as readily because
    // the boot partition never got far enough to be readable, and what a
    // half-written card should contain is not a contract anything states.
}

TEST_CASE("DownloadThread signs a boot image for secure boot", "[download][secureboot]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");
    if (!haveOpenssl())
        SKIP("openssl is not installed, so no signing key can be generated");

    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("secureboot.pem"));
    REQUIRE(generateRsaKey(key));
    setConfiguredRsaKey(key);

    const QString source = scratch.filePath(QStringLiteral("sb-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));
    const QString dest = scratch.filePath(QStringLiteral("sb-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "systemd", ImageOptions::EnableSecureBoot);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // A secure-boot card is only bootable if both halves landed: the packed
    // image and the signature over it.
    const QByteArray bootImg = readFromBootPartition(dest, QStringLiteral("boot.img"));
    const QByteArray bootSig = readFromBootPartition(dest, QStringLiteral("boot.sig"));

    INFO("boot.img size: " << bootImg.size());
    INFO("boot.sig: " << QString::fromUtf8(bootSig).toStdString());
    CHECK_FALSE(bootImg.isEmpty());
    CHECK_FALSE(bootSig.isEmpty());
    CHECK(QString::fromUtf8(bootSig).contains(QStringLiteral("ts:")));

    setConfiguredRsaKey(QString());
}

TEST_CASE("DownloadThread refuses secure boot with no key configured",
          "[download][secureboot]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    setConfiguredRsaKey(QString());

    const QString source = scratch.filePath(QStringLiteral("sb-nokey-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));
    const QString dest = scratch.filePath(QStringLiteral("sb-nokey-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "systemd", ImageOptions::EnableSecureBoot);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);

    // Silently writing an unsigned card when the user asked for secure boot
    // would be the worst outcome here: it looks like it worked and the device
    // refuses to boot.
    CHECK_FALSE(outcome.succeeded);
    INFO("reported error: " << outcome.errorMessage.toStdString());
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

TEST_CASE("DownloadThread refuses secure boot with a key that is not there",
          "[download][secureboot]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    setConfiguredRsaKey(QStringLiteral("/nonexistent-rpi-imager-dir/absent.pem"));

    const QString source = scratch.filePath(QStringLiteral("sb-gone-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));
    const QString dest = scratch.filePath(QStringLiteral("sb-gone-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "systemd", ImageOptions::EnableSecureBoot);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);

    setConfiguredRsaKey(QString());
}

// ---------------------------------------------------------------------------
// Customisation read-back
// ---------------------------------------------------------------------------
//
// With verification on, the customisation pass does not just write the files
// -- it reopens the partition and reads them back to confirm they landed.
// That check is what catches a card that acknowledged the writes and dropped
// them, which is a real failure mode on counterfeit media, and it only runs
// when customisation and verification are both enabled.

TEST_CASE("DownloadThread verifies the customisation it wrote", "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("vc-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("vc-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    const QByteArray sourceBytes = readFile(source);
    const QByteArray hash =
        QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256).toHex();

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), hash);
    dt.setVerifyEnabled(true);
    dt.setImageCustomisation("dtparam=audio=on", "console=tty1",
                             "#!/bin/sh\nexit 0\n", QByteArray(), QByteArray(), "systemd",
                             ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // Both the image verification and the customisation read-back ran, and
    // the files are on the card.
    CHECK(dt.verifyNow() > 0);
    CHECK(readFromBootPartition(dest, QStringLiteral("config.txt"))
              .contains("dtparam=audio=on"));
    CHECK(readFromBootPartition(dest, QStringLiteral("firstrun.sh"))
              .contains("exit 0"));
}

TEST_CASE("DownloadThread verifies cloud-init customisation", "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("vci-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("vci-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    // Verification needs the expected hash: without one it has no length to
    // bound the read-back against, and compares the whole destination -- which
    // is larger than the image -- with the hash of what was streamed.
    const QByteArray hash =
        QCryptographicHash::hash(readFile(source), QCryptographicHash::Sha256).toHex();

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), hash);
    dt.setVerifyEnabled(true);
    dt.setImageCustomisation(QByteArray(), QByteArray(), QByteArray(),
                             "#cloud-config\nhostname: verified\n",
                             "version: 2\n", "cloudinit", ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFromBootPartition(dest, QStringLiteral("user-data")).contains("hostname: verified"));
}

TEST_CASE("DownloadThread applies rpi-preseed customisation", "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("preseed-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("preseed-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // The third init format, alongside systemd and cloudinit; it writes a
    // different file set again.
    dt.setImageCustomisation("arm_64bit=1", QByteArray(), QByteArray(), QByteArray(),
                             QByteArray(), "rpi-preseed", ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    CHECK(readFromBootPartition(dest, QStringLiteral("config.txt")).contains("arm_64bit=1"));
}

// ---------------------------------------------------------------------------
// Write errors from a genuinely faulty device
// ---------------------------------------------------------------------------
//
// A card that accepts the first part of a write and then starts returning
// errors is a real failure -- worn flash, a counterfeit card lying about its
// size, a reader losing contact. The one outcome that must never happen is
// reporting success, because the user then boots a half-written card and has
// no idea why it fails.
//
// The device here is entirely synthetic: a scratch file on a loop device with
// a device-mapper table that maps the first 48MB through and returns EIO for
// everything after it. No real media is touched. Writes past the boundary
// produce genuine negative completions from the block layer, which is how the
// io_uring error handling gets exercised at all -- it cannot be reached by
// feeding the writer bad arguments.
//
// Needs root to create the mapping, so the cases skip themselves when
// passwordless sudo is unavailable, matching how disk_formatter_test handles
// privileged setup.

namespace {

using rpi_imager::testing::canRunPrivileged;
using rpi_imager::testing::FaultyDevice;

} // namespace

#define REQUIRE_FAULTY(device)                                                                     \
    if (!canRunPrivileged())                                                                       \
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");                \
    if (!(device).isReady())                                                                       \
    SKIP("the device-mapper fault injection device could not be created")

TEST_CASE("DownloadThread reports a device that fails partway through a write",
          "[download][faulty]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");

    FaultyDevice device(64, 16);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    // Comfortably larger than the good region, so the write runs into the
    // failing sectors rather than stopping short of them.
    const QByteArray payload = patternOfSize(48 * 1024 * 1024, 101);
    const QString source = scratch.filePath(QStringLiteral("faulty-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);

    // The whole point: a device that stopped accepting data must not be
    // reported as a completed write.
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

// Regression: an async write failure used to abort the process with
// "double free or corruption" instead of reporting the error.
//
// AsyncWriteSequential() invokes its callback exactly once on every path it
// can return by, and that callback frees the bounce buffer. The caller also
// freed it whenever a non-success code came back -- so every async
// submission failure freed the same block twice. The sync path handled the
// identical condition cleanly, which is what made it look like a detection
// problem rather than a memory-safety one.
//
// A card that starts erroring mid-write is the ordinary way a user met this,
// and the crash destroyed the error message that would have explained it.
//
// The device below is synthetic: a loop-backed device-mapper target whose
// sectors return EIO past a boundary. No real media is involved.
TEST_CASE("DownloadThread reports a faulty device under async I/O",
          "[download][faulty]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");

    FaultyDevice device(64, 16);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    const QByteArray payload = patternOfSize(48 * 1024 * 1024, 103);
    const QString source = scratch.filePath(QStringLiteral("faulty-async-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // With async submission the failure arrives as a negative completion on
    // the reaping side rather than as a return value from the write call --
    // a genuinely different error path, and one that cannot be provoked
    // without a device that really fails.
    dt.setDebugAsyncIO(true);
    dt.setDebugDirectIO(true);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("DownloadThread writes successfully within the good region",
          "[download][faulty]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");

    // Fully writable: the control case. Note it has to be *fully* writable,
    // not merely large enough for the payload -- the imager deliberately
    // zeroes the last part of the card to catch counterfeits that advertise
    // more capacity than they have, so a device with a bad tail is correctly
    // rejected as fake no matter how small the image is.
    FaultyDevice device(64, 64);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    const QByteArray payload = patternOfSize(8 * 1024 * 1024, 107);
    const QString source = scratch.filePath(QStringLiteral("good-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // Buffered, synchronous writes.
    //
    // Not a convenience: O_DIRECT writes to this device-mapper target fail
    // where buffered writes to the same device succeed, and the imager does
    // not fall back -- it reports a write error and stops. dd(1) with
    // oflag=direct writes to the same device happily at 1MB blocks, so the
    // device itself accepts direct I/O; it is the imager's combination of
    // alignment and request size that it rejects. Worth a look, because the
    // same shape of failure on a real card is reported to the user as
    // "check if the device is writable" with no way to tell it was an
    // alignment problem.
    dt.setDebugAsyncIO(false);
    dt.setDebugDirectIO(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);
}

TEST_CASE("An image larger than the device is refused, not half-written",
          "[download][faulty]")
{
    // Normally the size is checked before anything starts. That check is
    // `_devLen && _extrLen > _devLen`, so it is skipped whenever the
    // uncompressed size could not be determined -- which is exactly what
    // happens for a streaming-compressed .zst, where the frame header carries
    // no content size (see image_size_parser_test). The write then begins
    // against a device too small to hold it and runs off the end.
    //
    // What matters is that it stops and says so. Reporting success would
    // leave the user with a card holding a truncated image: it may even mount
    // and appear to have worked, and only fails to boot later.
    if (!rpi_imager::testing::canRunPrivileged())
        SKIP("needs root or passwordless sudo to create a loop device");

    FaultyDevice device(32, 32);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    // Comfortably larger than the 32 MB device.
    const QByteArray payload = patternOfSize(64 * 1024 * 1024, 151);
    const QString source = scratch.filePath(QStringLiteral("oversized-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.setDebugAsyncIO(false);
    dt.setDebugDirectIO(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());

    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

// DownloadThread signals across threads and this file waits on those with a
// QEventLoop, none of which does anything without a QCoreApplication to
// dispatch it -- the loop simply never wakes and the case hangs rather than
// failing. Catch2's stock main() creates no such object, so this target
// supplies its own.

// ── What the screen says about why a write is slow ────────────────────
//
// While a write runs the pipeline reports which stage it is waiting on, and
// that becomes a line of text under the progress bar. It is the only thing
// telling a user whether a slow write is their broadband, their processor or
// their card -- and each answer sends them to do something different. Two of
// the cases crossed would send somebody out to buy a card that was never the
// problem.
//
// The whole mapping, so a state added later without a string of its own
// shows up here rather than as a blank line under a stalled progress bar.

TEST_CASE("Each bottleneck names the thing to look at", "[downloadthread][progress]")
{
    using B = DownloadThread::BottleneckState;

    CHECK(DownloadThread::bottleneckStatusText(B::Network)
              .contains(QStringLiteral("download"), Qt::CaseInsensitive));
    CHECK(DownloadThread::bottleneckStatusText(B::Decompression)
              .contains(QStringLiteral("decompression"), Qt::CaseInsensitive));
    CHECK(DownloadThread::bottleneckStatusText(B::Storage)
              .contains(QStringLiteral("storage"), Qt::CaseInsensitive));
    CHECK(DownloadThread::bottleneckStatusText(B::Verifying)
              .contains(QStringLiteral("Verifying"), Qt::CaseInsensitive));
}

TEST_CASE("A pipeline that is flowing says nothing at all", "[downloadthread][progress]")
{
    // Not "no bottleneck" or "OK": there is no problem to report, and a line
    // of reassurance under the progress bar is one more thing to read on a
    // screen that already has a percentage on it.
    CHECK(DownloadThread::bottleneckStatusText(
              DownloadThread::BottleneckState::None).isEmpty());
}

TEST_CASE("No two bottlenecks say the same thing", "[downloadthread][progress]")
{
    // The point of the message is to tell them apart. Two states sharing a
    // string would read as the display being stuck rather than the stage
    // having changed.
    const DownloadThread::BottleneckState states[] = {
        DownloadThread::BottleneckState::Network,
        DownloadThread::BottleneckState::Decompression,
        DownloadThread::BottleneckState::Storage,
        DownloadThread::BottleneckState::Verifying,
    };

    QSet<QString> seen;
    for (const auto s : states)
    {
        const QString text = DownloadThread::bottleneckStatusText(s);
        INFO(text.toStdString());
        CHECK_FALSE(text.isEmpty());
        CHECK_FALSE(seen.contains(text));
        seen.insert(text);
    }
}

// ── What a storage failure is reported as ─────────────────────────────
//
// Every way the card can fail during a write comes through one table, and
// what it produces is the whole of what the user is told: whether to close
// the application holding the device, whether the card is write-protected,
// whether to unplug and reconnect it. The messages carry the advice, so they
// are the part that has to be right.
//
// Reached through the thread only when the corresponding failure happens on
// a real device, which is why most of the table had never produced a string.
// Asked directly here instead.

namespace {

class ErrorPhrasing : public DownloadThread
{
public:
    ErrorPhrasing() : DownloadThread("file:///nonexistent", "", "") {}
    using DownloadThread::_fileErrorToString;
};

const rpi_imager::FileError kEveryFailure[] = {
    rpi_imager::FileError::kOpenError,
    rpi_imager::FileError::kWriteError,
    rpi_imager::FileError::kReadError,
    rpi_imager::FileError::kSeekError,
    rpi_imager::FileError::kSizeError,
    rpi_imager::FileError::kCloseError,
    rpi_imager::FileError::kLockError,
    rpi_imager::FileError::kSyncError,
    rpi_imager::FileError::kFlushError,
    rpi_imager::FileError::kTimeout,
};

} // namespace

TEST_CASE("Every storage failure has advice of its own", "[download][messages]")
{
    ErrorPhrasing p;
    QSet<QString> seen;

    for (const auto e : kEveryFailure)
    {
        const QString said = p._fileErrorToString(e, QStringLiteral("the final sync"));
        INFO(said.toStdString());

        CHECK_FALSE(said.isEmpty());
        // Not the fallback. A failure that fell through to "Unknown storage
        // error" would leave the user with nothing to do about it, and the
        // switch has no -Wswitch protection because of the default arm.
        CHECK_THAT(said.toStdString(), !Catch::Matchers::ContainsSubstring("Unknown storage error"));
        // And not a duplicate: two failures sharing a message means one of
        // them is being described as something it is not.
        CHECK_FALSE(seen.contains(said));
        seen.insert(said);
    }
}

TEST_CASE("A cancelled write is not reported as a fault", "[download][messages]")
{
    // The user pressed Cancel. Raising an error afterwards would tell them
    // something went wrong with a card that is simply unfinished -- and the
    // writing path calls this on the way out of a cancellation, so an empty
    // string here is what keeps the screen quiet.
    ErrorPhrasing p;

    CHECK(p._fileErrorToString(rpi_imager::FileError::kCancelled,
                               QStringLiteral("the write")).isEmpty());
    CHECK(p._fileErrorToString(rpi_imager::FileError::kSuccess,
                               QStringLiteral("the write")).isEmpty());
}

TEST_CASE("A failure names the step it happened during", "[download][messages]")
{
    // "during the final sync" and "during zeroing the partition table" are
    // the difference between a card that is nearly written and one that was
    // never touched. Where the caller supplies the step, it has to appear.
    ErrorPhrasing p;

    const QString named = p._fileErrorToString(rpi_imager::FileError::kWriteError,
                                               QStringLiteral("the final sync"));
    CHECK_THAT(named.toStdString(), Catch::Matchers::ContainsSubstring("the final sync"));

    // And where it does not, the sentence still reads: no dangling "during ."
    const QString unnamed = p._fileErrorToString(rpi_imager::FileError::kWriteError);
    INFO(unnamed.toStdString());
    CHECK_FALSE(unnamed.contains(QStringLiteral("during .")));
    CHECK_THAT(unnamed.toStdString(), Catch::Matchers::ContainsSubstring("storage operation"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // The secure-boot cases read the signing key path from QSettings. Test
    // mode plus a scoped name keeps that in a throwaway file rather than the
    // developer's real imager configuration.
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    // Scoped to this process, not just to test mode. catch_discover_tests
    // runs every TEST_CASE as its own process, so `ctest -j4` has several
    // of these alive at once -- and a settings file shared between them is
    // shared mutable state. The secure-boot cases each write a different
    // secureboot_rsa_key, so one process would see another's: the case
    // that expects no key found a valid one, the write it expected to be
    // refused went ahead, and the failure looked like a timing flake.
    QCoreApplication::setApplicationName(
        QStringLiteral("download_thread_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    const int rc = Catch::Session().run(argc, argv);

    // One settings file per process would otherwise pile up.
    QFile::remove(QSettings().fileName());
    return rc;
}

TEST_CASE("DownloadThread does not cache an unverified download", "[download][cache]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(128 * 1024, 59);

    // No expected hash, so nothing can vouch for what arrived.
    auto dt = makeDownload(scratch, payload, QStringLiteral("unver-src.img"),
                           QStringLiteral("unver-dest.img"));
    dt->setVerifyEnabled(false);

    const QString cachePath = scratch.filePath(QStringLiteral("unver-cached.img"));
    dt->setCacheFile(cachePath, payload.size());

    const Outcome outcome = runToCompletion(*dt, 120000);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The write itself still lands...
    CHECK(readFile(scratch.filePath(QStringLiteral("unver-dest.img"))).left(payload.size()) ==
          payload);

    // ...but the cache entry is discarded, because a later run would have no
    // way to tell whether what it found was the image it asked for. Note the
    // bytes are written first and removed afterwards rather than never
    // written, so an unverified download still pays the I/O cost.
    CHECK_FALSE(QFileInfo::exists(cachePath));
}

TEST_CASE("DownloadThread skips customisation without an init format",
          "[download][customise]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString source = scratch.filePath(QStringLiteral("noinit-src.img"));
    REQUIRE(buildPartitionedImage(source, 48));

    const QString dest = scratch.filePath(QStringLiteral("noinit-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(56 * 1024 * 1024, '\0')));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(), dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // Settings supplied, but no init format. The pass is gated on the format
    // as well as the content, so nothing is written -- worth pinning, because
    // from the caller's side it looks like customisation was requested and
    // the failure is completely silent.
    dt.setImageCustomisation("dtoverlay=disable-bt", "quiet", QByteArray(), QByteArray(),
                             QByteArray(), QByteArray(), ImageOptions::AdvancedOptions());

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    const QByteArray config = readFromBootPartition(dest, QStringLiteral("config.txt"));
    CHECK_FALSE(config.contains("dtoverlay=disable-bt"));
}

// The imager zeroes the last part of the card after writing, specifically to
// catch counterfeits: a card that reports 64GB but only has 8GB of real flash
// accepts the write and silently discards most of it. A device whose tail
// returns errors is indistinguishable from that, which makes it a faithful
// stand-in for the real thing.
TEST_CASE("DownloadThread detects a card that lies about its capacity",
          "[download][faulty]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");

    // Claims 64MB, only the first 48MB are real.
    FaultyDevice device(64, 48);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    // Small enough to fit inside the working region, so the *only* thing that
    // can catch this is the end-of-device check.
    const QByteArray payload = patternOfSize(4 * 1024 * 1024, 109);
    const QString source = scratch.filePath(QStringLiteral("counterfeit-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    REQUIRE(outcome.finished);

    // The image itself wrote fine. Reporting success here would hand the user
    // a card that appears to have worked and will not boot.
    CHECK_FALSE(outcome.succeeded);
    INFO("error: " << outcome.errorMessage.toStdString());
    CHECK(outcome.errorMessage.contains(QStringLiteral("capacity"), Qt::CaseInsensitive));
}

TEST_CASE("DownloadThread can be told to skip the end-of-device check",
          "[download][faulty]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no faulty device can be built");

    FaultyDevice device(64, 48);
    REQUIRE_FAULTY(device);

    ScratchDir scratch;
    const QByteArray payload = patternOfSize(4 * 1024 * 1024, 113);
    const QString source = scratch.filePath(QStringLiteral("skipend-src.img"));
    REQUIRE(writeFile(source, payload));

    DownloadThread dt(QByteArray("file://") + source.toUtf8(),
                      device.path().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    // The escape hatch for a device that legitimately refuses writes at the
    // very end. With it set, the same card that was rejected above completes.
    dt.setDebugSkipEndOfDevice(true);
    dt.setDebugAsyncIO(false);
    dt.setDebugDirectIO(false);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);
}

// ══════════════════════════════════════════════════════════════════════════
// What the user is told when a write fails
//
// _fileErrorToString() is the last step between a FileError and the dialog
// somebody reads at two in the morning with a card that will not write. It
// was entirely uncovered. The failures worth guarding against are quiet
// ones: a forgotten case silently degrading to "Unknown storage error", or a
// message that keeps its %1 placeholder because the .arg() was dropped.
// ══════════════════════════════════════════════════════════════════════════

namespace {

class ErrorStrings : public DownloadThread
{
public:
    ErrorStrings() : DownloadThread("file:///nonexistent", "", "") {}
    using DownloadThread::_fileErrorToString;
};

// Every error the enum defines, with the two that are deliberately silent
// kept separate.
const std::vector<rpi_imager::FileError> kReportedErrors = {
    rpi_imager::FileError::kOpenError,
    rpi_imager::FileError::kWriteError,
    rpi_imager::FileError::kReadError,
    rpi_imager::FileError::kSeekError,
    rpi_imager::FileError::kSizeError,
    rpi_imager::FileError::kCloseError,
    rpi_imager::FileError::kLockError,
    rpi_imager::FileError::kSyncError,
    rpi_imager::FileError::kFlushError,
    rpi_imager::FileError::kTimeout,
};

} // namespace

TEST_CASE("Every storage error says something", "[download][errors]")
{
    ErrorStrings t;
    for (auto e : kReportedErrors) {
        const QString msg = t._fileErrorToString(e, QStringLiteral("verification"));
        INFO("error " << static_cast<int>(e) << ": " << msg.toStdString());
        CHECK_FALSE(msg.isEmpty());
    }
}

TEST_CASE("Success and cancellation are not reported as errors",
          "[download][errors]")
{
    // Cancelling is something the user did; telling them it failed would be
    // both wrong and alarming.
    ErrorStrings t;
    CHECK(t._fileErrorToString(rpi_imager::FileError::kSuccess).isEmpty());
    CHECK(t._fileErrorToString(rpi_imager::FileError::kCancelled).isEmpty());
}

TEST_CASE("No error message reaches the user with its placeholder intact",
          "[download][errors]")
{
    // Several of these messages name the operation. Dropping the .arg() puts
    // a literal %1 in front of the user, which is the kind of thing that
    // survives review because the code reads correctly.
    ErrorStrings t;
    for (auto e : kReportedErrors) {
        const QString withOp = t._fileErrorToString(e, QStringLiteral("verification"));
        const QString without = t._fileErrorToString(e);
        INFO("error " << static_cast<int>(e));
        CHECK_FALSE(withOp.contains(QStringLiteral("%1")));
        CHECK_FALSE(without.contains(QStringLiteral("%1")));
    }
}

TEST_CASE("The operation name is used, and has a sensible default",
          "[download][errors]")
{
    ErrorStrings t;
    const QString named =
        t._fileErrorToString(rpi_imager::FileError::kWriteError,
                             QStringLiteral("verification"));
    CHECK(named.contains(QStringLiteral("verification")));

    // Called without one -- as the internal callers do -- it still reads as
    // a sentence rather than trailing off.
    const QString unnamed = t._fileErrorToString(rpi_imager::FileError::kWriteError);
    INFO(unnamed.toStdString());
    CHECK(unnamed.contains(QStringLiteral("storage operation")));
}

TEST_CASE("Each storage error is distinguishable from the unknown one",
          "[download][errors]")
{
    // A case dropped from the switch degrades to the catch-all, which reads
    // plausibly and tells the user nothing. Comparing against the message an
    // unhandled value produces is what catches that.
    ErrorStrings t;
    const auto bogus = static_cast<rpi_imager::FileError>(9999);
    const QString unknown = t._fileErrorToString(bogus, QStringLiteral("verification"));
    REQUIRE_FALSE(unknown.isEmpty());

    for (auto e : kReportedErrors) {
        const QString msg = t._fileErrorToString(e, QStringLiteral("verification"));
        INFO("error " << static_cast<int>(e) << " gave: " << msg.toStdString());
        CHECK(msg != unknown);
    }
}

TEST_CASE("Storage errors do not share a message with each other",
          "[download][errors]")
{
    // Two errors reading identically means one of them was copied and not
    // edited, and the user is told the wrong thing about their card.
    ErrorStrings t;
    std::vector<QString> seen;
    for (auto e : kReportedErrors) {
        const QString msg = t._fileErrorToString(e, QStringLiteral("verification"));
        INFO("error " << static_cast<int>(e) << ": " << msg.toStdString());
        CHECK(std::find(seen.begin(), seen.end(), msg) == seen.end());
        seen.push_back(msg);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Reporting the verification that happened, and only that one
//
// eventVerify becomes a HashComputation entry in the performance report --
// the file somebody exports and attaches to a bug report about a card that
// will not boot. A verification that was switched off reported the same
// success as a clean read-back, so the report said post-write verification
// had passed, with an empty verify hash beside it.
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("A write with verification off reports no verification",
          "[download][verify]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 41);
    auto dt = makeDownload(scratch, payload, QStringLiteral("noverify-src.img"),
                           QStringLiteral("noverify-dst.img"));
    dt->setVerifyEnabled(false);

    int events = 0;
    QObject ctx;
    QObject::connect(dt.get(), &DownloadThread::eventVerify, &ctx,
                     [&events](quint32, bool, QByteArray, QByteArray) { ++events; });

    const Outcome out = runToCompletion(*dt);
    INFO("error: " << out.errorMessage.toStdString());
    REQUIRE(out.finished);
    CHECK(out.succeeded);

    // Nothing was verified, so nothing is claimed.
    CHECK(events == 0);
}

TEST_CASE("A verified write reports the verification it performed",
          "[download][verify]")
{
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(512 * 1024, 43);
    auto dt = makeDownload(scratch, payload, QStringLiteral("verify-src.img"),
                           QStringLiteral("verify-dst.img"));
    dt->setVerifyEnabled(true);

    int events = 0;
    bool reportedSuccess = false;
    QByteArray writeHash, verifyHash;
    QObject ctx;
    QObject::connect(dt.get(), &DownloadThread::eventVerify, &ctx,
                     [&](quint32, bool ok, QByteArray w, QByteArray v) {
                         ++events;
                         reportedSuccess = ok;
                         writeHash = w;
                         verifyHash = v;
                     });

    const Outcome out = runToCompletion(*dt, 240000);
    INFO("error: " << out.errorMessage.toStdString());
    REQUIRE(out.finished);
    REQUIRE(out.succeeded);

    REQUIRE(events == 1);
    CHECK(reportedSuccess);
    // And the hashes it reports are real ones that agree, rather than the
    // empty pair a skipped verification used to record.
    CHECK_FALSE(writeHash.isEmpty());
    CHECK_FALSE(verifyHash.isEmpty());
    CHECK(writeHash == verifyHash);
}

// ══════════════════════════════════════════════════════════════════════════
// What the user is told when a write fails
//
// _onWriteError() asks the device what went wrong and turns the answer into
// one sentence. The answers are not interchangeable: a write-protect switch,
// a full card, a counterfeit one and a disconnected reader each need
// something different done about them, and the generic fallback tells
// somebody to check three things when only one is wrong.
//
// Only the I/O-error class is reachable through a real faulty device -- the
// rest are reported by the platform layer, several of them only on Windows
// -- so the classification is driven directly instead.
// ══════════════════════════════════════════════════════════════════════════

namespace {

// The real Linux implementation with one answer replaced, so the other
// twenty-nine members of the interface behave as they always do.
class ClassifyingDevice : public rpi_imager::PlatformFileOperations
{
public:
    explicit ClassifyingDevice(rpi_imager::WriteErrorClass klass) : _klass(klass) {}

    rpi_imager::WriteErrorClass ClassifyLastWriteError() const override
    {
        return _klass;
    }

private:
    rpi_imager::WriteErrorClass _klass;
};

class FailingWrite : public DownloadThread
{
public:
    FailingWrite() : DownloadThread("file:///nonexistent", "", "") {}

    void deviceReports(rpi_imager::WriteErrorClass klass)
    {
        _file = std::make_shared<ClassifyingDevice>(klass);
    }

    void beCancelled() { _cancelled = true; }

    // A real device, for the one failure that can be produced to order.
    bool openRealDevice(const char *path)
    {
        auto ops = rpi_imager::FileOperations::Create();
        if (!ops || ops->OpenDevice(path) != rpi_imager::FileError::kSuccess)
            return false;
        _file = std::move(ops);
        return true;
    }

    rpi_imager::FileOperations *device() { return _file.get(); }

    using DownloadThread::_onWriteError;
};

} // namespace

TEST_CASE("Each kind of write failure is explained in its own terms",
          "[downloadthread][writeerror]")
{
    struct Case {
        rpi_imager::WriteErrorClass klass;
        const char *mustMention;
        const char *tag;
    };

    auto c = GENERATE(
        // Names the switch to look for, rather than "not writable".
        Case{rpi_imager::WriteErrorClass::kWriteProtected, "write-protect", "write protected"},
        // The card is too small; a larger one is the answer, not retrying.
        Case{rpi_imager::WriteErrorClass::kDiskFull, "larger", "disk full"},
        // The one worth saying out loud: a card that fails mid-write is
        // often not the size it claims.
        Case{rpi_imager::WriteErrorClass::kMediaError, "counterfeit", "media error"},
        Case{rpi_imager::WriteErrorClass::kIoDeviceError, "disconnected", "I/O error"},
        Case{rpi_imager::WriteErrorClass::kInvalidParameter, "reconnecting", "invalid parameter"},
        Case{rpi_imager::WriteErrorClass::kAccessDenied, "another application", "access denied"},
        // Windows only, and undiagnosable without being named: the write is
        // refused by a security feature, not by the card.
        Case{rpi_imager::WriteErrorClass::kAccessDeniedControlledFolderAccess,
             "Controlled Folder Access", "controlled folder access"});

    FailingWrite thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.deviceReports(c.klass);

    thread._onWriteError();

    REQUIRE(failed.count() == 1);
    const QString message = failed.at(0).at(0).toString();
    INFO(c.tag << " -> " << message.toStdString());
    CHECK_THAT(message.toStdString(), Catch::Matchers::ContainsSubstring(c.mustMention));
}

TEST_CASE("A card that really fills up is described as full, end to end",
          "[downloadthread][writeerror]")
{
    // The cases above hand the classification over directly, because most of
    // these failures cannot be produced on demand. One can: /dev/full is a
    // character device that fails every write with ENOSPC. So this drives the
    // whole chain -- a real write to a real device that really fails, the
    // errno the kernel set, the platform classifying it, and the sentence the
    // user ends up reading.
    //
    // Worth doing because that chain was broken until recently on everything
    // but Windows: the POSIX layer never classified anything, so a full card
    // produced the generic message asking the user to check three things.
    if (::access("/dev/full", W_OK) != 0)
        SKIP("/dev/full is not available on this host");

    FailingWrite thread;
    if (!thread.openRealDevice("/dev/full"))
        SKIP("/dev/full could not be opened as a device");

    rpi_test::SignalLog failed(&thread, &DownloadThread::error);

    std::vector<std::uint8_t> block(4096, 0x5A);
    const rpi_imager::FileError wrote =
        thread.device()->WriteSequential(block.data(), block.size());
    REQUIRE(wrote != rpi_imager::FileError::kSuccess);

    thread._onWriteError();

    REQUIRE(failed.count() == 1);
    const QString message = failed.at(0).at(0).toString();
    INFO("message: " << message.toStdString());
    // Named for what it is, and pointing at the one thing that helps.
    CHECK_THAT(message.toStdString(), Catch::Matchers::ContainsSubstring("full"));
    CHECK_THAT(message.toStdString(), Catch::Matchers::ContainsSubstring("larger"));
    // And not the three-questions fallback.
    CHECK_THAT(message.toStdString(),
               !Catch::Matchers::ContainsSubstring("sufficient space"));
}

TEST_CASE("An unrecognised write failure still says something useful",
          "[downloadthread][writeerror]")
{
    // The fallback. It has to cover the ground the specific messages would
    // have, since there is nothing else to go on.
    FailingWrite thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.deviceReports(rpi_imager::WriteErrorClass::kUnknown);

    thread._onWriteError();

    REQUIRE(failed.count() == 1);
    const std::string message = failed.at(0).at(0).toString().toStdString();
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("writable"));
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("space"));
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("write-protected"));
}

TEST_CASE("A cancelled write is not reported as a failure",
          "[downloadthread][writeerror]")
{
    // Cancelling makes the write in flight fail, which is expected rather
    // than wrong. Reporting it would show the user an error dialog for
    // something they just asked for.
    FailingWrite thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.deviceReports(rpi_imager::WriteErrorClass::kIoDeviceError);
    thread.beCancelled();

    thread._onWriteError();

    CHECK(failed.count() == 0);
}

// ══════════════════════════════════════════════════════════════════════════
// When the write pauses to sync
//
// _periodicSync() decides whether to stop and flush part-way through a
// write. Both answers matter. Syncing too rarely leaves more in the page
// cache than a card can absorb at the end; syncing too often is the reason
// a write to an SD card over USB can take several times longer than it
// should, because each fsync() waits on the slowest device in the chain.
//
// The decision is a pure function of a handful of protected members, and it
// reports itself through eventPeriodicSync, so it can be driven directly
// rather than by writing an image and waiting.
// ══════════════════════════════════════════════════════════════════════════

namespace {

class SyncCountingDevice : public rpi_imager::PlatformFileOperations
{
public:
    bool directIo = false;
    bool asyncSupported = false;
    int queueDepth = 1;
    int pendingWrites = 0;

    bool IsAsyncIOSupported() const override { return asyncSupported; }
    int GetAsyncQueueDepth() const override { return queueDepth; }
    int GetPendingWriteCount() const override { return pendingWrites; }
    bool flushFails = false;
    bool forceSyncFails = false;
    int flushes = 0;
    int forceSyncs = 0;

    bool IsDirectIOEnabled() const override { return directIo; }

    rpi_imager::FileError Flush() override
    {
        ++flushes;
        return flushFails ? rpi_imager::FileError::kFlushError
                          : rpi_imager::FileError::kSuccess;
    }

    rpi_imager::FileError ForceSync() override
    {
        ++forceSyncs;
        return forceSyncFails ? rpi_imager::FileError::kSyncError
                              : rpi_imager::FileError::kSuccess;
    }
};

class SyncDecision : public DownloadThread
{
public:
    SyncDecision() : DownloadThread("file:///nonexistent", "", "")
    {
        _debugPeriodicSync = true;
        device = std::make_shared<SyncCountingDevice>();
        _file = device;
        _lastSyncTime.start();
        _lastSyncBytes = 0;
    }

    // Enough written since the last sync to be worth one.
    void haveWritten(qint64 bytes) { _bytesWritten = bytes; }
    qint64 syncThreshold() const { return _syncConfig.syncIntervalBytes; }
    void beCancelled() { _cancelled = true; }
    void disableViaDebugOption() { _debugPeriodicSync = false; }

    // Make the time-based trigger eligible without waiting for the real
    // interval, which is five seconds. Nothing else in the decision changes,
    // so what remains is whether the "and something was written" clause
    // holds it back.
    void makeAnyElapsedTimeEnough() { _syncConfig.syncIntervalMs = 0; }

    std::shared_ptr<SyncCountingDevice> device;

    using DownloadThread::_periodicSync;
};

} // namespace

TEST_CASE("A write syncs once it has put down enough data",
          "[downloadthread][periodicsync]")
{
    SyncDecision thread;
    rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
    thread.haveWritten(thread.syncThreshold());

    thread._periodicSync();

    REQUIRE(synced.count() == 1);
    CHECK(synced.at(0).at(1).toBool() == true);
    CHECK(thread.device->forceSyncs == 1);
}

TEST_CASE("Direct I/O writes do not stop to sync",
          "[downloadthread][periodicsync]")
{
    // With O_DIRECT the data is not sitting in the page cache, so there is
    // nothing for fsync() to push and every call is pure delay -- which on a
    // card behind a USB reader is where a write's time goes. The sync at the
    // end still happens; this is only about the ones part-way through.
    SyncDecision thread;
    rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
    thread.device->directIo = true;
    thread.haveWritten(thread.syncThreshold() * 4);

    thread._periodicSync();

    CHECK(synced.count() == 0);
    CHECK(thread.device->forceSyncs == 0);
    CHECK(thread.device->flushes == 0);
}

TEST_CASE("Time passing on its own is not a reason to sync",
          "[downloadthread][periodicsync]")
{
    // The time-based trigger requires that something was actually written.
    // Without that clause a stalled write would sync on a timer, repeatedly
    // flushing nothing.
    SyncDecision thread;
    rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
    thread.makeAnyElapsedTimeEnough();
    thread.haveWritten(0);

    thread._periodicSync();

    CHECK(synced.count() == 0);
    CHECK(thread.device->forceSyncs == 0);
}

TEST_CASE("A cancelled write does not stop to sync",
          "[downloadthread][periodicsync]")
{
    SyncDecision thread;
    rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
    thread.haveWritten(thread.syncThreshold());
    thread.beCancelled();

    thread._periodicSync();

    CHECK(synced.count() == 0);
}

TEST_CASE("Periodic sync can be turned off for diagnosis",
          "[downloadthread][periodicsync]")
{
    SyncDecision thread;
    rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
    thread.disableViaDebugOption();
    thread.haveWritten(thread.syncThreshold() * 4);

    thread._periodicSync();

    CHECK(synced.count() == 0);
}

TEST_CASE("A sync that fails is recorded as having failed",
          "[downloadthread][periodicsync]")
{
    // The event is what the performance capture is read from afterwards, so
    // a failed sync reported as a success would hide the thing somebody is
    // looking for.
    SECTION("the flush fails") {
        SyncDecision thread;
        rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
        thread.device->flushFails = true;
        thread.haveWritten(thread.syncThreshold());

        thread._periodicSync();

        REQUIRE(synced.count() == 1);
        CHECK(synced.at(0).at(1).toBool() == false);
        CHECK(thread.device->forceSyncs == 0);
    }

    SECTION("the flush works and the sync fails") {
        SyncDecision thread;
        rpi_test::SignalLog synced(&thread, &DownloadThread::eventPeriodicSync);
        thread.device->forceSyncFails = true;
        thread.haveWritten(thread.syncThreshold());

        thread._periodicSync();

        REQUIRE(synced.count() == 1);
        CHECK(synced.at(0).at(1).toBool() == false);
        CHECK(thread.device->flushes == 1);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Throughput belongs to one write, not to the process
//
// The figures shown while a card is written -- the rate, and whether
// storage or the network is holding things up -- were measured through
// function-local statics, so every DownloadThread in the process shared one
// timer and one byte count. "Write another card" reaches this: the second
// write began with the first write's final byte count as its baseline, so
// its opening delta was negative and the rate it reported was zero.
//
// The state is per-instance now. These cases hold it there.
// ══════════════════════════════════════════════════════════════════════════

namespace {

class ThroughputProbe : public DownloadThread
{
public:
    ThroughputProbe() : DownloadThread("file:///nonexistent", "", "")
    {
        device = std::make_shared<SyncCountingDevice>();
        _file = device;
    }

    void haveWritten(qint64 bytes) { _bytesWritten = bytes; }
    bool hasBaseline() const { return _throughputTimerStarted; }
    qint64 baseline() const { return _lastThroughputBytes; }
    BottleneckState bottleneck() const { return _currentBottleneck; }

    std::shared_ptr<SyncCountingDevice> device;

    using DownloadThread::_updateBottleneckState;
};

} // namespace

TEST_CASE("A write measures its own throughput from its own start",
          "[downloadthread][throughput]")
{
    // The first pass takes a baseline rather than reporting a rate, because
    // there is nothing yet to compare against.
    ThroughputProbe first;
    first.haveWritten(64 * 1024 * 1024);
    first._updateBottleneckState();

    CHECK(first.hasBaseline());
    CHECK(first.baseline() == 64 * 1024 * 1024);
}

TEST_CASE("A second write does not inherit the first one's baseline",
          "[downloadthread][throughput]")
{
    // The bug this replaced. With the state shared, the second write found a
    // baseline already set to the first write's total, and measured its own
    // opening bytes against it.
    ThroughputProbe first;
    first.haveWritten(64 * 1024 * 1024);
    first._updateBottleneckState();
    REQUIRE(first.baseline() == 64 * 1024 * 1024);

    ThroughputProbe second;
    CHECK_FALSE(second.hasBaseline());

    second.haveWritten(1 * 1024 * 1024);
    second._updateBottleneckState();

    CHECK(second.baseline() == 1 * 1024 * 1024);
    CHECK(first.baseline() == 64 * 1024 * 1024);
}

TEST_CASE("Storage is named as the bottleneck, and unnamed again",
          "[downloadthread][throughput]")
{
    // What this tells the user is why their write is slow. Naming the card
    // when the queue is nearly empty, or staying quiet when it is full,
    // sends them looking in the wrong place.
    //
    // Both directions are asserted, and in that order, because None is also
    // the state it starts in -- a case that only checked for None would pass
    // without the detection ever having run.
    //
    // The state changes are held behind 500ms of hysteresis, so that it does
    // not flicker between causes while a write settles. There is no way to
    // wind that timer forward, so the wait is real. Once, not per section.
    ThroughputProbe thread;
    thread.device->asyncSupported = true;
    thread.device->queueDepth = 16;
    rpi_test::SignalLog states(&thread, &DownloadThread::bottleneckStateChanged);

    // A queue over three quarters full: the card cannot take writes as fast
    // as they arrive.
    thread.device->pendingWrites = 13;
    thread._updateBottleneckState();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    thread._updateBottleneckState();

    REQUIRE(thread.bottleneck() == DownloadThread::BottleneckState::Storage);
    REQUIRE(states.count() >= 1);
    CHECK(states.at(states.count() - 1).at(0).toInt()
          == static_cast<int>(DownloadThread::BottleneckState::Storage));

    // Room in the queue again: the card has caught up and is no longer what
    // is holding the write back.
    thread.device->pendingWrites = 2;
    thread._updateBottleneckState();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    thread._updateBottleneckState();

    CHECK(thread.bottleneck() == DownloadThread::BottleneckState::None);
}

// ══════════════════════════════════════════════════════════════════════════
// Verification: reading the card back and not believing the write
//
// _verify() reads the image back off the device and compares its hash with
// what was written. The mismatch branch is the entire reason the feature
// exists, and it was uncovered -- so the one outcome nothing tested was the
// one where verification has something to say.
//
// A device that takes writes and reads back something else is the
// counterfeit card, and it is why this is not optional for a lot of people.
// A real device cannot be made to do that, so the read is faked.
// ══════════════════════════════════════════════════════════════════════════

namespace {

// Reads back whatever it is told to, regardless of what was written.
class ReadbackDevice : public rpi_imager::PlatformFileOperations
{
public:
    QByteArray readsBack;
    bool readFails = false;
    std::uint64_t written = 0;

    std::uint64_t Tell() const override { return written; }
    rpi_imager::FileError Seek(std::uint64_t position) override
    {
        _pos = position;
        return rpi_imager::FileError::kSuccess;
    }
    void PrepareForSequentialRead(std::uint64_t, std::uint64_t) override {}

    rpi_imager::FileError ReadSequential(std::uint8_t* data, std::size_t size,
                                         std::size_t& bytes_read) override
    {
        if (readFails)
            return rpi_imager::FileError::kReadError;
        const std::size_t available =
            static_cast<std::size_t>(readsBack.size()) > _pos
                ? static_cast<std::size_t>(readsBack.size()) - _pos : 0;
        bytes_read = std::min(size, available);
        memcpy(data, readsBack.constData() + _pos, bytes_read);
        _pos += bytes_read;
        return rpi_imager::FileError::kSuccess;
    }

private:
    std::size_t _pos = 0;
};

class Verification : public DownloadThread
{
public:
    Verification() : DownloadThread("file:///nonexistent", "", "")
    {
        device = std::make_shared<ReadbackDevice>();
        _file = device;
        _verifyEnabled = true;
    }

    // What the write believed it put down.
    void wroteThis(const QByteArray &data)
    {
        _writehash.addData(data.constData(), data.size());
        device->written = static_cast<std::uint64_t>(data.size());
    }

    void cardReadsBack(const QByteArray &data) { device->readsBack = data; }

    std::shared_ptr<ReadbackDevice> device;

    using DownloadThread::_verify;
};

} // namespace

TEST_CASE("Verification passes when the card holds what was written",
          "[downloadthread][verify]")
{
    const QByteArray image(64 * 1024, '\xA5');

    Verification thread;
    rpi_test::SignalLog verified(&thread, &DownloadThread::eventVerify);
    thread.wroteThis(image);
    thread.cardReadsBack(image);

    CHECK(thread._verify());

    REQUIRE(verified.count() == 1);
    CHECK(verified.at(0).at(1).toBool() == true);
}

TEST_CASE("Verification fails when the card holds something else",
          "[downloadthread][verify]")
{
    // The counterfeit card: the write was accepted, the read gives back
    // something different. Without this the user is told the write
    // succeeded and finds out when the board will not boot.
    QByteArray image(64 * 1024, '\xA5');
    QByteArray whatItActuallyHolds = image;
    whatItActuallyHolds[40000] = '\x00';   // one byte, past the start

    Verification thread;
    rpi_test::SignalLog verified(&thread, &DownloadThread::eventVerify);
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.wroteThis(image);
    thread.cardReadsBack(whatItActuallyHolds);

    CHECK_FALSE(thread._verify());

    REQUIRE(verified.count() == 1);
    CHECK(verified.at(0).at(1).toBool() == false);
    REQUIRE(failed.count() == 1);
    CHECK_THAT(failed.at(0).at(0).toString().toStdString(),
               Catch::Matchers::ContainsSubstring("different from what was written"));
}

TEST_CASE("Verification reports both hashes either way",
          "[downloadthread][verify]")
{
    // The event carries the two digests, and a support conversation about a
    // failed write starts with them. A mismatch reported with equal hashes,
    // or with one of them empty, ends that conversation early.
    QByteArray image(32 * 1024, '\x5A');
    QByteArray different = image;
    different[1000] = '\xFF';

    Verification thread;
    rpi_test::SignalLog verified(&thread, &DownloadThread::eventVerify);
    thread.wroteThis(image);
    thread.cardReadsBack(different);

    thread._verify();

    REQUIRE(verified.count() == 1);
    const QString expected = verified.at(0).at(2).toString();
    const QString found = verified.at(0).at(3).toString();
    CHECK(expected.length() > 0);
    CHECK(found.length() > 0);
    CHECK(expected != found);
}

TEST_CASE("A card that cannot be read back is called broken",
          "[downloadthread][verify]")
{
    // Distinct from a mismatch: nothing came back at all, so there is
    // nothing to compare and the device itself is the problem.
    const QByteArray image(16 * 1024, '\x11');

    Verification thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.wroteThis(image);
    thread.device->readFails = true;

    CHECK_FALSE(thread._verify());

    REQUIRE(failed.count() == 1);
    CHECK_THAT(failed.at(0).at(0).toString().toStdString(),
               Catch::Matchers::ContainsSubstring("may be broken"));
}

// The read-back that decides whether the customisation actually reached the
// card.
//
// After a write, DownloadThread reads its own customisation files back off the
// media and compares them with what it recorded writing. The success path was
// covered; the failure path was not, and it is the one that matters. A card
// whose config.txt or user-data never landed boots without the hostname, the
// user account or the Wi-Fi the user asked for -- and if the read-back does not
// fail the write, they are told it completed and find out when the board does
// not appear on the network.
//
// There is a deliberate asymmetry here worth pinning: a read that cannot be
// performed at all -- an unreadable partition, an unexpected layout -- lets the
// write stand, because that is not evidence the customisation is bad and
// failing a good card is worse. A read that succeeds and disagrees fails it.
// Both directions are checked.
//
// The verification needs a real FAT partition to read, so these use the same
// mkfs.vfat-built image the customisation cases use, and reach the protected
// members through a subclass rather than driving a whole write.
class CustomisationVerifier : public DownloadThread
{
public:
    CustomisationVerifier(const QByteArray &src, const QByteArray &dst)
        : DownloadThread(src, dst, QByteArray()) {}

    // Open the destination for reading without going through
    // _openAndPrepareDevice(), which zeroes the first and last megabyte -- and
    // would take the partition table this test needs with it.
    bool openForReadBack(const QString &path)
    {
        _file = rpi_imager::FileOperations::Create();
        if (_file->OpenDevice(path.toStdString()) != rpi_imager::FileError::kSuccess)
            return false;
        std::uint64_t size = 0;
        if (_file->GetSize(size) != rpi_imager::FileError::kSuccess)
            return false;
        _bytesWritten.store(size);
        return true;
    }

    void expect(const QString &name, const QByteArray &contents)
    {
        _recordCustomisationWrite(name, contents);
    }

    bool expectationCount() const { return _customisationDigests.size(); }
    void setVerifyReadBack(bool on) { _verifyEnabled = on; }

    using DownloadThread::_verifyCustomisation;
};

// Plant a file directly in the boot partition, standing in for what the
// customisation pass would have written.
bool plantInBootPartition(const QString &devicePath, const QString &name,
                          const QByteArray &contents)
{
    auto ops = rpi_imager::FileOperations::Create();
    if (ops->OpenDevice(devicePath.toStdString()) != rpi_imager::FileError::kSuccess)
        return false;
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition *fat = dw.fatPartition(1);
    if (!fat)
        return false;
    fat->writeFile(name, contents);
    return true;
}

TEST_CASE("Customisation read-back passes when the card kept what was written",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-ok.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    const QByteArray contents = QByteArray("hostname=raspberrypi\n");
    REQUIRE(plantInBootPartition(dest, QStringLiteral("user-data"), contents));

    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("user-data"), contents);

    CHECK(v._verifyCustomisation());
}

TEST_CASE("Customisation read-back fails when a file never reached the card",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-missing.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    // Nothing planted: the customisation pass believes it wrote a file that
    // is not there. A board written this way comes up with no user account.
    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("user-data"), QByteArray("hostname=raspberrypi\n"));

    CHECK_FALSE(v._verifyCustomisation());
}

TEST_CASE("Customisation read-back fails when a file came back truncated",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-short.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    // The card kept less than was written -- the classic result of a write
    // that was not synced before the card was pulled.
    REQUIRE(plantInBootPartition(dest, QStringLiteral("user-data"),
                                 QByteArray("hostname=ras")));

    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("user-data"), QByteArray("hostname=raspberrypi\n"));

    CHECK_FALSE(v._verifyCustomisation());
}

TEST_CASE("Customisation read-back fails when a file came back corrupted",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-corrupt.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    // Same length, different bytes: a size check alone would pass this, which
    // is why the small files are content-checked.
    const QByteArray written = QByteArray("hostname=raspberrypi\n");
    QByteArray onCard = written;
    onCard[9] = 'X';
    REQUIRE(onCard.size() == written.size());
    REQUIRE(plantInBootPartition(dest, QStringLiteral("user-data"), onCard));

    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("user-data"), written);

    CHECK_FALSE(v._verifyCustomisation());
}

TEST_CASE("Customisation read-back checks every file it recorded",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-many.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    // Several files, one of them wrong. Stopping at the first match would let
    // this through.
    const QByteArray good = QByteArray("ok\n");
    REQUIRE(plantInBootPartition(dest, QStringLiteral("config.txt"), good));
    REQUIRE(plantInBootPartition(dest, QStringLiteral("cmdline.txt"), good));
    REQUIRE(plantInBootPartition(dest, QStringLiteral("user-data"),
                                 QByteArray("wrong\n")));

    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("config.txt"), good);
    v.expect(QStringLiteral("cmdline.txt"), good);
    v.expect(QStringLiteral("user-data"), QByteArray("right\n"));

    CHECK_FALSE(v._verifyCustomisation());
}

TEST_CASE("Nothing customised means nothing to read back",
          "[download][customise][verify]")
{
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-none.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    // No expectations recorded, so there is nothing to check and no partition
    // to go looking for -- it must not fail a write that asked for no
    // customisation at all.
    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));

    CHECK(v._verifyCustomisation());
}

TEST_CASE("A card that cannot be read back is not failed for it",
          "[download][customise][verify]")
{
    // The deliberate asymmetry. If the partition cannot be found or read, that
    // is not evidence the customisation is bad, and failing a good card over an
    // unreadable check is worse than letting it stand with a warning. A read
    // that succeeds and disagrees is a different matter -- see the cases above.
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-nofat.img"));
    REQUIRE(writeFile(dest, QByteArray(4 * 1024 * 1024, '\0')));

    CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
    REQUIRE(v.openForReadBack(dest));
    v.expect(QStringLiteral("user-data"), QByteArray("hostname=raspberrypi\n"));

    CHECK(v._verifyCustomisation());
}

TEST_CASE("A large file is still size-checked when verification is off",
          "[download][customise][verify]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");

    // Content-checking a large file means reading it back a 4 KiB block at a
    // time with direct I/O, which for a secure-boot boot.img is thousands of
    // syscalls on every write -- so the content check is done only when the
    // user has already opted into a read-back by enabling verification.
    //
    // The length is a different matter: it is recorded in the directory entry,
    // so checking it costs one seek rather than a read of the file. It used to
    // be skipped along with the content, which meant a truncated boot.img went
    // unnoticed unless verification was on -- and for a secure-boot write that
    // file is the signed bootloader payload, so a short one leaves a board
    // that will not boot.
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("verify-large.img"));
    REQUIRE(buildPartitionedImage(dest, 48));

    const qint64 kAlwaysVerifyMaxBytes = 1024 * 1024;
    const QByteArray written(kAlwaysVerifyMaxBytes + 4096, '\xA5');
    const QByteArray truncated(1024, '\xA5');
    REQUIRE(truncated.size() < written.size());
    REQUIRE(plantInBootPartition(dest, QStringLiteral("boot.img"), truncated));

    {
        CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
        REQUIRE(v.openForReadBack(dest));
        v.setVerifyReadBack(false);
        v.expect(QStringLiteral("boot.img"), written);

        CHECK_FALSE(v._verifyCustomisation());
    }

    // And with verification on, the same truncation is caught by the content
    // comparison rather than the length -- both routes reach it.
    {
        CustomisationVerifier v(QByteArray("file:///dev/null"), dest.toUtf8());
        REQUIRE(v.openForReadBack(dest));
        v.setVerifyReadBack(true);
        v.expect(QStringLiteral("boot.img"), written);

        CHECK_FALSE(v._verifyCustomisation());
    }
}

// ---------------------------------------------------------------------------
// The partition table, written last
// ---------------------------------------------------------------------------
//
// The first 512 bytes of the image are held back until everything behind them
// is on the card, so that a card only ever shows a partition whose filesystem
// is already durable. That means the last thing a write does is seek to sector
// zero and put them there.
//
// The guard on that seek was added without a test. Without it, a failed seek
// followed by a successful write puts those 512 bytes wherever the file
// position happens to be -- the end of the image -- and the card is handed back
// with no partition table and a successful write behind it. It looks blank, and
// nothing said anything went wrong.

class SeekingDevice : public rpi_imager::PlatformFileOperations
{
public:
    bool seekFails = false;
    bool writeFails = false;
    int seeks = 0;
    int sequentialWrites = 0;
    int flushes = 0;
    // Which flush to fail, counting from 1. _writeComplete() flushes twice
    // before the partition table: once for the final sync, and again just
    // before the table goes down. They are separate guards with separate
    // messages, so a fake that failed both would only ever reach the first.
    int failFlushNumber = 0;
    std::uint64_t lastSeek = ~0ULL;

    // Async off, so _writeComplete() takes the straightforward path down to
    // the partition table rather than the drain.
    bool IsAsyncIOSupported() const override { return false; }
    int GetAsyncQueueDepth() const override { return 1; }
    bool IsDirectIOEnabled() const override { return true; }

    rpi_imager::FileError Seek(std::uint64_t position) override
    {
        ++seeks;
        lastSeek = position;
        return seekFails ? rpi_imager::FileError::kSeekError
                         : rpi_imager::FileError::kSuccess;
    }

    rpi_imager::FileError WriteSequential(const std::uint8_t *, std::size_t len) override
    {
        ++sequentialWrites;
        (void)len;
        return writeFails ? rpi_imager::FileError::kWriteError
                          : rpi_imager::FileError::kSuccess;
    }

    rpi_imager::FileError Flush() override
    {
        ++flushes;
        return flushes == failFlushNumber ? rpi_imager::FileError::kFlushError
                                          : rpi_imager::FileError::kSuccess;
    }

    rpi_imager::FileError ForceSync() override { return rpi_imager::FileError::kSuccess; }
    rpi_imager::FileError Close() override { return rpi_imager::FileError::kSuccess; }
    bool IsOpen() const override { return true; }
};

class FirstBlockWriter : public DownloadThread
{
public:
    FirstBlockWriter() : DownloadThread("file:///nonexistent", "", "")
    {
        device = std::make_shared<SeekingDevice>();
        _file = device;
        _verifyEnabled = false;
        _cacheEnabled = false;
        _ejectEnabled = false;
    }

    // Stand in for the partition table the write held back. Allocated the way
    // _writeData does it, because _writeComplete frees it with qFreeAligned.
    void holdBackPartitionTable(std::size_t len = 512)
    {
        _firstBlock = static_cast<char *>(qMallocAligned(len, 4096));
        _firstBlockSize = len;
        ::memset(_firstBlock, 0xAA, len);
    }

    std::shared_ptr<SeekingDevice> device;

    using DownloadThread::_writeComplete;
};

TEST_CASE("The held-back partition table goes to sector zero",
          "[downloadthread][mbr]")
{
    FirstBlockWriter thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.holdBackPartitionTable();

    thread._writeComplete();

    CHECK(thread.device->lastSeek == 0);
    CHECK(thread.device->sequentialWrites == 1);
    CHECK(failed.count() == 0);
}

TEST_CASE("A write whose final seek fails does not report success",
          "[downloadthread][mbr]")
{
    // The 512 bytes would otherwise land at the end of the image, and the card
    // would come back looking blank with a completed write behind it.
    FirstBlockWriter thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.holdBackPartitionTable();
    thread.device->seekFails = true;

    thread._writeComplete();

    REQUIRE(failed.count() == 1);
    CHECK_THAT(failed.at(0).at(0).toString().toStdString(),
               Catch::Matchers::ContainsSubstring("partition table"));
    CHECK(thread.device->sequentialWrites == 0);
}

TEST_CASE("A write whose pre-table flush fails does not report success",
          "[downloadthread][mbr]")
{
    // Everything behind the partition table has to be durable before it
    // appears, or a host can see a partition whose filesystem is not all
    // there -- which is what prompts Windows to offer to format the card.
    // This is the second flush: the first is the final sync, guarded
    // separately below.
    FirstBlockWriter thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.holdBackPartitionTable();
    thread.device->failFlushNumber = 2;

    thread._writeComplete();

    REQUIRE(failed.count() == 1);
    // The message says a flush failed but not which one:
    // _fileErrorToString() interpolates its operation argument for write,
    // read, seek and timeout errors and ignores it for flush and sync, so the
    // "flushing image before writing partition table" this caller passes is
    // built and discarded. Left alone rather than fixed -- adding %1 to those
    // strings would invalidate their translations in every locale, for a
    // message that already tells the user what to do. What matters here is
    // that the table did not go down.
    CHECK_THAT(failed.at(0).at(0).toString().toStdString(),
               Catch::Matchers::ContainsSubstring("flushing"));
    CHECK(thread.device->sequentialWrites == 0);
    CHECK(thread.device->lastSeek == ~0ULL);
}

TEST_CASE("A write whose final sync fails does not reach the partition table",
          "[downloadthread][mbr]")
{
    // The first flush. Nothing is durable yet, so the table must not go down
    // at all -- and the message names the sync rather than the table, because
    // that is what failed.
    FirstBlockWriter thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.holdBackPartitionTable();
    thread.device->failFlushNumber = 1;

    thread._writeComplete();

    REQUIRE(failed.count() == 1);
    CHECK(thread.device->sequentialWrites == 0);
    CHECK(thread.device->lastSeek == ~0ULL);
}

TEST_CASE("A write whose partition table cannot be written does not report success",
          "[downloadthread][mbr]")
{
    FirstBlockWriter thread;
    rpi_test::SignalLog failed(&thread, &DownloadThread::error);
    thread.holdBackPartitionTable();
    thread.device->writeFails = true;

    thread._writeComplete();

    REQUIRE(failed.count() == 1);
    CHECK(thread.device->lastSeek == 0);
}

// ---------------------------------------------------------------------------
// Secure-boot packaging, when there is nothing to package
// ---------------------------------------------------------------------------
//
// _createSecureBootFiles() extracts every file from the boot partition, repacks
// them into a boot.img and signs that. Its first two refusals -- no key
// configured, key file missing -- are covered above through a whole write. The
// third is not reachable that way: by the time the packaging runs, the
// customisation pass has already written config.txt, so the partition is never
// empty.
//
// It is worth having anyway. A secure-boot card carries boot.img and a boot.sig
// over it, and the firmware refuses to boot if they do not agree. Packaging
// nothing and signing it would produce a card that fails at the bootloader,
// after the one-way OTP fuses have been programmed. Called directly, with a
// boot partition that really is empty.
//
// The refusal turns out to be defended three deep: the empty extraction, then
// the empty boot-file map, then SecureBoot::createBootImg refusing to pack
// nothing. Removing any one of them -- or the first two together -- leaves the
// outcome unchanged, so no reversion of a single guard makes these fail. They
// assert the outcome rather than any one guard, which is the property that
// matters and the one that survives the internals being rearranged.

class SecureBootPackager : public DownloadThread
{
public:
    SecureBootPackager() : DownloadThread("file:///nonexistent", "", "") {}
    using DownloadThread::_createSecureBootFiles;
};

TEST_CASE("Secure boot refuses to package an empty boot partition",
          "[download][secureboot]")
{
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");
    if (!haveOpenssl())
        SKIP("openssl is not installed, so no signing key can be generated");

    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("sb-empty.pem"));
    REQUIRE(generateRsaKey(key));
    setConfiguredRsaKey(key);

    const QString image = scratch.filePath(QStringLiteral("sb-empty.img"));
    REQUIRE(buildPartitionedImage(image, 48));

    auto ops = rpi_imager::FileOperations::Create();
    REQUIRE(ops->OpenDevice(image.toStdString()) == rpi_imager::FileError::kSuccess);
    DeviceWrapper dw(ops.get());
    DeviceWrapperFatPartition *fat = dw.fatPartition(1);
    REQUIRE(fat != nullptr);
    REQUIRE(fat->listAllFiles().isEmpty());

    SecureBootPackager packager;
    rpi_test::SignalLog failed(&packager, &DownloadThread::error);

    CHECK_FALSE(packager._createSecureBootFiles(fat));

    REQUIRE(failed.count() >= 1);
    INFO("error: " << failed.at(0).at(0).toString().toStdString());
    CHECK_FALSE(failed.at(0).at(0).toString().isEmpty());

    setConfiguredRsaKey(QString());
}

TEST_CASE("Secure boot packaging leaves no signature behind when it refuses",
          "[download][secureboot]")
{
    // Refusing has to leave the card as it was. A boot.sig with no boot.img,
    // or either of them half-written, is worse than neither: the firmware
    // would find a signature it cannot check.
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed, so no boot partition can be built");
    if (!haveOpenssl())
        SKIP("openssl is not installed, so no signing key can be generated");

    ScratchDir scratch;
    const QString key = scratch.filePath(QStringLiteral("sb-none.pem"));
    REQUIRE(generateRsaKey(key));
    setConfiguredRsaKey(key);

    const QString image = scratch.filePath(QStringLiteral("sb-none.img"));
    REQUIRE(buildPartitionedImage(image, 48));

    {
        auto ops = rpi_imager::FileOperations::Create();
        REQUIRE(ops->OpenDevice(image.toStdString()) == rpi_imager::FileError::kSuccess);
        DeviceWrapper dw(ops.get());
        DeviceWrapperFatPartition *fat = dw.fatPartition(1);
        REQUIRE(fat != nullptr);

        SecureBootPackager packager;
        CHECK_FALSE(packager._createSecureBootFiles(fat));
    }

    CHECK(readFromBootPartition(image, QStringLiteral("boot.img")).isEmpty());
    CHECK(readFromBootPartition(image, QStringLiteral("boot.sig")).isEmpty());

    setConfiguredRsaKey(QString());
}

// ---------------------------------------------------------------------------
// Resuming an interrupted download
// ---------------------------------------------------------------------------
//
// A connection that drops part-way is the normal case on a poor link, and
// DownloadThread is built to survive it: it reconnects, sets
// CURLOPT_RESUME_FROM_LARGE to how far it had got, and adds that offset to
// everything the progress callback reports afterwards.
//
// Get any of that wrong and the failure is quiet. Restarting from nothing
// wastes the download but at least writes the right bytes; resuming without
// accounting for the offset writes the remainder over the beginning, so the
// card ends up with a hole in the middle of the image and the write still
// reports success. The board then fails to boot for no visible reason.
//
// The server here answers the first GET with a full Content-Length and then
// sends fewer bytes than it promised before hanging up, which is what curl
// reports as a partial transfer, and honours Range on everything after.

TEST_CASE("An interrupted download resumes and writes the whole image",
          "[download][http][resume]")
{
    ScratchDir scratch;
    // Big enough that the drop lands well inside it, and a pattern rather than
    // a constant so bytes written at the wrong offset do not happen to match.
    const QByteArray payload = patternOfSize(3 * 1024 * 1024, 131);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("flaky.img")), payload));

    rpi_test::ResumableHttpServer server(scratch.filePath(QStringLiteral(".")),
                                         1024 * 1024);
    if (!rpi_test::havePython())
        SKIP("python3 is not installed, so no local HTTP server can be started");
    if (!server.isRunning())
        SKIP("the resumable HTTP server did not start");

    const QString dest = scratch.filePath(QStringLiteral("resume-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    DownloadThread dt(server.urlFor(QStringLiteral("flaky.img")), dest.toUtf8(),
                      QByteArray());
    dt.setVerifyEnabled(false);
    dt.setUserAgent("rpi-imager-test/1.0");
    rpi_test::SignalLog retried(&dt, &DownloadThread::eventNetworkRetry);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The retry actually happened, so this is not the plain path in disguise.
    CHECK(retried.count() >= 1);

    // And the card carries the image, not the image with its middle
    // overwritten by its own tail.
    QFile written(dest);
    REQUIRE(written.open(QIODevice::ReadOnly));
    const QByteArray onCard = written.read(payload.size());
    written.close();
    REQUIRE(onCard.size() == payload.size());
    if (onCard != payload) {
        // Say where it first went wrong rather than dumping three megabytes.
        int at = 0;
        while (at < payload.size() && onCard.at(at) == payload.at(at))
            ++at;
        INFO("first mismatch at byte " << at << " of " << payload.size());
        CHECK(onCard == payload);
    } else {
        CHECK(onCard == payload);
    }
}

TEST_CASE("A download interrupted twice still writes the whole image",
          "[download][http][resume]")
{
    // What a single drop does not reach: the arithmetic behind the resume
    // offset.
    //
    // The progress callback reports _startOffset + what curl reports, and the
    // next retry sets _startOffset from that same figure. So with one drop the
    // image comes out right whether or not the offset is added -- curl's own
    // count is enough. With two, the second resume starts from wherever the
    // first left off *according to the callback*, and if that is not the true
    // position the remainder lands at the wrong offset: a hole in the middle of
    // the image, and a write that still reports success.
    //
    // The second failure comes within five seconds of the first, so the retry
    // loop sleeps before reconnecting. That is the code's own back-off, not
    // padding, and it makes this case take about five seconds.
    ScratchDir scratch;
    const QByteArray payload = patternOfSize(3 * 1024 * 1024, 197);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("flaky2.img")), payload));

    // Two drops, the second a different distance in, so the offsets differ.
    rpi_test::ResumableHttpServer server(scratch.filePath(QStringLiteral(".")),
                                         768 * 1024, 2);
    if (!rpi_test::havePython())
        SKIP("python3 is not installed, so no local HTTP server can be started");
    if (!server.isRunning())
        SKIP("the resumable HTTP server did not start");

    const QString dest = scratch.filePath(QStringLiteral("resume-twice.img"));
    REQUIRE(writeFile(dest, QByteArray(payload.size() + (1024 * 1024), '\0')));

    DownloadThread dt(server.urlFor(QStringLiteral("flaky2.img")), dest.toUtf8(),
                      QByteArray());
    dt.setVerifyEnabled(false);
    dt.setUserAgent("rpi-imager-test/1.0");

    rpi_test::SignalLog retried(&dt, &DownloadThread::eventNetworkRetry);

    const Outcome outcome = runToCompletion(dt, kWriteTimeoutMs);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(retried.count() >= 2);

    QFile written(dest);
    REQUIRE(written.open(QIODevice::ReadOnly));
    const QByteArray onCard = written.read(payload.size());
    written.close();
    REQUIRE(onCard.size() == payload.size());
    if (onCard != payload) {
        int at = 0;
        while (at < payload.size() && onCard.at(at) == payload.at(at))
            ++at;
        INFO("first mismatch at byte " << at << " of " << payload.size());
    }
    CHECK(onCard == payload);
}

// ---------------------------------------------------------------------------
// An async submission that fails, and the buffer it is not allowed to free
// ---------------------------------------------------------------------------
//
// On the async-with-copy path _writeFile allocates an aligned buffer, copies
// the incoming block into it, and hands it to AsyncWriteSequential with a
// callback that frees it. The interface promises that callback runs exactly
// once on every path the call can return by -- bad descriptor, async
// unavailable, a previous async error, cancellation, no queue entry, submit
// failure -- so the caller must not free the buffer when submission fails.
//
// It used to. The comment left in its place records what that cost: the double
// free fires on every async submission failure, which is exactly what a card
// starting to error mid-write produces, so the process went down with "double
// free or corruption" instead of reporting the write error. The user lost the
// one message that would have told them what had happened.
//
// Nothing covered the contract. These cases drive a submission failure through
// that path. Re-adding the free is detected, though not the way you would
// expect: rather than glibc catching it and aborting, the heap corruption
// leaves the process spinning, so the reversion shows up as these cases
// hanging instead of failing. Blunt, but unambiguous -- and a reminder that a
// double free is not reliably a crash you can see.

class FailingAsyncDevice : public rpi_imager::PlatformFileOperations
{
public:
    int asyncSubmissions = 0;
    int callbackInvocations = 0;
    int syncWrites = 0;
    bool submissionFails = true;

    bool IsAsyncIOSupported() const override { return true; }
    int GetAsyncQueueDepth() const override { return 16; }
    bool SetAsyncQueueDepth(int) override { return true; }
    int GetPendingWriteCount() const override { return 0; }
    bool IsOpen() const override { return true; }
    rpi_imager::FileError Flush() override { return rpi_imager::FileError::kSuccess; }
    rpi_imager::FileError ForceSync() override { return rpi_imager::FileError::kSuccess; }
    rpi_imager::FileError Close() override { return rpi_imager::FileError::kSuccess; }

    rpi_imager::FileError WriteSequential(const std::uint8_t *, std::size_t) override
    {
        ++syncWrites;
        return rpi_imager::FileError::kSuccess;
    }

    // Honours the documented contract: the callback runs once whatever is
    // returned. A fake that skipped it on failure would leak the buffer and
    // hide the very thing under test.
    rpi_imager::FileError AsyncWriteSequential(const std::uint8_t *, std::size_t size,
                                               AsyncWriteCallback callback) override
    {
        ++asyncSubmissions;
        const rpi_imager::FileError result = submissionFails
            ? rpi_imager::FileError::kWriteError
            : rpi_imager::FileError::kSuccess;
        if (callback) {
            ++callbackInvocations;
            callback(result, result == rpi_imager::FileError::kSuccess ? size : 0);
        }
        return result;
    }
};

class AsyncWriter : public DownloadThread
{
public:
    AsyncWriter() : DownloadThread("file:///nonexistent", "", "")
    {
        device = std::make_shared<FailingAsyncDevice>();
        _file = device;
        _debugAsyncIO = true;
        // Past the first block, so _writeFile does the write rather than
        // holding the partition table back.
        _firstBlock = static_cast<char *>(qMallocAligned(512, 4096));
        _firstBlockSize = 512;
        ::memset(_firstBlock, 0, 512);
    }

    std::shared_ptr<FailingAsyncDevice> device;

    using DownloadThread::_writeFile;
};

TEST_CASE("An async submission failure does not take the process with it",
          "[downloadthread][async]")
{
    AsyncWriter thread;
    const QByteArray block(64 * 1024, '\x33');

    // No completion callback, so this is the async-with-copy path: the buffer
    // is owned by the callback, not by _writeFile.
    const size_t written = thread._writeFile(block.constData(), block.size());

    CHECK(thread.device->asyncSubmissions == 1);
    CHECK(thread.device->callbackInvocations == 1);
    CHECK(written == 0);
}

TEST_CASE("Repeated async submission failures stay survivable",
          "[downloadthread][async]")
{
    // The failure mode this guards fires on every submission, so a card that
    // has started erroring produces a run of them. One was enough to abort
    // before; a run is what a user would actually hit.
    AsyncWriter thread;
    const QByteArray block(64 * 1024, '\x44');

    for (int i = 0; i < 16; ++i)
        CHECK(thread._writeFile(block.constData(), block.size()) == 0);

    CHECK(thread.device->asyncSubmissions == 16);
    CHECK(thread.device->callbackInvocations == 16);
}

TEST_CASE("A successful async submission reports the block as written",
          "[downloadthread][async]")
{
    // The other side, so the cases above are not passing because the async
    // path is never reached.
    AsyncWriter thread;
    thread.device->submissionFails = false;
    const QByteArray block(64 * 1024, '\x55');

    const size_t written = thread._writeFile(block.constData(), block.size());

    CHECK(thread.device->asyncSubmissions == 1);
    CHECK(written == static_cast<size_t>(block.size()));
    CHECK(thread.device->syncWrites == 0);
}
