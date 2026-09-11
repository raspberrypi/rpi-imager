/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * These functions answer "how big will this image be once decompressed?",
 * and ImageWriter::startWrite() refuses the write when that answer will not
 * fit the chosen card. Both ways of being wrong are user-facing:
 *
 *   - too large, and a good image is rejected with "Storage capacity is not
 *     large enough" for a card that would have held it fine;
 *   - too small, and the check waves the write through, to fail partway with
 *     the card already overwritten.
 */

#include <catch2/catch_test_macros.hpp>

#include "imagesizeparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <archive.h>

#include "fixture_process.h"
#include "platform_tools.h"

namespace {

using rpi_test::haveTool;

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

TEST_CASE("A zip under a name outside Latin-1 still reports its contents",
          "[imagesize][encoding]")
{
    // The path is a filesystem path, and a great many people's are not
    // Latin-1: a username, or a folder named in the language they speak.
    // Converting it with toLatin1() turned every such character into a
    // question mark, so the archive could not be opened and came back as
    // holding nothing.
    //
    // Nothing said so. The caller reads an empty answer as "size unknown",
    // which loses the "image too big for this card" refusal before the write
    // starts, and as "one file", which makes a multi-file archive look like a
    // single image and puts only part of it on the card.
    if (!haveTool("zip"))
        SKIP("zip is not installed");

    Scratch scratch;
    const QString dir = scratch.path(QString::fromUtf8("\xe3\x82\xa4\xe3\x83\xa1\xe3\x83\xbc\xe3\x82\xb8"));
    REQUIRE(QDir().mkpath(dir));
    const QString raw = QDir(dir).filePath(QStringLiteral("image.img"));
    const QString zipPath = QDir(dir).filePath(QStringLiteral("image.zip"));
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("cd '%1' && zip -q image.zip image.img").arg(dir)));
    REQUIRE(QFileInfo::exists(zipPath));

    const auto info = imagesize::parseArchive(zipPath);
    INFO("path: " << zipPath.toStdString());
    CHECK(info.fileCount == 1);
    CHECK(info.uncompressedSize == quint64(kRawSize));
}

TEST_CASE("A zip whose own filename is outside Latin-1 reports its contents",
          "[imagesize][encoding]")
{
    // The other half: the folder may be plain and the file itself not.
    if (!haveTool("zip"))
        SKIP("zip is not installed");

    Scratch scratch;
    const QString raw = scratch.path(QStringLiteral("image.img"));
    const QString name = QString::fromUtf8("\xd0\xbe\xd0\xb1\xd1\x80\xd0\xb0\xd0\xb7.zip");
    const QString zipPath = scratch.path(name);
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("cd '%1' && zip -q '%2' image.img")
                         .arg(QFileInfo(raw).path(), name)));
    REQUIRE(QFileInfo::exists(zipPath));

    const auto info = imagesize::parseArchive(zipPath);
    INFO("path: " << zipPath.toStdString());
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

// ══════════════════════════════════════════════════════════════
// Sizing by content
//
// The write path decides what a file holds by sniffing it, so sizing has to
// reach the same verdict the same way. Where the two disagree, the card is
// written by one and measured by the other.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An xz image named .img is sized decompressed, not by its file length",
          "[imagesize][mislabelled]")
{
    // The regression. Sized by extension, a download served as xz under a
    // .img name took the "not compressed" branch and was measured at its
    // compressed length, while the write sniffed the same bytes and
    // decompressed them: progress ran past 400%, and the capacity check was
    // made against a fraction of what the card actually needed.
    if (!haveTool("xz"))
        SKIP("xz is not installed");

    Scratch scratch;
    const QString raw = scratch.path("payload.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("xz -k -f '%1'").arg(raw)));

    // The name says raw image; the bytes say xz.
    const QString mislabelled = scratch.path("latest_image.img");
    REQUIRE(runShell(QStringLiteral("mv '%1.xz' '%2'").arg(raw, mislabelled)));

    const quint64 onDisk = quint64(QFileInfo(mislabelled).size());
    const auto measured = imagesize::measureLocalFile(mislabelled);
    INFO("on disk: " << onDisk << ", reported: " << measured.uncompressedSize);
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK(measured.uncompressedSize > onDisk);
    CHECK(measured.sizeIsReliable);
    CHECK(measured.fileCount == 0);
}

TEST_CASE("A raw image is sized at its own length", "[imagesize]")
{
    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));

    const auto measured = imagesize::measureLocalFile(raw);
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK(measured.sizeIsReliable);
    CHECK(measured.fileCount == 0);
}

TEST_CASE("A zip named .img is still measured as a container",
          "[imagesize][mislabelled]")
{
    if (!haveTool("zip"))
        SKIP("zip is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    const QString zipPath = scratch.path("bundle.zip");
    REQUIRE(runShell(QStringLiteral("cd '%1' && zip -q '%2' image.img")
                         .arg(QFileInfo(raw).path(), zipPath)));

    const QString mislabelled = scratch.path("bundle.img");
    REQUIRE(runShell(QStringLiteral("mv '%1' '%2'").arg(zipPath, mislabelled)));

    const auto measured = imagesize::measureLocalFile(mislabelled);
    CHECK(measured.fileCount == 1);
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK(measured.sizeIsReliable);
}

TEST_CASE("A gzip image is sized for capacity but barred from progress",
          "[imagesize]")
{
    if (!haveTool("gzip"))
        SKIP("gzip is not installed");

    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("gzip -k -f '%1'").arg(raw)));

    const auto measured = imagesize::measureLocalFile(raw + ".gz");
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK_FALSE(measured.sizeIsReliable);
}

TEST_CASE("A gzip image named .img is still barred from progress",
          "[imagesize][mislabelled]")
{
    // What makes gzip untrustworthy is ISIZE wrapping at 4 GB, not the
    // suffix. The old flag tested the name for ".gz", so a mislabelled one
    // earned a determinate bar off a size that may have wrapped.
    if (!haveTool("gzip"))
        SKIP("gzip is not installed");

    Scratch scratch;
    const QString raw = scratch.path("payload.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("gzip -k -f '%1'").arg(raw)));
    const QString mislabelled = scratch.path("sneaky.img");
    REQUIRE(runShell(QStringLiteral("mv '%1.gz' '%2'").arg(raw, mislabelled)));

    const auto measured = imagesize::measureLocalFile(mislabelled);
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK_FALSE(measured.sizeIsReliable);
}

TEST_CASE("A zstd image named .img is sized decompressed",
          "[imagesize][mislabelled]")
{
    if (!haveTool("zstd"))
        SKIP("zstd is not installed");

    Scratch scratch;
    const QString raw = scratch.path("payload.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("zstd -q -k -f '%1'").arg(raw)));
    const QString mislabelled = scratch.path("zstd_as_img.img");
    REQUIRE(runShell(QStringLiteral("mv '%1.zst' '%2'").arg(raw, mislabelled)));

    const auto measured = imagesize::measureLocalFile(mislabelled);
    CHECK(measured.uncompressedSize == quint64(kRawSize));
    CHECK(measured.sizeIsReliable);
}

TEST_CASE("A bzip2 image reports unknown rather than its compressed length",
          "[imagesize][mislabelled]")
{
    // No cheap way to read the decompressed length out of bzip2, so the
    // answer is "unknown". Wrong answers here are worse than none: the
    // compressed length would pass a capacity check the write then fails.
    if (!haveTool("bzip2"))
        SKIP("bzip2 is not installed");

    Scratch scratch;
    const QString raw = scratch.path("payload.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("bzip2 -k -f '%1'").arg(raw)));
    const QString mislabelled = scratch.path("bz2_as_img.img");
    REQUIRE(runShell(QStringLiteral("mv '%1.bz2' '%2'").arg(raw, mislabelled)));

    const auto measured = imagesize::measureLocalFile(mislabelled);
    CHECK(measured.uncompressedSize == 0);
    CHECK_FALSE(measured.sizeIsReliable);
}

TEST_CASE("A file that is not there is unreadable and unsized", "[imagesize]")
{
    const auto measured =
        imagesize::measureLocalFile(QStringLiteral("/nonexistent-9f2a/x.img"));
    CHECK(measured.uncompressedSize == 0);
    CHECK_FALSE(measured.sizeIsReliable);
}

TEST_CASE("probeFormat reports no filter for a raw image", "[imagesize]")
{
    Scratch scratch;
    const QString raw = scratch.path("image.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));

    const auto probed = imagesize::probeFormat(raw);
    CHECK(probed.readable);
    CHECK(probed.filterCode == ARCHIVE_FILTER_NONE);
}

TEST_CASE("probeFormat reports the xz filter whatever the file is called",
          "[imagesize][mislabelled]")
{
    if (!haveTool("xz"))
        SKIP("xz is not installed");

    Scratch scratch;
    const QString raw = scratch.path("payload.img");
    REQUIRE(writeFile(raw, payloadOfSize(kRawSize)));
    REQUIRE(runShell(QStringLiteral("xz -k -f '%1'").arg(raw)));
    const QString mislabelled = scratch.path("named_wrong.img");
    REQUIRE(runShell(QStringLiteral("mv '%1.xz' '%2'").arg(raw, mislabelled)));

    const auto probed = imagesize::probeFormat(mislabelled);
    CHECK(probed.readable);
    CHECK(probed.filterCode == ARCHIVE_FILTER_XZ);
}
