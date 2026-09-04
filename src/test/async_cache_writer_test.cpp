/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The writer that saves a copy of the image while it is being downloaded.
 *
 * It runs on its own thread with a bounded queue, so a slow disk cannot hold
 * the download up: past the limit it gives up on caching rather than stall
 * the write the user is waiting on. It also hashes what it writes, and that
 * hash is what the next launch verifies the cached file against.
 *
 * Every failure here is meant to be invisible -- caching is best-effort, and
 * an image that fails to cache must still be written. The one thing that
 * must not happen quietly is a cache file that is kept while being wrong,
 * because the next write starts from it.
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
