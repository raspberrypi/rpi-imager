/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * These functions answer "how big will this image be once decompressed?",
 * and ImageWriter::startWrite() refuses the write when that answer will not
 * fit the chosen card. Both ways of being wrong are user-facing:
 *
 *   - too large, and a good image is rejected with "Storage capacity is not
 *     large enough" for a card that would have held it fine;
 *   - too small, and the check waves the write through, to fail partway with
 *     the card already overwritten.
 *
 * The zstd case below is the one that has actually gone wrong: the "size not
 * recorded" sentinel is (0ULL - 2), so treating it as a number rather than as
 * a sentinel yields an apparent 16 exabytes and rejects every streaming-
 * compressed image.
 */

#include <catch2/catch_test_macros.hpp>

#include "imagesizeparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include "fixture_process.h"

namespace {

bool haveTool(const QString &name)
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/") + name);
}

// Deterministic, compressible-but-not-trivial content.
QByteArray payloadOfSize(int bytes)
{
    QByteArray out;
    out.reserve(bytes);
    for (int i = 0; i < bytes; ++i)
        out.append(char('A' + (i * 7 + i / 64) % 26));
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

// Run a shell pipeline so streaming compression (stdin, no known size) can be
// expressed -- that is precisely the case that produces a zstd frame with no
// content size recorded.
bool runShell(const QString &script)
{
    QProcess p;
    p.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
    if (!p.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

class Scratch
{
public:
    Scratch() { REQUIRE(_dir.isValid()); }
    QString path(const QString &name) const { return QDir(_dir.path()).filePath(name); }
private:
    QTemporaryDir _dir;
};

constexpr int kRawSize = 256 * 1024;

} // namespace

// ══════════════════════════════════════════════════════════════
// gzip
// ══════════════════════════════════════════════════════════════

TEST_CASE("A gzip image reports its uncompressed size", "[imagesize]")
{
    if (!haveTool("gzip"))
        SKIP("gzip is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("gzip -k -f %1").arg(raw)));

    CHECK(imagesize::parseGz(raw + ".gz") == quint64(kRawSize));
}

TEST_CASE("A file too short to hold a gzip trailer reports unknown", "[imagesize]")
{
    Scratch scratch;
    const QString path = scratch.path("stub.gz");
    REQUIRE(writeFile(path, QByteArray(4, '\x1f')));
    CHECK(imagesize::parseGz(path) == 0);
}

TEST_CASE("A gzip file that is not there reports unknown", "[imagesize]")
{
    CHECK(imagesize::parseGz(QStringLiteral("/nonexistent-9f2a/x.gz")) == 0);
}

// ══════════════════════════════════════════════════════════════
// xz
// ══════════════════════════════════════════════════════════════

TEST_CASE("An xz image reports its uncompressed size", "[imagesize]")
{
    if (!haveTool("xz"))
        SKIP("xz is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("xz -k -f %1").arg(raw)));

    CHECK(imagesize::parseXz(raw + ".xz") == quint64(kRawSize));
}

TEST_CASE("A truncated xz file reports unknown rather than a guess", "[imagesize]")
{
    if (!haveTool("xz"))
        SKIP("xz is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("xz -k -f %1").arg(raw)));

    // Lop off the footer, which is where the index lives.
    QFile f(raw + ".xz");
    REQUIRE(f.open(QIODevice::ReadOnly));
    QByteArray whole = f.readAll();
    f.close();
    const QString truncated = scratch.path("truncated.xz");
    REQUIRE(writeFile(truncated, whole.left(whole.size() / 2)));

    CHECK(imagesize::parseXz(truncated) == 0);
}

// ══════════════════════════════════════════════════════════════
// zstd
// ══════════════════════════════════════════════════════════════

TEST_CASE("A zstd image reports its uncompressed size", "[imagesize]")
{
    if (!haveTool("zstd"))
        SKIP("zstd is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("zstd -q -k -f %1").arg(raw)));

    CHECK(imagesize::parseZstd(raw + ".zst") == quint64(kRawSize));
}

TEST_CASE("A streaming-compressed zstd image reports unknown, not a huge number",
          "[imagesize]")
{
    // The regression this file exists for. Compressing from a pipe leaves the
    // frame content size out of the header, and ZSTD_findDecompressedSize
    // signals that with (0ULL - 2). Returned as a size, that is ~16 EB, and
    // every capacity check against it fails: the user is told their card is
    // too small for an image that would have fitted.
    if (!haveTool("zstd"))
        SKIP("zstd is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    const QString out = scratch.path("streamed.zst");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("cat %1 | zstd -q -o %2 -f").arg(raw, out)));

    const quint64 size = imagesize::parseZstd(out);
    INFO("reported size: " << size);
    CHECK(size == 0);
}

TEST_CASE("A corrupt zstd file reports unknown", "[imagesize]")
{
    Scratch scratch;
    const QString path = scratch.path("garbage.zst");
    REQUIRE(writeFile(path, QByteArray(4096, '\xEE')));
    CHECK(imagesize::parseZstd(path) == 0);
}

TEST_CASE("An empty zstd file reports unknown", "[imagesize]")
{
    Scratch scratch;
    const QString path = scratch.path("empty.zst");
    REQUIRE(writeFile(path, QByteArray()));
    CHECK(imagesize::parseZstd(path) == 0);
}

TEST_CASE("A zstd file that is not there reports unknown", "[imagesize]")
{
    CHECK(imagesize::parseZstd(QStringLiteral("/nonexistent-9f2a/x.zst")) == 0);
}

// ══════════════════════════════════════════════════════════════
// Archives
//
// The file count is what decides _multipleFilesInZip, which switches the
// whole write between "one image onto the card" and "unpack these files".
// ══════════════════════════════════════════════════════════════

TEST_CASE("A zip holding one image reports one file and its size", "[imagesize]")
{
    if (!haveTool("zip"))
        SKIP("zip is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    const QString zipPath = scratch.path("image.zip");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("cd %1 && zip -q %2 image.img")
                         .arg(QFileInfo(raw).path(), zipPath)));

    const auto info = imagesize::parseArchive(zipPath);
    CHECK(info.fileCount == 1);
    CHECK(info.uncompressedSize == quint64(kRawSize));
}

TEST_CASE("A zip holding several files reports all of them", "[imagesize]")
{
    if (!haveTool("zip"))
        SKIP("zip is not installed");

    Scratch scratch;
    const QString dir = QFileInfo(scratch.path("x")).path();
    for (const char *name : {"a.img", "b.img", "c.img"})
        REQUIRE(writeFile(scratch.path(QString::fromLatin1(name)), payloadOfSize(kRawSize)));

    const QString zipPath = scratch.path("multi.zip");
    REQUIRE(runShell(QStringLiteral("cd %1 && zip -q %2 a.img b.img c.img")
                         .arg(dir, zipPath)));

    const auto info = imagesize::parseArchive(zipPath);
    CHECK(info.fileCount == 3);
    CHECK(info.uncompressedSize == quint64(kRawSize) * 3);
}

TEST_CASE("An archive that is not there reports nothing", "[imagesize]")
{
    const auto info = imagesize::parseArchive(QStringLiteral("/nonexistent-9f2a/x.zip"));
    CHECK(info.fileCount == 0);
    CHECK(info.uncompressedSize == 0);
}

TEST_CASE("A file that is not an archive reports nothing", "[imagesize]")
{
    Scratch scratch;
    const QString path = scratch.path("notanarchive.zip");
    REQUIRE(writeFile(path, QByteArray(8192, '\x00')));

    const auto info = imagesize::parseArchive(path);
    CHECK(info.fileCount == 0);
    CHECK(info.uncompressedSize == 0);
}
