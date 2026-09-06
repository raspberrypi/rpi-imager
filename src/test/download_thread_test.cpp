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
#include "timeout_utils.h"

using rpi_imager::TimeoutDefaults::kHardTimeoutSeconds;
#include "faulty_block_device.h"
#include "devicewrapper.h"
#include "devicewrapperfatpartition.h"
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
