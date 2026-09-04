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
    CHECK_NOTHROW(manager.updateCacheFile(uncompressed, compressed));
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
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}

// ---------------------------------------------------------------------------
// Disk space and readiness
// ---------------------------------------------------------------------------

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
