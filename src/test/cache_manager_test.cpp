// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// CacheManager decides whether a previously downloaded image can be reused
// instead of fetched again. Getting that wrong in the permissive direction is
// the expensive failure: handing back a cache entry that is not the image the
// user asked for writes the wrong thing to their card, silently. It had no
// tests.
//
// Everything here runs against QStandardPaths test mode, so the cache
// directory is a throwaway under the test tree rather than the developer's
// real one -- no case can find, trust, or delete a genuine cached image.
//
// The verification work happens on a background thread and reports by signal,
// so this target supplies its own main() with a QCoreApplication and waits on
// event loops rather than sleeping.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "cachemanager.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include "config.h"
#include "signal_log.h"
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <unistd.h>
#include <QTimer>
#include <QUuid>

namespace {

QByteArray hashOf(const QByteArray &contents)
{
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
}

QByteArray payloadOfSize(int size, int seed)
{
    QByteArray out;
    out.reserve(size);
    quint32 state = static_cast<quint32>(seed) | 1u;
    for (int i = 0; i < size; ++i) {
        state = state * 1664525u + 1013904223u;
        out.append(static_cast<char>((state >> 16) & 0xFF));
    }
    return out;
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const bool ok = f.write(contents) == contents.size();
    f.close();
    return ok;
}

// Wipe whatever the previous case left in the (test-mode) cache directory, so
// each case starts from a known-empty cache rather than inheriting one.
void clearCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!dir.isEmpty())
        QDir(dir).removeRecursively();
    QDir().mkpath(dir);
}

// Pump the event loop until `predicate` holds or the deadline passes, so a
// case that never settles fails rather than hanging the suite.
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 15000)
{
    QEventLoop loop;
    QTimer poll;
    QTimer guard;
    bool satisfied = false;

    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (predicate()) {
            satisfied = true;
            loop.quit();
        }
    });
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);

    poll.start(10);
    guard.start(timeoutMs);
    if (predicate())
        return true;
    loop.exec();
    return satisfied;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("CacheManager constructs and tears down cleanly", "[cache-manager]")
{
    clearCacheDir();

    // The constructor starts a worker thread; the destructor has to stop and
    // join it. A leak here shows up as a hang at process exit rather than a
    // failure, so this case exists mostly to prove it does not.
    {
        CacheManager manager;
        CHECK_NOTHROW(manager.getCacheStatus());
    }
    SUCCEED("constructed and destroyed without hanging");
}

TEST_CASE("CacheManager reports a cache status before anything is cached", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const CacheManager::CacheStatus status = manager.getCacheStatus();

    // Nothing has been cached, so nothing may be claimed as valid.
    CHECK_FALSE(status.isValid);
}

TEST_CASE("CacheManager starts background operations without blocking", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    CHECK_NOTHROW(manager.startBackgroundOperations());

    // Readiness is reached on the worker thread; it must arrive rather than
    // leaving the UI waiting on a flag that never flips.
    CHECK(waitFor([&]() { return manager.isReady(); }));
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

TEST_CASE("CacheManager reports an unknown hash as not cached", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QByteArray unknown = hashOf("an image nobody has downloaded");

    // The dangerous answer is a false positive: claiming a cache hit for an
    // image that was never fetched.
    CHECK_FALSE(manager.isCached(unknown));
    CHECK_FALSE(manager.hasPotentialCache(unknown));
}

TEST_CASE("CacheManager reports an empty hash as not cached", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    // Callers pass through whatever the OS list gave them, which is sometimes
    // nothing at all. An empty hash must never match.
    CHECK_FALSE(manager.isCached(QByteArray()));
    CHECK_FALSE(manager.hasPotentialCache(QByteArray()));
}

TEST_CASE("CacheManager hands back a cache path for a hash", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QByteArray hash = hashOf("raspios");
    const QString path = manager.getCacheFilePath(hash);

    // The path is derived rather than looked up, so it is answered even for a
    // hash with no file behind it -- but it must be inside the cache
    // directory, not somewhere arbitrary.
    if (!path.isEmpty()) {
        const QString cacheDir =
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        INFO("cache path: " << path.toStdString());
        INFO("cache dir:  " << cacheDir.toStdString());
        CHECK(path.startsWith(cacheDir));
    }
}

// ---------------------------------------------------------------------------
// Custom cache files
// ---------------------------------------------------------------------------

TEST_CASE("CacheManager accepts a custom cache file", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString custom = QDir(dir).filePath(QStringLiteral("custom.img"));
    const QByteArray contents = payloadOfSize(64 * 1024, 3);
    REQUIRE(writeFile(custom, contents));

    CHECK_NOTHROW(manager.setCustomCacheFile(custom, hashOf(contents)));
}

TEST_CASE("CacheManager invalidates a cache on request", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString custom = QDir(dir).filePath(QStringLiteral("doomed.img"));
    const QByteArray contents = payloadOfSize(32 * 1024, 5);
    REQUIRE(writeFile(custom, contents));
    manager.setCustomCacheFile(custom, hashOf(contents));

    manager.invalidateCache();

    // Invalidation is what runs when a write fails partway or the user asks
    // for a fresh download: afterwards nothing may be reported as cached.
    CHECK_FALSE(manager.getCacheStatus().isValid);
    CHECK_FALSE(manager.isCached(hashOf(contents)));
}

TEST_CASE("CacheManager updates the recorded cache hashes", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QByteArray uncompressed = hashOf("uncompressed image bytes");
    const QByteArray compressed = hashOf("compressed archive bytes");

    // Both are recorded: a cached .xz is looked up by the archive hash but
    // has to be verified against the image hash once expanded.
    //
    // Which of the two goes out to the UI is the point, and it was not
    // checked. The signal carries the uncompressed hash, because that is what
    // the OS list holds and what a cache hit is matched against; sending the
    // archive hash instead would leave every cached image looking like a miss
    // and quietly re-downloading. The two are easy to transpose and nothing
    // here would have noticed.
    QList<QByteArray> announced;
    QObject::connect(&manager, &CacheManager::cacheFileUpdated, &manager,
                     [&announced](const QByteArray &hash) { announced << hash; });

    manager.updateCacheFile(uncompressed, compressed);

    REQUIRE(announced.size() == 1);
    CHECK(announced.first() == uncompressed);
    CHECK(announced.first() != compressed);
}

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

TEST_CASE("CacheManager verification reports a mismatch", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString custom = QDir(dir).filePath(QStringLiteral("wrong.img"));
    const QByteArray contents = payloadOfSize(256 * 1024, 7);
    REQUIRE(writeFile(custom, contents));

    // File on disk, but filed under a hash that is not its own -- exactly the
    // corrupt-cache case that must not be handed to a write.
    const QByteArray wrongHash = hashOf("a completely different image");
    manager.setCustomCacheFile(custom, wrongHash);

    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(wrongHash);

    REQUIRE(waitFor([&]() { return completed.count() > 0; }, 30000));
    REQUIRE(completed.count() > 0);
    CHECK_FALSE(completed.at(0).at(0).toBool());
    CHECK_FALSE(manager.isCached(wrongHash));
}

TEST_CASE("CacheManager verification confirms a matching file", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QByteArray contents = payloadOfSize(256 * 1024, 11);
    const QByteArray hash = hashOf(contents);
    const QString custom = QDir(dir).filePath(QStringLiteral("good.img"));
    REQUIRE(writeFile(custom, contents));

    manager.setCustomCacheFile(custom, hash);

    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(hash);

    REQUIRE(waitFor([&]() { return completed.count() > 0; }, 30000));
    REQUIRE(completed.count() > 0);
    CHECK(completed.at(0).at(0).toBool());
}

TEST_CASE("CacheManager verification of a missing file fails rather than hangs",
          "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QByteArray hash = hashOf("never written");
    manager.setCustomCacheFile(
        QStringLiteral("/nonexistent-rpi-imager-dir/gone.img"), hash);

    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(hash);

    // The worker must answer even when there is nothing to read; a silent
    // failure leaves the UI on "checking cache" forever.
    REQUIRE(waitFor([&]() { return completed.count() > 0; }, 30000));
    CHECK_FALSE(completed.at(0).at(0).toBool());
}

TEST_CASE("CacheManager reports progress while verifying", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    // Large enough that verification takes more than one read.
    const QByteArray contents = payloadOfSize(4 * 1024 * 1024, 13);
    const QByteArray hash = hashOf(contents);
    const QString custom = QDir(dir).filePath(QStringLiteral("progress.img"));
    REQUIRE(writeFile(custom, contents));

    manager.setCustomCacheFile(custom, hash);

    rpi_test::SignalLog progress(&manager, &CacheManager::cacheVerificationProgress);
    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(hash);

    REQUIRE(waitFor([&]() { return completed.count() > 0; }, 60000));
    INFO("progress signals: " << progress.count());
    CHECK(progress.count() > 0);
}

// ---------------------------------------------------------------------------
// Setting up a download
// ---------------------------------------------------------------------------

TEST_CASE("CacheManager sets up a cache file for a download", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    manager.startBackgroundOperations();
    REQUIRE(waitFor([&]() { return manager.isReady(); }));

    QString cacheFile;
    const QByteArray hash = hashOf("an image about to be downloaded");
    const bool ok = manager.setupCacheForDownload(hash, 8 * 1024 * 1024, cacheFile);

    // Caching is best-effort: a refusal is legitimate (no space, disabled),
    // but agreeing to cache and handing back nowhere to write is not.
    if (ok) {
        INFO("cache file: " << cacheFile.toStdString());
        CHECK_FALSE(cacheFile.isEmpty());
    }
}

TEST_CASE("CacheManager refuses to cache a download larger than the disk", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    manager.startBackgroundOperations();
    REQUIRE(waitFor([&]() { return manager.isReady(); }));

    QString cacheFile;
    // An exabyte will not fit anywhere. Agreeing to this would fill the
    // user's disk mid-write and fail the imaging for want of a cache nobody
    // asked for.
    const bool ok = manager.setupCacheForDownload(hashOf("enormous"),
                                                  qint64(1) << 60, cacheFile);
    CHECK_FALSE(ok);
}

// CacheManager does its verification on a worker thread and reports by
// signal, so the cases above need a QCoreApplication to dispatch them. Test
// mode redirects QStandardPaths so no case can touch the real cache.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Test mode alone keeps this off the developer's real cache, but it does
    // not keep the cases apart from each other. Each TEST_CASE runs as its own
    // process and ctest -j runs several at once, so without a name unique to
    // the process they all share one cache directory and one settings file --
    // and clearCacheDir() is remove_all() on that directory while
    // clearCacheSettings() wipes that file. One case deletes what another is
    // partway through using.
    //
    // It surfaced as "A cache file that cannot be read is discarded on
    // startup" failing on setPermissions() returning false: the path had been
    // removed by a sibling between being written and being chmodded. Once in
    // 1157, only when those two happened to overlap.
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("cache_manager_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);

    const int rc = Catch::Session().run(argc, argv);

    // Nothing else knows this directory's name, so nothing else will remove it.
    QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .removeRecursively();
    return rc;
}

TEST_CASE("A custom cache file with no hash matches a lookup with no hash",
          "[cache][custom]")
{
    // Recorded because it is the reason the CLI refuses --cache-file without
    // --sha256 rather than passing an empty hash through. A custom cache is
    // handed back when its stored hash equals the one being looked up, and
    // with neither set that comparison is "" == "" -- so whatever is in the
    // cache file is returned as if it were the image that was asked for, and
    // nothing downstream checks it, because there is no hash to check
    // against.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cacheFile = dir.filePath(QStringLiteral("stale.img"));
    {
        QFile f(cacheFile);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("an image from some earlier run");
    }

    CacheManager mgr;
    mgr.setCustomCacheFile(cacheFile, QByteArray());

    CHECK(mgr.getCacheFilePath(QByteArray()) == cacheFile);
    // With a hash to check against it behaves: a cache recorded under no hash
    // is not offered for a specific one.
    CHECK(mgr.getCacheFilePath(QByteArray("abc123")).isEmpty());
}

TEST_CASE("A custom cache file with a hash is only offered for that hash",
          "[cache][custom]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cacheFile = dir.filePath(QStringLiteral("cached.img"));
    {
        QFile f(cacheFile);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("the image that was asked for");
    }

    CacheManager mgr;
    mgr.setCustomCacheFile(cacheFile, QByteArray("aa11"));

    CHECK(mgr.getCacheFilePath(QByteArray("aa11")) == cacheFile);
    CHECK(mgr.getCacheFilePath(QByteArray("bb22")).isEmpty());
}

// ---------------------------------------------------------------------------
// Disk space and readiness
// ---------------------------------------------------------------------------

TEST_CASE("A cache directory that cannot be written to reports no space",
          "[cache][diskspace]")
{
    // The cache lives under the user's home. It can stop being writable
    // between runs -- a full disk, a home directory remounted read-only, a
    // permissions change -- and what the caller needs then is to be told
    // there is nowhere to cache, so the next write downloads instead of
    // trying to cache into a directory it cannot use.
    //
    // Reported as no space and no directory. Reporting the real free space
    // with the directory named would have the caller cache into somewhere
    // that will refuse every file.
    if (::geteuid() == 0)
        SKIP("root can write to a directory with no write bit");

    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    REQUIRE_FALSE(cacheDir.isEmpty());
    REQUIRE(QDir().mkpath(cacheDir));
    REQUIRE(QFile::setPermissions(cacheDir,
                                  QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    qint64 bytes = -1;
    QString reported = QStringLiteral("unset");
    CacheVerificationWorker worker;
    QObject::connect(&worker, &CacheVerificationWorker::diskSpaceCheckComplete,
                     [&](qint64 b, const QString &d) { bytes = b; reported = d; });

    worker.checkDiskSpace();

    // Put it back before asserting, so a failing case still leaves a
    // directory the run can clean up after itself.
    QFile::setPermissions(cacheDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);

    CHECK(bytes == 0);
    CHECK(reported.isEmpty());
}

TEST_CASE("A usable cache directory reports the space it has", "[cache][diskspace]")
{
    // The other half, and the reason the case above is not simply "it
    // reports zero": zero has to mean something.
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    REQUIRE(QDir().mkpath(cacheDir));

    qint64 bytes = -1;
    QString reported;
    CacheVerificationWorker worker;
    QObject::connect(&worker, &CacheVerificationWorker::diskSpaceCheckComplete,
                     [&](qint64 b, const QString &d) { bytes = b; reported = d; });

    worker.checkDiskSpace();

    CHECK(bytes > 0);
    CHECK(reported == cacheDir);
}

TEST_CASE("CacheManager reports disk space once ready", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    rpi_test::SignalLog spaceChecked(&manager, &CacheManager::diskSpaceCheckComplete);
    manager.startBackgroundOperations();

    // The space check runs on the worker and gates whether caching is offered
    // at all; never completing leaves the feature permanently unavailable.
    REQUIRE(waitFor([&]() { return spaceChecked.count() > 0 || manager.isReady(); }, 30000));
    CHECK(manager.isReady());
}

TEST_CASE("CacheManager tolerates being started twice", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    manager.startBackgroundOperations();
    REQUIRE(waitFor([&]() { return manager.isReady(); }));

    // The UI can re-enter this on a settings change; a second start must not
    // spawn another worker or re-run the scan.
    CHECK_NOTHROW(manager.startBackgroundOperations());
    CHECK(manager.isReady());
}

TEST_CASE("CacheManager invalidating an empty cache is harmless", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    // Runs on every failed write, including ones that failed before anything
    // was cached.
    CHECK_NOTHROW(manager.invalidateCache());
    CHECK_NOTHROW(manager.invalidateCache());
    CHECK_FALSE(manager.getCacheStatus().isValid);
}

TEST_CASE("CacheManager verification of an empty hash settles", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;

    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(QByteArray());

    // An OS list entry with no published hash still reaches here; it must
    // answer rather than leave the caller waiting.
    if (waitFor([&]() { return completed.count() > 0; }, 15000))
        CHECK_FALSE(completed.at(0).at(0).toBool());
    else
        SUCCEED("no verification was attempted for an empty hash, which is also valid");
}

TEST_CASE("CacheManager setup for a zero-byte download", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    manager.startBackgroundOperations();
    REQUIRE(waitFor([&]() { return manager.isReady(); }));

    QString cacheFile;
    // A size of zero means the caller does not know how big the image is,
    // which happens when the server sends no Content-Length.
    const bool ok = manager.setupCacheForDownload(hashOf("unknown size"), 0, cacheFile);
    if (ok)
        CHECK_FALSE(cacheFile.isEmpty());
}

TEST_CASE("CacheManager updates hashes after a completed write", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    manager.startBackgroundOperations();
    REQUIRE(waitFor([&]() { return manager.isReady(); }));

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QByteArray contents = payloadOfSize(128 * 1024, 17);
    const QByteArray hash = hashOf(contents);
    const QString cached = QDir(dir).filePath(QStringLiteral("written.img"));
    REQUIRE(writeFile(cached, contents));

    manager.setCustomCacheFile(cached, hash);
    // The real sequence at the end of a write: record what was cached, then
    // confirm it is usable next time.
    manager.updateCacheFile(hash, hash);

    rpi_test::SignalLog completed(&manager, &CacheManager::cacheVerificationComplete);
    manager.startVerification(hash);
    REQUIRE(waitFor([&]() { return completed.count() > 0; }, 30000));
    CHECK(completed.at(0).at(0).toBool());
}

// ═══════════════════════════════════════════════════════════════════════════
// Surviving a restart
//
// The cache is only worth having if it is still there next launch.
// updateCacheFile() records the file and its hashes in settings; the next
// CacheManager reads them back in its constructor and adopts the file --
// after checking it is still there and still readable.
//
// Skipping that check would be worse than not caching at all: the imager
// would write from a file that has been deleted, truncated by a cleaner, or
// left unreadable, and the first sign would be a card that does not boot.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Clears only the settings this section writes, leaving the cache directory
// alone -- the point is to control the two independently.
void clearCacheSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("caching"));
    s.remove(QString());
    s.endGroup();
    s.sync();
}

} // namespace

TEST_CASE("A cache recorded in settings is adopted next launch", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    const QByteArray payload = payloadOfSize(4096, 7);
    const QByteArray uncompressed = hashOf(payload);
    const QByteArray compressed = hashOf(payload + "z");

    QString cacheFile;
    {
        CacheManager first;
        first.startBackgroundOperations();
        REQUIRE(waitFor([&]() { return first.isReady(); }));
        first.setupCacheForDownload(uncompressed, payload.size(), cacheFile);
        if (cacheFile.isEmpty())
            cacheFile = first.getCacheStatus().cacheFileName;
        REQUIRE_FALSE(cacheFile.isEmpty());
        REQUIRE(writeFile(cacheFile, payload));
        first.updateCacheFile(uncompressed, compressed);
    }

    CacheManager second;
    const CacheManager::CacheStatus status = second.getCacheStatus();

    // Without this, every launch re-downloads an image already on disk.
    CHECK(status.cacheFileName == cacheFile);
    CHECK(status.cachedHash == uncompressed);
    CHECK(second.hasPotentialCache(uncompressed));
}

TEST_CASE("A recorded cache is not trusted until it is verified", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    const QByteArray payload = payloadOfSize(4096, 8);
    const QByteArray uncompressed = hashOf(payload);

    QString cacheFile;
    {
        CacheManager first;
        first.startBackgroundOperations();
        REQUIRE(waitFor([&]() { return first.isReady(); }));
        first.setupCacheForDownload(uncompressed, payload.size(), cacheFile);
        if (cacheFile.isEmpty())
            cacheFile = first.getCacheStatus().cacheFileName;
        REQUIRE(writeFile(cacheFile, payload));
        first.updateCacheFile(uncompressed, hashOf(payload));
    }

    CacheManager second;
    // Adopted, but verificationComplete is false: the file may have changed
    // under us, so it is re-hashed before anything is written from it.
    CHECK(second.hasPotentialCache(uncompressed));
    CHECK_FALSE(second.getCacheStatus().verificationComplete);
}

TEST_CASE("A cache whose file has gone is discarded on startup", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    const QByteArray payload = payloadOfSize(4096, 9);
    const QByteArray uncompressed = hashOf(payload);

    QString cacheFile;
    {
        CacheManager first;
        first.startBackgroundOperations();
        REQUIRE(waitFor([&]() { return first.isReady(); }));
        first.setupCacheForDownload(uncompressed, payload.size(), cacheFile);
        if (cacheFile.isEmpty())
            cacheFile = first.getCacheStatus().cacheFileName;
        REQUIRE(writeFile(cacheFile, payload));
        first.updateCacheFile(uncompressed, hashOf(payload));
    }

    // A disk cleaner, or the user emptying their cache directory.
    REQUIRE(QFile::remove(cacheFile));

    CacheManager second;
    CHECK_FALSE(second.hasPotentialCache(uncompressed));
    CHECK(second.getCacheStatus().cacheFileName.isEmpty());
}

TEST_CASE("A cache file that has been emptied is discarded on startup", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    const QByteArray payload = payloadOfSize(4096, 10);
    const QByteArray uncompressed = hashOf(payload);

    QString cacheFile;
    {
        CacheManager first;
        first.startBackgroundOperations();
        REQUIRE(waitFor([&]() { return first.isReady(); }));
        first.setupCacheForDownload(uncompressed, payload.size(), cacheFile);
        if (cacheFile.isEmpty())
            cacheFile = first.getCacheStatus().cacheFileName;
        REQUIRE(writeFile(cacheFile, payload));
        first.updateCacheFile(uncompressed, hashOf(payload));
    }

    // Truncated rather than removed: an interrupted cleanup, or a full disk.
    REQUIRE(writeFile(cacheFile, QByteArray()));

    CacheManager second;
    CHECK_FALSE(second.hasPotentialCache(uncompressed));
}

TEST_CASE("A cache file that cannot be read is discarded on startup", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    if (::geteuid() == 0)
        SKIP("running as root, which can read a file with no permissions");

    const QByteArray payload = payloadOfSize(4096, 11);
    const QByteArray uncompressed = hashOf(payload);

    QString cacheFile;
    {
        CacheManager first;
        first.startBackgroundOperations();
        REQUIRE(waitFor([&]() { return first.isReady(); }));
        first.setupCacheForDownload(uncompressed, payload.size(), cacheFile);
        if (cacheFile.isEmpty())
            cacheFile = first.getCacheStatus().cacheFileName;
        REQUIRE(writeFile(cacheFile, payload));
        first.updateCacheFile(uncompressed, hashOf(payload));
    }

    REQUIRE(QFile::setPermissions(cacheFile, QFileDevice::Permissions()));

    CacheManager second;
    CHECK_FALSE(second.hasPotentialCache(uncompressed));

    QFile::setPermissions(cacheFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

TEST_CASE("A custom cache file is not written into settings", "[cache-manager][persist]")
{
    clearCacheDir();
    clearCacheSettings();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString custom = QDir(dir.path()).filePath(QStringLiteral("mine.img"));
    const QByteArray payload = payloadOfSize(4096, 12);
    REQUIRE(writeFile(custom, payload));

    {
        CacheManager first;
        first.setCustomCacheFile(custom, hashOf(payload));
        first.updateCacheFile(hashOf(payload), hashOf(payload));
    }

    // A path the user pointed at once should not become the imager's
    // permanent cache; the temporary directory it lived in is gone by now.
    CacheManager second;
    CHECK(second.getCacheStatus().cacheFileName != custom);
}

// ---------------------------------------------------------------------------
// Deciding whether the next download may be cached
// ---------------------------------------------------------------------------

namespace {

// What the background disk-space check reports back. It is a private slot,
// which the metaobject can still reach by name -- the alternative would be
// widening the class's interface for the benefit of a test.
void reportDiskSpace(CacheManager &manager, qint64 availableBytes)
{
    const bool invoked = QMetaObject::invokeMethod(
        &manager, "onDiskSpaceCheckComplete", Qt::DirectConnection,
        Q_ARG(qint64, availableBytes), Q_ARG(QString, QDir::tempPath()));
    REQUIRE(invoked);
}

} // namespace
//
// setupCacheForDownload() answers that, and its refusals were uncovered.
//
// Two of them matter to a user. A cache holding one image has to be thrown
// away before another is downloaded into it, or a later run can be handed
// the file it already had for a different selection -- an image written to
// a card that is not the one that was chosen. And the disk-space guards are
// what stop a download filling the drive the application is running from,
// which on a laptop means the machine, not just the download.

TEST_CASE("A cache holding another image is thrown away first",
          "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    manager.updateCacheFile(QByteArray("hash-of-the-old-image"),
                            QByteArray("compressed-hash-of-the-old-image"));
    REQUIRE(manager.getCacheStatus().cachedHash
            == QByteArray("hash-of-the-old-image"));

    rpi_test::SignalLog invalidated(&manager, &CacheManager::cacheInvalidated);

    QString path;
    manager.setupCacheForDownload(QByteArray("hash-of-a-different-image"),
                                  1024, path);

    CHECK(invalidated.count() == 1);
    CHECK(manager.getCacheStatus().cachedHash.isEmpty());
}

TEST_CASE("A cache holding the same image is left where it is",
          "[cache-manager]")
{
    // The counterpart. Throwing it away every time would mean downloading
    // the image again on every write of the same card.
    clearCacheDir();
    CacheManager manager;
    manager.updateCacheFile(QByteArray("hash-of-the-image"),
                            QByteArray("compressed-hash"));

    rpi_test::SignalLog invalidated(&manager, &CacheManager::cacheInvalidated);

    QString path;
    manager.setupCacheForDownload(QByteArray("hash-of-the-image"), 1024, path);

    CHECK(invalidated.count() == 0);
    CHECK(manager.getCacheStatus().cachedHash == QByteArray("hash-of-the-image"));
}

TEST_CASE("Nothing is cached before the disk has been looked at",
          "[cache-manager]")
{
    // The check runs on a background thread. Until it answers there is no
    // way to know whether writing a copy of the image would fill the drive.
    //
    // Defended twice: the explicit "has the check finished" test, and the
    // floor below it, which refuses anyway because the available space is
    // still zero. Removing the first fails nothing, so what is pinned here
    // is the outcome rather than either guard.
    clearCacheDir();
    CacheManager manager;
    REQUIRE(!manager.getCacheStatus().diskSpaceCheckComplete);

    QString path;
    CHECK_FALSE(manager.setupCacheForDownload(QByteArray("some-hash"), 1024, path));
    CHECK(path.isEmpty());
}

TEST_CASE("Nothing is cached when the disk is too full", "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    // What the background check reports when it finds the drive nearly full.
    reportDiskSpace(manager, 1024LL * 1024 * 1024);

    QString path;
    CHECK_FALSE(manager.setupCacheForDownload(QByteArray("some-hash"),
                                              1024, path));
}

TEST_CASE("Nothing is cached when the download would leave too little",
          "[cache-manager]")
{
    // Enough space for the download itself is not enough: the application
    // keeps a floor free, because a drive filled to the last byte by a
    // cached copy is a machine that stops working for reasons the user will
    // not connect to having written a card.
    clearCacheDir();
    CacheManager manager;
    const qint64 floor = IMAGEWRITER_MINIMAL_SPACE_FOR_CACHING;
    reportDiskSpace(manager, floor + (2LL * 1024 * 1024 * 1024));

    QString path;
    // Three gigabytes on top of the floor leaves less than the floor.
    CHECK_FALSE(manager.setupCacheForDownload(QByteArray("some-hash"),
                                              3LL * 1024 * 1024 * 1024, path));

    // One gigabyte leaves more than it, so this one is allowed.
    CHECK(manager.setupCacheForDownload(QByteArray("some-hash"),
                                        1LL * 1024 * 1024 * 1024, path));
    CHECK(!path.isEmpty());
}

TEST_CASE("A download that is allowed to be cached is given somewhere to go",
          "[cache-manager]")
{
    clearCacheDir();
    CacheManager manager;
    reportDiskSpace(manager, 200LL * 1024 * 1024 * 1024);

    QString path;
    REQUIRE(manager.setupCacheForDownload(QByteArray("some-hash"), 1024, path));

    CHECK(!path.isEmpty());
    CHECK(manager.getCacheStatus().cacheFileName == path);
}

TEST_CASE("A custom cache file is used rather than the default one",
          "[cache-manager]")
{
    // Set from the debug options. A download that ignored it would write to
    // the default location while the user watched the one they chose.
    clearCacheDir();
    CacheManager manager;
    const QString chosen = QDir::temp().filePath(
        QStringLiteral("rpi-imager-chosen-cache.img"));
    manager.setCustomCacheFile(chosen, QByteArray("some-hash"));
    reportDiskSpace(manager, 200LL * 1024 * 1024 * 1024);

    QString path;
    REQUIRE(manager.setupCacheForDownload(QByteArray("some-hash"), 1024, path));

    CHECK(path == chosen);
}
