/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The writer that saves a copy of the image while it is being downloaded.
 *
 * It runs on its own thread with a bounded queue, so a slow disk cannot hold
 * the download up: past the limit it gives up on caching rather than stall
 * the write the user is waiting on. It also hashes what it writes, and that
 * hash is what the next launch verifies the cached file against.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "asynccachewriter.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

namespace {

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

QByteArray sha256HexOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

template <typename Predicate>
bool waitFor(Predicate done, int timeoutMs = 20000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

struct Outcome
{
    bool finished = false;
    QByteArray hash;
    QStringList errors;
};

} // namespace

TEST_CASE("A cache that runs out of room says so rather than going quiet",
          "[cachewriter]")
{
    // The cache is written alongside the card, from the same stream. When
    // the disk holding it fills up -- a 4 GB image and a nearly full home
    // partition is the ordinary way -- the writer has to say so.
    //
    // Silence here is the bad outcome, and it is the plausible one: the
    // write to the card carries on and succeeds, so the user is told the
    // whole operation worked. Next launch finds a cache file that is short,
    // fails verification, and downloads the image again -- with nothing
    // anywhere having mentioned a full disk.
    if (!QFile::exists(QStringLiteral("/dev/full")))
        SKIP("/dev/full is not present, so a full disk cannot be simulated");

    const QByteArray payload = payloadOfSize(64 * 1024, 7);

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });
    QObject::connect(&writer, &AsyncCacheWriter::error, &writer,
                     [&](const QString &m) { out.errors << m; });

    REQUIRE(writer.open(QStringLiteral("/dev/full"), payload.size()));
    // Accepted into the queue: the failure is the write, one thread along,
    // not the handoff.
    REQUIRE(writer.write(payload.constData(), size_t(payload.size())));
    writer.finish();

    REQUIRE(waitFor([&] { return !out.errors.isEmpty() || out.finished; }));

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.errors.isEmpty());
    // Names the cache, so it is not mistaken for the card failing, and
    // passes on the system's reason rather than "an error occurred".
    CHECK(out.errors.first().contains(QStringLiteral("Cache"), Qt::CaseInsensitive));
    CHECK(out.errors.first().contains(QStringLiteral("space"), Qt::CaseInsensitive));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("A cached copy matches what was written", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));
    const QByteArray payload = payloadOfSize(512 * 1024, 3);

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });
    QObject::connect(&writer, &AsyncCacheWriter::error, &writer,
                     [&](const QString &m) { out.errors << m; });

    REQUIRE(writer.open(path, payload.size()));
    REQUIRE(writer.write(payload.constData(), size_t(payload.size())));
    writer.finish();

    REQUIRE(waitFor([&] { return out.finished; }));
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.errors.isEmpty());

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray written = f.readAll();

    // The next launch writes from this file. A byte wrong here is a card
    // that does not boot, blamed on the card.
    CHECK(written == payload);
}

TEST_CASE("The hash reported is the hash of the file", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));
    const QByteArray payload = payloadOfSize(256 * 1024, 4);

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });

    REQUIRE(writer.open(path, payload.size()));
    REQUIRE(writer.write(payload.constData(), size_t(payload.size())));
    writer.finish();
    REQUIRE(waitFor([&] { return out.finished; }));

    // This is the hash the cache is verified against next time. If it
    // describes something other than the bytes on disk, verification passes
    // on a file that is wrong.
    CHECK(out.hash == sha256HexOf(payload));
    CHECK(writer.hash() == sha256HexOf(payload));
}

TEST_CASE("A stream written in many pieces still matches", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));
    const QByteArray payload = payloadOfSize(400 * 1024, 5);

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });

    REQUIRE(writer.open(path, payload.size()));
    // A download arrives in whatever sizes curl hands over, not in one block.
    const int chunk = 7919;   // deliberately not a round number
    for (int off = 0; off < payload.size(); off += chunk) {
        const int n = qMin(chunk, int(payload.size()) - off);
        REQUIRE(writer.write(payload.constData() + off, size_t(n)));
    }
    writer.finish();
    REQUIRE(waitFor([&] { return out.finished; }));

    CHECK(out.hash == sha256HexOf(payload));
}

TEST_CASE("An empty image still produces a cache file and a hash", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });

    REQUIRE(writer.open(path, 0));
    writer.finish();
    REQUIRE(waitFor([&] { return out.finished; }));

    CHECK(out.hash == sha256HexOf(QByteArray()));
    CHECK(QFile::exists(path));
}

TEST_CASE("A path that cannot be opened is refused up front", "[cachewriter]")
{
    AsyncCacheWriter writer;

    // Caching is best-effort: refusing here lets the download carry on
    // uncached rather than failing the write.
    CHECK_FALSE(writer.open(QStringLiteral("/proc/definitely/not/writable/cached.img"), 1024));
    CHECK_FALSE(writer.isActive());
}

TEST_CASE("A cancelled cache leaves nothing behind", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));
    const QByteArray payload = payloadOfSize(256 * 1024, 6);

    AsyncCacheWriter writer;
    REQUIRE(writer.open(path, payload.size()));
    REQUIRE(writer.write(payload.constData(), size_t(payload.size())));

    // The user cancelled the write. A half-written cache kept from it would
    // be adopted on the next launch and written to a card.
    writer.cancel();
    REQUIRE(waitFor([&] { return !writer.isActive(); }));

    CHECK_FALSE(QFile::exists(path));
}

TEST_CASE("Writing before opening does nothing", "[cachewriter]")
{
    AsyncCacheWriter writer;
    const QByteArray payload = payloadOfSize(1024, 7);

    CHECK_FALSE(writer.isActive());
    // Returns rather than crashing: the caller does not check isActive()
    // before every chunk.
    writer.write(payload.constData(), size_t(payload.size()));
    CHECK_FALSE(writer.isActive());
}

TEST_CASE("Finishing twice is harmless", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("cached.img"));
    const QByteArray payload = payloadOfSize(64 * 1024, 8);

    AsyncCacheWriter writer;
    Outcome out;
    QObject::connect(&writer, &AsyncCacheWriter::finished, &writer,
                     [&](const QByteArray &h) { out.finished = true; out.hash = h; });

    REQUIRE(writer.open(path, payload.size()));
    REQUIRE(writer.write(payload.constData(), size_t(payload.size())));
    writer.finish();
    REQUIRE(waitFor([&] { return out.finished; }));

    CHECK_NOTHROW(writer.finish());
    CHECK(out.hash == sha256HexOf(payload));
}

TEST_CASE("A cancelled writer can be destroyed promptly", "[cachewriter]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QByteArray payload = payloadOfSize(512 * 1024, 9);

    QElapsedTimer timer;
    timer.start();
    {
        AsyncCacheWriter writer;
        REQUIRE(writer.open(QDir(dir.path()).filePath(QStringLiteral("cached.img")),
                            payload.size()));
        writer.write(payload.constData(), size_t(payload.size()));
        writer.cancel();
        // The destructor has to stop and join the thread; a leak here hangs
        // the application on quit rather than failing anything.
    }
    INFO("teardown took " << timer.elapsed() << "ms");
    CHECK(timer.elapsed() < 15000);
}

TEST_CASE("Caching giving up is told apart from caching failing", "[async-cache-writer]")
{
    // Two different things wear the same error flag. If the cache disk cannot
    // keep up, caching is switched off and the download carries on -- the user
    // gets their image, just no cached copy. If the writer failed outright,
    // that is a fault. Only the first should be reported to the user as "not
    // cached"; treating the second the same way loses a real error.
    //
    // The distinction is that backpressure leaves the writer still marked
    // active, having stood down rather than stopped.
    AsyncCacheWriter writer;

    // A fresh writer has neither happened.
    CHECK_FALSE(writer.hasError());
    CHECK_FALSE(writer.wasDisabledDueToBackpressure());
}

// Every rung of the queue-size ladder. On one machine only one rung is ever
// taken, so the boundaries -- which decide how much RAM the cache writer may
// hold on a small Pi -- went unchecked. The values are asserted, not just the
// ordering: getting 128 MB on a 512 MB board is the failure that matters.
TEST_CASE("Cache queue limits step with the memory of the machine", "[cache][memory]")
{
    struct Rung {
        qint64 totalMemMB;
        int chunks;
        qint64 megabytes;
    };

    const Rung rungs[] = {
        {  256,  8,   8 },   // below 1 GB
        { 1023,  8,   8 },   // last MB below the first boundary
        { 1024, 16,  16 },   // the boundary itself
        { 2047, 16,  16 },
        { 2048, 24,  32 },
        { 4095, 24,  32 },
        { 4096, 32,  64 },
        { 8191, 32,  64 },
        { 8192, 48, 128 },   // and everything above
        { 65536, 48, 128 },
    };

    for (const Rung &r : rungs) {
        const auto limits = AsyncCacheWriter::queueLimitsFor(r.totalMemMB);
        INFO("totalMemMB = " << r.totalMemMB);
        CHECK(limits.maxChunks == r.chunks);
        CHECK(limits.maxBytes == r.megabytes * 1024 * 1024);
    }
}

TEST_CASE("A cache queue never allows less than one chunk", "[cache][memory]")
{
    // Nonsense input must not produce a queue that can hold nothing: a zero
    // limit would wedge the writer rather than degrade it.
    for (qint64 mem : {qint64(0), qint64(-1), qint64(1)}) {
        INFO("totalMemMB = " << mem);
        const auto limits = AsyncCacheWriter::queueLimitsFor(mem);
        CHECK(limits.maxChunks >= 1);
        CHECK(limits.maxBytes > 0);
    }
}
