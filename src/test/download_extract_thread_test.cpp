// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// DownloadExtractThread is the path almost every real write takes: images
// ship compressed, so this is DownloadThread with libarchive in front of it,
// decompressing on the way to the card. 759 branches, none covered.
//
// The archives here are built at test time by xz, gzip and zip rather than
// checked in, so the decompressors are fed real containers and the fixtures
// cannot drift from what the tools actually produce. Cases skip themselves
// when a compressor is missing.
//
// As with the plain DownloadThread tests, correctness is checked by reading
// the destination back with ordinary file I/O -- never by asking the writer
// what it thinks it wrote.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "downloadextractthread.h"
#include "localfileextractthread.h"
#include "archive_kind.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <memory>

#include <unistd.h>

#include "fixture_process.h"

namespace {

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-dx-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    QString filePath(const QString &name) const { return QDir(_path).filePath(name); }
    QString path() const { return _path; }

private:
    QString _path;
};

bool haveTool(const QString &name)
{
    for (const char *dir : {"/usr/bin/", "/bin/"}) {
        if (QFileInfo::exists(QString::fromUtf8(dir) + name))
            return true;
    }
    return false;
}

QString toolPath(const QString &name)
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/") + name)
               ? QStringLiteral("/usr/bin/") + name
               : QStringLiteral("/bin/") + name;
}

// An image with structure rather than a constant: a run of identical bytes
// compresses to almost nothing and would not exercise the streaming path.
QByteArray imageOfSize(int size, int seed)
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

bool runTool(const QString &tool, const QStringList &args, const QString &workingDir = {})
{
    QProcess proc;
    if (!workingDir.isEmpty())
        proc.setWorkingDirectory(workingDir);
    proc.start(toolPath(tool), args);
    proc.waitForFinished(120000);
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

struct Outcome {
    bool finished = false;
    bool succeeded = false;
    QString errorMessage;
};

Outcome runToCompletion(DownloadExtractThread &dt, int timeoutMs = 120000)
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

    // Only cancel on a timeout; cancelling a finished write tears down state
    // the assertions still want to look at.
    if (!outcome.finished)
        dt.cancelDownload();
    dt.wait(30000);
    return outcome;
}

// Write the raw image, compress it with the named tool, and return a thread
// wired to decompress it onto a scratch destination.
std::unique_ptr<DownloadExtractThread> makeExtract(const ScratchDir &scratch,
                                                   const QByteArray &image,
                                                   const QString &archivePath,
                                                   const QString &destName)
{
    const QString dest = scratch.filePath(destName);
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    const QByteArray url = QByteArray("file://") + archivePath.toUtf8();
    auto dt = std::make_unique<DownloadExtractThread>(url, dest.toUtf8(), QByteArray());
    dt->setExtractTotal(static_cast<uint64_t>(image.size()));
    return dt;
}

} // namespace

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

TEST_CASE("DownloadExtractThread writes an image by default", "[extract]")
{
    DownloadExtractThread dt("file:///nonexistent", "", "");

    // Decompressing a single image straight onto the card is the ordinary
    // case, so the destination is still opened as a device.
    CHECK(dt.isImage());
}

TEST_CASE("DownloadExtractThread stops being an image writer for multi-file", "[extract]")
{
    DownloadExtractThread dt("file:///nonexistent", "", "");
    REQUIRE(dt.isImage());

    // A multi-file archive is unpacked onto a filesystem rather than written
    // as a raw image, so the device path must be switched off -- otherwise
    // the base class opens the destination as a block device and the first
    // extracted file overwrites the partition table.
    dt.enableMultipleFileExtraction();
    CHECK_FALSE(dt.isImage());
}

TEST_CASE("DownloadExtractThread can be cancelled before it starts", "[extract]")
{
    DownloadExtractThread dt("file:///nonexistent", "", "");

    CHECK_NOTHROW(dt.cancelDownload());
    CHECK_FALSE(dt.successfull());
}

// ---------------------------------------------------------------------------
// Decompression
// ---------------------------------------------------------------------------

TEST_CASE("DownloadExtractThread decompresses an xz image onto the target", "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(2 * 1024 * 1024, 7);
    const QString raw = scratch.filePath(QStringLiteral("image.img"));
    REQUIRE(writeFile(raw, image));

    // -T1 keeps the container single-threaded and so byte-stable; the default
    // varies with the host's core count.
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));
    const QString archive = raw + QStringLiteral(".xz");
    REQUIRE(QFileInfo::exists(archive));

    auto dt = makeExtract(scratch, image, archive, QStringLiteral("xz-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // What lands on the "card" must be the original image, not the archive.
    const QByteArray written = readFile(scratch.filePath(QStringLiteral("xz-dest.img")));
    REQUIRE(written.size() >= image.size());
    CHECK(written.left(image.size()) == image);
}

TEST_CASE("DownloadExtractThread decompresses a gzip image onto the target", "[extract]")
{
    if (!haveTool(QStringLiteral("gzip")))
        SKIP("gzip is not installed, so no .gz image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 11);
    const QString raw = scratch.filePath(QStringLiteral("gz-image.img"));
    REQUIRE(writeFile(raw, image));

    REQUIRE(runTool(QStringLiteral("gzip"), {QStringLiteral("-1"), raw}));
    const QString archive = raw + QStringLiteral(".gz");
    REQUIRE(QFileInfo::exists(archive));

    // A different container through the same libarchive front end.
    auto dt = makeExtract(scratch, image, archive, QStringLiteral("gz-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("gz-dest.img"))).left(image.size()) == image);
}

TEST_CASE("DownloadExtractThread decompresses a zipped image onto the target", "[extract]")
{
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no .zip image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 13);
    const QString raw = scratch.filePath(QStringLiteral("zip-image.img"));
    REQUIRE(writeFile(raw, image));

    const QString archive = scratch.filePath(QStringLiteral("image.zip"));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-0"), archive,
                     QStringLiteral("zip-image.img")},
                    QFileInfo(raw).absolutePath()));
    REQUIRE(QFileInfo::exists(archive));

    // Zip is the case with a directory rather than a plain stream, so the
    // entry has to be selected rather than just decompressed.
    auto dt = makeExtract(scratch, image, archive, QStringLiteral("zip-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("zip-dest.img"))).left(image.size()) == image);
}

TEST_CASE("DownloadExtractThread writes an uncompressed image unchanged", "[extract]")
{
    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 17);
    const QString raw = scratch.filePath(QStringLiteral("plain.img"));
    REQUIRE(writeFile(raw, image));

    // Not every image ships compressed; libarchive has to pass a raw one
    // through rather than refusing it for having no recognisable container.
    auto dt = makeExtract(scratch, image, raw, QStringLiteral("plain-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("plain-dest.img"))).left(image.size()) == image);
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST_CASE("DownloadExtractThread reports a corrupt archive", "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built to corrupt");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 19);
    const QString raw = scratch.filePath(QStringLiteral("corrupt.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    const QString archive = raw + QStringLiteral(".xz");
    QByteArray damaged = readFile(archive);
    REQUIRE(damaged.size() > 2048);
    // Corrupt the middle: the header still parses, so this fails during the
    // stream rather than being rejected up front.
    for (int i = 1024; i < 1536; ++i)
        damaged[i] = static_cast<char>(damaged[i] ^ 0xFF);
    REQUIRE(writeFile(archive, damaged));

    auto dt = makeExtract(scratch, image, archive, QStringLiteral("corrupt-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    REQUIRE(outcome.finished);

    // A corrupt image must be refused. Reporting success here would hand the
    // user a card that cannot boot and no reason why.
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());
}

TEST_CASE("DownloadExtractThread reports an archive that is not there", "[extract]")
{
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("unused.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    DownloadExtractThread dt("file:///nonexistent-rpi-imager/missing.img.xz", dest.toUtf8(),
                             QByteArray());

    const Outcome outcome = runToCompletion(dt, 60000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("DownloadExtractThread reports a destination it cannot open", "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 23);
    const QString raw = scratch.filePath(QStringLiteral("nodest.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    DownloadExtractThread dt(QByteArray("file://") + (raw + QStringLiteral(".xz")).toUtf8(),
                             "/nonexistent-rpi-imager-dir/deeper/dest.img", QByteArray());

    const Outcome outcome = runToCompletion(dt, 60000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------

TEST_CASE("DownloadExtractThread accounts for decompressed bytes", "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(2 * 1024 * 1024, 29);
    const QString raw = scratch.filePath(QStringLiteral("prog.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    auto dt = makeExtract(scratch, image, raw + QStringLiteral(".xz"),
                          QStringLiteral("prog-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // The progress bar tracks decompressed bytes, not archive bytes: a
    // counter stuck at the compressed size would stall the UI well short of
    // the end even on a perfectly good write.
    CHECK(dt->bytesWritten() >= static_cast<uint64_t>(image.size()));
}

// ---------------------------------------------------------------------------
// LocalFileExtractThread
// ---------------------------------------------------------------------------
//
// The same decompression pipeline for an image the user already has on disk,
// chosen with "Use custom". It reads the archive through libarchive directly
// rather than through curl, and sniffs the container first with
// _testArchiveFormat() so a plain .img is written raw instead of being
// refused for having no recognisable header.

TEST_CASE("LocalFileExtractThread writes a local xz image", "[extract][local]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 101);
    const QString raw = scratch.filePath(QStringLiteral("local.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    const QString archive = raw + QStringLiteral(".xz");
    const QString dest = scratch.filePath(QStringLiteral("local-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    // The path is parsed with QUrl::toLocalFile(), so it has to be a
    // file:// URL -- a bare path yields an empty filename and "Error opening
    // image file".
    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(dest).left(image.size()) == image);
}

TEST_CASE("LocalFileExtractThread writes a local uncompressed image", "[extract][local]")
{
    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 103);
    const QString raw = scratch.filePath(QStringLiteral("plain-local.img"));
    REQUIRE(writeFile(raw, image));

    const QString dest = scratch.filePath(QStringLiteral("plain-local-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    // No container at all: the format sniff has to fall through to the raw
    // path rather than treating an unrecognised header as corruption.
    LocalFileExtractThread dt(QByteArray("file://") + raw.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(dest).left(image.size()) == image);
}

TEST_CASE("LocalFileExtractThread reports a file that is not there", "[extract][local]")
{
    ScratchDir scratch;
    const QString dest = scratch.filePath(QStringLiteral("missing-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(1024 * 1024, '\0')));

    LocalFileExtractThread dt("file:///nonexistent-rpi-imager/nowhere.img.xz",
                              dest.toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 60000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("LocalFileExtractThread reports a corrupt local archive", "[extract][local]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built to corrupt");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 107);
    const QString raw = scratch.filePath(QStringLiteral("bad-local.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    const QString archive = raw + QStringLiteral(".xz");
    QByteArray damaged = readFile(archive);
    REQUIRE(damaged.size() > 2048);
    for (int i = 1024; i < 1536; ++i)
        damaged[i] = static_cast<char>(damaged[i] ^ 0xFF);
    REQUIRE(writeFile(archive, damaged));

    const QString dest = scratch.filePath(QStringLiteral("bad-local-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("A .xz that is not an archive at all is refused, not written raw",
          "[extract][local]")
{
    // The probe answers "can libarchive extract this?", and a no used to mean
    // "then it must be a raw disk image". For a file whose name says it is a
    // container, the only way the probe fails is that the file is corrupt or
    // half-downloaded -- and writing it raw puts the compressed bytes on the
    // card and calls the write a success. The user gets a card that does not
    // boot and nothing that says why.
    ScratchDir scratch;
    const QString archive = scratch.filePath(QStringLiteral("truncated.img.xz"));
    REQUIRE(writeFile(archive, imageOfSize(64 * 1024, 109)));   // not xz at all

    const QByteArray blank(1024 * 1024, '\0');
    const QString dest = scratch.filePath(QStringLiteral("notxz-dest.img"));
    REQUIRE(writeFile(dest, blank));

    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 180000);
    REQUIRE(outcome.finished);
    INFO("error: " << outcome.errorMessage.toStdString());
    CHECK_FALSE(outcome.succeeded);

    // And it must say so rather than failing silently.
    CHECK_FALSE(outcome.errorMessage.isEmpty());

    // The card is left as it was: nothing half-written to boot from.
    CHECK(readFile(dest) == blank);
}

TEST_CASE("A .cache file is still written raw when it is a plain image",
          "[extract][local]")
{
    // .cache is deliberately not treated as claiming compression: it holds
    // whatever the last download happened to be, which is usually already
    // decompressed. Refusing it would break every write from the cache.
    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 111);
    const QString cached = scratch.filePath(QStringLiteral("lastdownload.cache"));
    REQUIRE(writeFile(cached, image));

    const QString dest = scratch.filePath(QStringLiteral("cache-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (1024 * 1024), '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + cached.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);
    CHECK(readFile(dest).left(image.size()) == image);
}

// ---------------------------------------------------------------------------
// Container or disk image
// ---------------------------------------------------------------------------
//
// Everything above turns on _testArchiveFormat() getting one question right:
// are these bytes the disk image, or a container holding one? Unpack a plain
// .img and libarchive hands the same bytes back, slower. Copy a container
// verbatim and the card gets the zip instead of what is inside it.
//
// The test that used to answer it read the first entry and treated an empty
// read as "raw disk image". The first entry of an archive built by zipping a
// folder is the folder, and a directory entry has no data -- so a valid .zip
// or .tar.gz holding an image was called a raw image, and run() then refused
// it outright because its name says it is a container. The user was told
// their own file was corrupt and to download it again.
//
// It is worth being clear about who reaches this. ImageWriter picks image
// mode when the archive holds one file, counting only entries with a size,
// so the folder entry does not count and a zipped folder arrives here in
// image mode. The first two cases assert that end to end.

TEST_CASE("An image inside a zipped folder is extracted, not called corrupt",
          "[extract][local]")
{
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no folder archive can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 211);
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("holder"))));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("holder/os.img")), image));

    // -r, so the archive begins with a "holder/" directory entry -- what any
    // zip of a folder looks like, from Finder, Explorer or the command line.
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-r"),
                     QStringLiteral("folder.zip"), QStringLiteral("holder")},
                    scratch.path()));

    const QString archive = scratch.filePath(QStringLiteral("folder.zip"));
    const QString dest = scratch.filePath(QStringLiteral("folder-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    // Not just "no error": the card has to hold the image, not the zip.
    CHECK(readFile(dest).left(image.size()) == image);
}

TEST_CASE("An image inside a tarred folder is extracted, not called corrupt",
          "[extract][local]")
{
    if (!haveTool(QStringLiteral("tar")))
        SKIP("tar is not installed, so no folder archive can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 212);
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("holder"))));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("holder/os.img")), image));

    // The same shape behind a decompression filter, which is the case the old
    // test got most obviously wrong: gzip is unambiguous about there being
    // something to unpack, and it was still classified as a raw image.
    REQUIRE(runTool(QStringLiteral("tar"),
                    {QStringLiteral("-czf"), QStringLiteral("folder.tar.gz"),
                     QStringLiteral("holder")},
                    scratch.path()));

    const QString archive = scratch.filePath(QStringLiteral("folder.tar.gz"));
    const QString dest = scratch.filePath(QStringLiteral("tar-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(dest).left(image.size()) == image);
}

TEST_CASE("An archive with nothing in it is refused, not written as an empty card",
          "[extract][local]")
{
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no folder archive can be built");

    // Walking past empty entries has to stop somewhere. If the walk runs off
    // the end of the archive the old behaviour is back: nothing to read, so
    // nothing written, and the write reports success. That is the outcome the
    // user cannot recover from on their own -- an unbootable card, and nothing
    // on screen suggesting a retry.
    ScratchDir scratch;
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("empty-holder"))));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-r"),
                     QStringLiteral("nothing.zip"), QStringLiteral("empty-holder")},
                    scratch.path()));

    const QString archive = scratch.filePath(QStringLiteral("nothing.zip"));
    const QString dest = scratch.filePath(QStringLiteral("nothing-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(2 * 1024 * 1024, '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(dt, 180000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);

    // What the user is told, not merely that they are told something.
    //
    // Worth recording why this row is about the wording. Removing the guard
    // that produces this message does not make the write succeed: reading
    // data past the end of an archive puts libarchive in a state it refuses,
    // so the write still fails. What it fails with is
    // "Error extracting archive: INTERNAL ERROR: Function called in wrong
    // state", which tells someone who picked a zip with nothing in it
    // nothing at all. So the assertion is on the message: no libarchive
    // internals, and a mention of the thing that is missing.
    INFO("message: " << outcome.errorMessage.toStdString());
    CHECK_FALSE(outcome.errorMessage.contains(QStringLiteral("INTERNAL ERROR")));
    CHECK(outcome.errorMessage.contains(QStringLiteral("image"), Qt::CaseInsensitive));
}

TEST_CASE("An ISO is copied to the card, not unpacked onto it", "[extract][local]")
{
    if (!haveTool(QStringLiteral("xorriso")))
        SKIP("xorriso is not installed, so no ISO can be built");

    // The other half of the same decision, and the reason it cannot simply be
    // "libarchive recognised a format, so unpack it". An ISO is a filesystem
    // libarchive is perfectly happy to read the files out of -- and doing so
    // would put those files on the card in place of the bootable image.
    ScratchDir scratch;
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("iso-root"))));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("iso-root/payload.bin")),
                      imageOfSize(64 * 1024, 213)));

    REQUIRE(runTool(QStringLiteral("xorriso"),
                    {QStringLiteral("-as"), QStringLiteral("mkisofs"),
                     QStringLiteral("-quiet"), QStringLiteral("-o"),
                     QStringLiteral("disc.iso"), QStringLiteral("iso-root")},
                    scratch.path()));

    const QString iso = scratch.filePath(QStringLiteral("disc.iso"));
    const QByteArray isoBytes = readFile(iso);
    REQUIRE(isoBytes.size() > 0);

    const QString dest = scratch.filePath(QStringLiteral("iso-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(isoBytes.size() + (2 * 1024 * 1024), '\0')));

    LocalFileExtractThread dt(QByteArray("file://") + iso.toUtf8(), dest.toUtf8(),
                              QByteArray());
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(isoBytes.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(dest).left(isoBytes.size()) == isoBytes);
}

TEST_CASE("What libarchive recognised decides how the file is written",
          "[extract][local]")
{
    // The decision itself, against the constants libarchive actually reports.
    // The end-to-end cases above cover the four containers that can be built
    // from tools on the machine; this covers the ones that cannot, and pins
    // the shape of the rule rather than four instances of it.
    //
    // The filter half matters most: a compressed image reports RAW exactly as
    // a plain one does, and the decompression filter is the only thing telling
    // them apart. Miss it and .img.xz goes to the card still compressed.
    struct Case {
        const char *what;
        int format;
        int filter;
        bool isTheImage;
    };

    const Case cases[] = {
        {".img",            ARCHIVE_FORMAT_RAW,               ARCHIVE_FILTER_NONE,  true},
        {".iso",            ARCHIVE_FORMAT_ISO9660,           ARCHIVE_FILTER_NONE,  true},
        {".iso, Rock Ridge", ARCHIVE_FORMAT_ISO9660_ROCKRIDGE, ARCHIVE_FILTER_NONE,  true},
        {".img.xz",         ARCHIVE_FORMAT_RAW,               ARCHIVE_FILTER_XZ,    false},
        {".img.gz",         ARCHIVE_FORMAT_RAW,               ARCHIVE_FILTER_GZIP,  false},
        {".img.zst",        ARCHIVE_FORMAT_RAW,               ARCHIVE_FILTER_ZSTD,  false},
        {".zip",            ARCHIVE_FORMAT_ZIP,               ARCHIVE_FILTER_NONE,  false},
        {".tar",            ARCHIVE_FORMAT_TAR_GNUTAR,        ARCHIVE_FILTER_NONE,  false},
        {".tar.gz",         ARCHIVE_FORMAT_TAR_GNUTAR,        ARCHIVE_FILTER_GZIP,  false},
        {".7z",             ARCHIVE_FORMAT_7ZIP,              ARCHIVE_FILTER_NONE,  false},
        // A compressed ISO is still something to decompress first.
        {".iso.xz",         ARCHIVE_FORMAT_ISO9660,           ARCHIVE_FILTER_XZ,    false},
    };

    for (const Case &c : cases) {
        INFO(c.what);
        CHECK(archivekind::bytesAreTheDiskImage(c.format, c.filter) == c.isTheImage);
    }
}

// ---------------------------------------------------------------------------
// Which corruption it was
// ---------------------------------------------------------------------------
//
// A hash that does not match ends the write, and the message decides where
// the user goes next. There are three, and they are not interchangeable: a
// corrupt cache is removed and refetched with nothing for the user to do, a
// corrupt file of their own has to be replaced by them, and a corrupt
// download is worth another try. Telling somebody whose own file is bad that
// the download will restart sends them to wait for something that is not
// going to happen.
//
// Until now only the refusal was checked, not what it said.

TEST_CASE("A corrupt cache says it will be replaced by itself", "[extract][hash]")
{
    // The cache is Imager's own file in Imager's own directory. The user did
    // not put it there and cannot fix it, so the message has to say that it
    // is being dealt with.
    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 221);
    const QString cached = scratch.filePath(QStringLiteral("lastdownload.cache"));
    REQUIRE(writeFile(cached, image));

    const QString dest = scratch.filePath(QStringLiteral("cache-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    const QByteArray wrong =
        QCryptographicHash::hash(QByteArray("not this image"), QCryptographicHash::Sha256).toHex();

    LocalFileExtractThread dt(QByteArray("file://") + cached.toUtf8(), dest.toUtf8(), wrong);
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    REQUIRE(outcome.finished);
    REQUIRE_FALSE(outcome.succeeded);

    const std::string message = outcome.errorMessage.toStdString();
    INFO("message: " << message);
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("Cached file"));
    // The half that matters: it fixes itself.
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("will be removed"));
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("restart"));
}

TEST_CASE("A corrupt file of the user's own is named as theirs", "[extract][hash]")
{
    // Chosen with "Use custom". Nothing will replace it, so the message says
    // it is the file that is wrong and gives both hashes -- which is how
    // somebody with several downloads of the same image works out which one
    // they picked.
    ScratchDir scratch;
    const QByteArray image = imageOfSize(256 * 1024, 222);
    const QString own = scratch.filePath(QStringLiteral("my-own.img"));
    REQUIRE(writeFile(own, image));

    const QString dest = scratch.filePath(QStringLiteral("own-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    const QByteArray wrong =
        QCryptographicHash::hash(QByteArray("not this image"), QCryptographicHash::Sha256).toHex();

    LocalFileExtractThread dt(QByteArray("file://") + own.toUtf8(), dest.toUtf8(), wrong);
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 180000);
    REQUIRE(outcome.finished);
    REQUIRE_FALSE(outcome.succeeded);

    const std::string message = outcome.errorMessage.toStdString();
    INFO("message: " << message);
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring("Local file"));
    CHECK_THAT(message, Catch::Matchers::ContainsSubstring(std::string(wrong.constData())));
    // And not the cache wording, which would have them waiting for a
    // download that is never going to start.
    CHECK_THAT(message, !Catch::Matchers::ContainsSubstring("will be removed"));
}

TEST_CASE("LocalFileExtractThread can be cancelled before it starts", "[extract][local]")
{
    LocalFileExtractThread dt("/nonexistent", "", "");

    CHECK_NOTHROW(dt.cancelDownload());
    CHECK_FALSE(dt.successfull());
}

// DownloadExtractThread signals across threads and this file waits on those
// with a QEventLoop, which needs a QCoreApplication to dispatch them.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

// ---------------------------------------------------------------------------
// Other containers
// ---------------------------------------------------------------------------
//
// libarchive sniffs the container rather than trusting the extension, so each
// format takes its own detection and decompression path. Raspberry Pi images
// ship as .xz today, but users point the imager at whatever they have.

TEST_CASE("DownloadExtractThread decompresses a zstd image", "[extract]")
{
    if (!haveTool(QStringLiteral("zstd")))
        SKIP("zstd is not installed, so no .zst image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 31);
    const QString raw = scratch.filePath(QStringLiteral("zst-image.img"));
    REQUIRE(writeFile(raw, image));

    REQUIRE(runTool(QStringLiteral("zstd"),
                    {QStringLiteral("-q"), QStringLiteral("-1"), QStringLiteral("--rm"), raw}));
    const QString archive = raw + QStringLiteral(".zst");
    REQUIRE(QFileInfo::exists(archive));

    auto dt = makeExtract(scratch, image, archive, QStringLiteral("zst-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("zst-dest.img"))).left(image.size()) == image);
}

TEST_CASE("DownloadExtractThread decompresses an image inside a tar", "[extract]")
{
    if (!haveTool(QStringLiteral("tar")) || !haveTool(QStringLiteral("gzip")))
        SKIP("tar or gzip is not installed, so no .tar.gz image can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 37);
    const QString raw = scratch.filePath(QStringLiteral("tar-image.img"));
    REQUIRE(writeFile(raw, image));

    const QString archive = scratch.filePath(QStringLiteral("image.tar.gz"));
    // A tar has entry headers in front of the payload, unlike the bare
    // compressed streams above, so the entry has to be walked to rather than
    // decompressed straight through.
    REQUIRE(runTool(QStringLiteral("tar"),
                    {QStringLiteral("-czf"), archive, QStringLiteral("tar-image.img")},
                    QFileInfo(raw).absolutePath()));
    REQUIRE(QFileInfo::exists(archive));

    auto dt = makeExtract(scratch, image, archive, QStringLiteral("tar-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(scratch.filePath(QStringLiteral("tar-dest.img"))).left(image.size()) == image);
}

TEST_CASE("DownloadExtractThread decompresses a bzip2 image", "[extract]")
{
    if (!haveTool(QStringLiteral("bzip2")))
        SKIP("bzip2 is not installed, so no .bz2 image can be built to extract");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 41);
    const QString raw = scratch.filePath(QStringLiteral("bz-image.img"));
    REQUIRE(writeFile(raw, image));

    REQUIRE(runTool(QStringLiteral("bzip2"), {QStringLiteral("-1"), raw}));
    const QString archive = raw + QStringLiteral(".bz2");
    REQUIRE(QFileInfo::exists(archive));

    auto dt = makeExtract(scratch, image, archive, QStringLiteral("bz-dest.img"));
    dt->setVerifyEnabled(false);

    const Outcome outcome = runToCompletion(*dt, 180000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    if (outcome.succeeded)
        CHECK(readFile(scratch.filePath(QStringLiteral("bz-dest.img"))).left(image.size()) ==
              image);
}

TEST_CASE("DownloadExtractThread verifies a decompressed image against its hash",
          "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(1024 * 1024, 43);
    const QString raw = scratch.filePath(QStringLiteral("hashed-image.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    const QString archive = raw + QStringLiteral(".xz");
    const QString dest = scratch.filePath(QStringLiteral("hashed-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    // The hash the OS list publishes is of the *decompressed* image, so this
    // is checked after extraction rather than against the archive.
    const QByteArray hash = QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex();

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(), hash);
    dt.setVerifyEnabled(true);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 240000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);
}

TEST_CASE("DownloadExtractThread rejects a decompressed image with the wrong hash",
          "[extract]")
{
    if (!haveTool(QStringLiteral("xz")))
        SKIP("xz is not installed, so no .xz image can be built");

    ScratchDir scratch;
    const QByteArray image = imageOfSize(512 * 1024, 47);
    const QString raw = scratch.filePath(QStringLiteral("wrong-image.img"));
    REQUIRE(writeFile(raw, image));
    REQUIRE(runTool(QStringLiteral("xz"), {QStringLiteral("-T1"), QStringLiteral("-2"), raw}));

    const QString archive = raw + QStringLiteral(".xz");
    const QString dest = scratch.filePath(QStringLiteral("wrong-dest.img"));
    REQUIRE(writeFile(dest, QByteArray(image.size() + (2 * 1024 * 1024), '\0')));

    const QByteArray wrong =
        QCryptographicHash::hash(QByteArray("a different image"), QCryptographicHash::Sha256)
            .toHex();

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(), dest.toUtf8(), wrong);
    dt.setVerifyEnabled(false);
    dt.setExtractTotal(static_cast<uint64_t>(image.size()));

    const Outcome outcome = runToCompletion(dt, 240000);
    REQUIRE(outcome.finished);
    // An archive that unpacks cleanly but is not the image that was asked for
    // must still be refused.
    CHECK_FALSE(outcome.succeeded);
}

// ---------------------------------------------------------------------------
// Multi-file extraction onto a real filesystem
// ---------------------------------------------------------------------------
//
// enableMultipleFileExtraction() unpacks an archive onto the target's
// filesystem rather than writing it as a raw image -- the path used for
// multi-file zips. It looks the device up in the drive list, and only falls
// back to mounting the partition itself when nothing has mounted it already.
//
// The fixture below takes the first branch: it builds a partitioned image on
// a loop device, mounts the FAT partition with uid= so the test user owns the
// files, and lets the drive list find it. That means the extraction runs as
// an ordinary user, with root needed only to set the fixture up.
//
// Everything is synthetic -- a scratch file on a loop device this object
// created. No real media is involved.

namespace {

bool canRunPrivileged()
{
    QProcess probe;
    probe.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    probe.waitForFinished(10000);
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

bool runPrivileged(const QString &program, const QStringList &args, QByteArray *out = nullptr)
{
    QProcess proc;
    proc.start(QStringLiteral("sudo"), QStringList{QStringLiteral("-n"), program} << args);
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    if (out) *out = proc.readAllStandardOutput();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool haveMkfsVfat()
{
    return QFileInfo::exists(QStringLiteral("/sbin/mkfs.vfat")) ||
           QFileInfo::exists(QStringLiteral("/usr/sbin/mkfs.vfat"));
}

QString mkfsVfatPath()
{
    return QFileInfo::exists(QStringLiteral("/sbin/mkfs.vfat")) ? QStringLiteral("/sbin/mkfs.vfat")
                                                                : QStringLiteral("/usr/sbin/mkfs.vfat");
}

// A loop device carrying an MBR with one FAT32 partition, already mounted and
// owned by the invoking user. Unmounts and detaches itself.
class MountedFatDevice
{
public:
    explicit MountedFatDevice(int megabytes)
    {
        _dir = QDir::temp().filePath(QStringLiteral("rpi-imager-mf-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(_dir);
        _mountPoint = QDir(_dir).filePath(QStringLiteral("mnt"));
        QDir().mkpath(_mountPoint);
        const QString backing = QDir(_dir).filePath(QStringLiteral("disk.img"));

        const qint64 partOffset = 2048LL * 512;
        const qint64 fatBytes = static_cast<qint64>(megabytes) * 1024 * 1024;

        // FAT filesystem first, then an MBR in front of it.
        const QString fatPath = QDir(_dir).filePath(QStringLiteral("fat.img"));
        { QFile f(fatPath); if (!f.open(QIODevice::WriteOnly) || !f.resize(fatBytes)) return; }
        QProcess mkfs;
        mkfs.start(mkfsVfatPath(), {QStringLiteral("-F"), QStringLiteral("32"),
                                    QStringLiteral("-n"), QStringLiteral("bootfs"), fatPath});
        mkfs.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
        if (mkfs.exitCode() != 0) return;

        QFile fat(fatPath);
        if (!fat.open(QIODevice::ReadOnly)) return;
        const QByteArray fatImage = fat.readAll();
        fat.close();
        QFile::remove(fatPath);

        QByteArray disk(partOffset, '\0');
        auto put32 = [&](int off, quint32 v) {
            disk[off] = char(v & 0xFF); disk[off+1] = char((v >> 8) & 0xFF);
            disk[off+2] = char((v >> 16) & 0xFF); disk[off+3] = char((v >> 24) & 0xFF);
        };
        disk[0x1BE] = char(0x80); disk[0x1C2] = char(0x0C);
        put32(0x1C6, 2048);
        put32(0x1CA, static_cast<quint32>(fatBytes / 512));
        disk[0x1FE] = char(0x55); disk[0x1FF] = char(0xAA);
        disk.append(fatImage);

        { QFile f(backing); if (!f.open(QIODevice::WriteOnly)) return; f.write(disk); f.close(); }

        // -P so the kernel scans the table and creates the partition node.
        QByteArray out;
        if (!runPrivileged(QStringLiteral("losetup"),
                           {QStringLiteral("-P"), QStringLiteral("-f"),
                            QStringLiteral("--show"), backing}, &out))
            return;
        _loop = QString::fromUtf8(out).trimmed();
        if (!_loop.startsWith(QStringLiteral("/dev/loop"))) return;
        _partition = _loop + QStringLiteral("p1");

        for (int i = 0; i < 100 && !QFileInfo::exists(_partition); ++i)
            QThread::msleep(20);
        if (!QFileInfo::exists(_partition)) return;

        runPrivileged(QStringLiteral("chown"),
                      {QString::number(::geteuid()), _loop});

        // uid= hands the mounted files to the test user, so the extraction
        // itself needs no privilege.
        if (!runPrivileged(QStringLiteral("mount"),
                           {QStringLiteral("-t"), QStringLiteral("vfat"),
                            QStringLiteral("-o"),
                            QStringLiteral("uid=%1,gid=%2").arg(::geteuid()).arg(::getegid()),
                            _partition, _mountPoint}))
            return;
        _mounted = true;
    }

    ~MountedFatDevice()
    {
        if (_mounted)
            runPrivileged(QStringLiteral("umount"), {_mountPoint});
        if (!_loop.isEmpty())
            runPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
        QDir(_dir).removeRecursively();
    }

    MountedFatDevice(const MountedFatDevice &) = delete;
    MountedFatDevice &operator=(const MountedFatDevice &) = delete;

    bool isReady() const { return _mounted; }
    QString device() const { return _loop; }
    QString mountPoint() const { return _mountPoint; }

private:
    QString _dir, _mountPoint, _loop, _partition;
    bool _mounted = false;
};

} // namespace

TEST_CASE("DownloadExtractThread unpacks a multi-file archive onto the target",
          "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no multi-file archive can be built");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    // A handful of files, as a multi-file OS image ships.
    const QStringList names = {QStringLiteral("config.txt"), QStringLiteral("cmdline.txt"),
                               QStringLiteral("kernel8.img")};
    for (const QString &n : names)
        REQUIRE(writeFile(scratch.filePath(n), ("contents of " + n).toUtf8()));

    const QString archive = scratch.filePath(QStringLiteral("multi.zip"));
    QStringList zipArgs{QStringLiteral("-q"), QStringLiteral("-0"), archive};
    zipArgs << names;
    REQUIRE(runTool(QStringLiteral("zip"), zipArgs, QFileInfo(archive).absolutePath()));

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();
    REQUIRE_FALSE(dt.isImage());

    const Outcome outcome = runToCompletion(dt, 240000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    // Read the files back off the mounted filesystem: the extraction is only
    // real if they landed there.
    for (const QString &n : names) {
        const QString onDisk = QDir(device.mountPoint()).filePath(n);
        INFO("expected on the card: " << onDisk.toStdString());
        CHECK(QFileInfo::exists(onDisk));
        CHECK(readFile(onDisk) == ("contents of " + n).toUtf8());
    }
}

TEST_CASE("DownloadExtractThread reports a multi-file archive it cannot read",
          "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");

    MountedFatDevice device(32);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    DownloadExtractThread dt("file:///nonexistent-rpi-imager/missing.zip",
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 120000);
    REQUIRE(outcome.finished);
    CHECK_FALSE(outcome.succeeded);
}

TEST_CASE("A multi-file archive that fails leaves nothing behind on the card",
          "[extract][multifile]")
{
    // The failure path deletes what it already unpacked, and that matters
    // more here than for a disk image: an image that stops halfway leaves a
    // card that plainly will not boot, but a directory tree that stops
    // halfway leaves one that looks populated. A user who does not notice the
    // error has a card with some of an OS on it.
    //
    // Reached by giving a good archive the wrong expected hash, which is what
    // a corrupted download looks like: everything unpacks, then the check at
    // the end rejects it.
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no multi-file archive can be built");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    const QStringList names = {QStringLiteral("config.txt"), QStringLiteral("cmdline.txt"),
                               QStringLiteral("kernel8.img")};
    for (const QString &n : names)
        REQUIRE(writeFile(scratch.filePath(n), ("contents of " + n).toUtf8()));

    const QString archive = scratch.filePath(QStringLiteral("corrupt.zip"));
    QStringList zipArgs{QStringLiteral("-q"), QStringLiteral("-0"), archive};
    zipArgs << names;
    REQUIRE(runTool(QStringLiteral("zip"), zipArgs, QFileInfo(archive).absolutePath()));

    // A hash that is the right shape and the wrong value.
    const QByteArray wrongHash(64, 'b');

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), wrongHash);
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    REQUIRE(outcome.finished);
    INFO("error: " << outcome.errorMessage.toStdString());
    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errorMessage.isEmpty());

    // Nothing it unpacked is still there.
    for (const QString &n : names) {
        const QString onDisk = QDir(device.mountPoint()).filePath(n);
        INFO("should have been removed: " << onDisk.toStdString());
        CHECK_FALSE(QFileInfo::exists(onDisk));
    }
}

TEST_CASE("A truncated multi-file archive leaves no partial tree",
          "[extract][multifile]")
{
    // The other way it fails: the download stopped early, so the archive ends
    // mid-entry. Some files unpack, then libarchive gives up. Those files must
    // go too, directories included -- an empty overlays/ left behind is the
    // kind of thing that makes a later write look like it worked.
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no multi-file archive can be built");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("overlays"))));
    const QStringList names = {QStringLiteral("config.txt"),
                               QStringLiteral("cmdline.txt"),
                               QStringLiteral("overlays/disable-bt.dtbo"),
                               QStringLiteral("kernel8.img")};
    for (const QString &n : names)
        REQUIRE(writeFile(scratch.filePath(n), QByteArray(64 * 1024, 'Z')));

    const QString archive = scratch.filePath(QStringLiteral("short.zip"));
    QStringList zipArgs{QStringLiteral("-q"), QStringLiteral("-0"), QStringLiteral("-r"), archive};
    zipArgs << names;
    REQUIRE(runTool(QStringLiteral("zip"), zipArgs, QFileInfo(archive).absolutePath()));

    // Cut it off partway so the first entries are whole and the rest is not.
    {
        const QByteArray whole = readFile(archive);
        REQUIRE(whole.size() > 100 * 1024);
        REQUIRE(writeFile(archive, whole.left(whole.size() * 2 / 5)));
    }

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    REQUIRE(outcome.finished);
    INFO("error: " << outcome.errorMessage.toStdString());
    CHECK_FALSE(outcome.succeeded);

    for (const QString &n : names) {
        const QString onDisk = QDir(device.mountPoint()).filePath(n);
        INFO("should have been removed: " << onDisk.toStdString());
        CHECK_FALSE(QFileInfo::exists(onDisk));
    }
}

TEST_CASE("DownloadExtractThread unpacks nested directories", "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    // overlays/ is exactly the shape a real boot partition has, and the
    // directory has to be created on the target rather than assumed.
    REQUIRE(QDir().mkpath(scratch.filePath(QStringLiteral("overlays"))));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("config.txt")), "arm_64bit=1\n"));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("overlays/disable-bt.dtbo")),
                      "overlay one"));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("overlays/vc4-kms-v3d.dtbo")),
                      "overlay two"));

    const QString archive = scratch.filePath(QStringLiteral("nested.zip"));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-0"), QStringLiteral("-r"), archive,
                     QStringLiteral("config.txt"), QStringLiteral("overlays")},
                    QFileInfo(archive).absolutePath()));

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    CHECK(outcome.succeeded);

    const QDir mounted(device.mountPoint());
    CHECK(QFileInfo::exists(mounted.filePath(QStringLiteral("config.txt"))));
    CHECK(QFileInfo::exists(mounted.filePath(QStringLiteral("overlays/disable-bt.dtbo"))));
    CHECK(readFile(mounted.filePath(QStringLiteral("overlays/vc4-kms-v3d.dtbo"))) ==
          QByteArray("overlay two"));
}

TEST_CASE("DownloadExtractThread unpacks many small files", "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    QStringList names;
    // Enough entries to push the FAT root directory past one cluster, which
    // is where directory-extension bugs show up.
    for (int i = 0; i < 64; ++i) {
        const QString n = QStringLiteral("overlay-%1.dtbo").arg(i, 3, 10, QChar('0'));
        REQUIRE(writeFile(scratch.filePath(n), QByteArray::number(i)));
        names << n;
    }

    const QString archive = scratch.filePath(QStringLiteral("many.zip"));
    QStringList args{QStringLiteral("-q"), QStringLiteral("-0"), archive};
    args << names;
    REQUIRE(runTool(QStringLiteral("zip"), args, QFileInfo(archive).absolutePath()));

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 300000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    const QDir mounted(device.mountPoint());
    int found = 0;
    for (const QString &n : names)
        if (QFileInfo::exists(mounted.filePath(n))) ++found;
    INFO("files landed: " << found << " of " << names.size());
    CHECK(found == names.size());
}

TEST_CASE("DownloadExtractThread unpacks a compressed multi-file archive",
          "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    // Actually deflated rather than stored, so the decompressor runs per
    // entry rather than the data being copied straight through.
    const QByteArray payload = imageOfSize(512 * 1024, 71);
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("kernel8.img")), payload));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("config.txt")), "arm_64bit=1\n"));

    const QString archive = scratch.filePath(QStringLiteral("deflated.zip"));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-9"), archive,
                     QStringLiteral("kernel8.img"), QStringLiteral("config.txt")},
                    QFileInfo(archive).absolutePath()));

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 300000);
    INFO("error: " << outcome.errorMessage.toStdString());
    REQUIRE(outcome.finished);
    REQUIRE(outcome.succeeded);

    CHECK(readFile(QDir(device.mountPoint()).filePath(QStringLiteral("kernel8.img"))) == payload);
}

// A corrupt multi-file archive is extracted without complaint.
//
// The archive below is damaged inside the compressed stream and unzip -t
// rejects it outright ("invalid compressed data to inflate") -- the test
// asserts that first, so a pass here cannot be the corruption having missed.
// The imager extracts it anyway: libarchive returns neither ARCHIVE_FATAL nor
// ARCHIVE_WARN for it, so _checkResult() never fires, all fifteen data blocks
// are written, and the write is reported successful.
//
// The single-image path does catch this -- see "reports a corrupt archive"
// above, which passes -- so the gap is specific to multi-file extraction. The
// user-visible outcome is a card that looks written and will not boot, with
// nothing in the log to say why.
//
// The gap was in _checkResult(): a zip CRC mismatch comes back as
// ARCHIVE_FAILED, which it logged and swallowed, so the entry was written
// out and the write reported successful. The data loop now treats
// ARCHIVE_FAILED as terminal, which is what libarchive means by it.
TEST_CASE("DownloadExtractThread reports a corrupt multi-file archive",
          "[extract][multifile]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed");

    MountedFatDevice device(32);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("a.txt")), imageOfSize(256 * 1024, 73)));
    const QString archive = scratch.filePath(QStringLiteral("bad.zip"));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-9"), archive,
                     QStringLiteral("a.txt")},
                    QFileInfo(archive).absolutePath()));

    QByteArray damaged = readFile(archive);
    REQUIRE(damaged.size() > 1024);
    // Corrupt a span inside the compressed stream, past the local header.
    for (int i = 400; i < 900 && i < damaged.size(); ++i)
        damaged[i] = static_cast<char>(damaged[i] ^ 0xFF);
    REQUIRE(writeFile(archive, damaged));

    // Confirm with an independent tool that the archive really is broken, so
    // a pass below cannot be the corruption having missed.
    if (haveTool(QStringLiteral("unzip"))) {
        QProcess check;
        check.start(toolPath(QStringLiteral("unzip")),
                    {QStringLiteral("-t"), archive});
        check.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
        INFO("unzip -t output: " << QString::fromUtf8(check.readAllStandardOutput()).toStdString());
        REQUIRE(check.exitCode() != 0);
    }

    DownloadExtractThread dt(QByteArray("file://") + archive.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    REQUIRE(outcome.finished);
    // Half-unpacking a corrupt archive and calling it done leaves a card that
    // looks written and will not boot.
    CHECK_FALSE(outcome.succeeded);
}

// ---------------------------------------------------------------------------
// A hostile multi-file archive
// ---------------------------------------------------------------------------
//
// Multi-file extraction writes entries onto the mounted boot partition using
// the paths the archive declares, and the archive is downloaded. An entry
// named "../../etc/cron.d/x", or an absolute path, is the classic Zip Slip:
// the extraction writes outside the target and onto the machine doing the
// imaging.
//
// libarchive defends against this, but only because the disk writer is given
// ARCHIVE_EXTRACT_SECURE_NODOTDOT, SECURE_NOABSOLUTEPATHS and
// SECURE_SYMLINKS. That is one line of flags, and dropping any of them would
// be silent -- ordinary archives would keep extracting perfectly. These pin
// it.

TEST_CASE("DownloadExtractThread will not write outside the target",
          "[extract][multifile][security]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no archive can be built");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;

    // A legitimate entry, so the extraction has something to do, and two
    // that try to climb out of the target.
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("config.txt")),
                      QByteArray("arm_64bit=1\n")));
    const QString escapee = scratch.filePath(QStringLiteral("escapee.txt"));
    REQUIRE(writeFile(escapee, QByteArray("should never be written outside\n")));

    const QString archive = scratch.filePath(QStringLiteral("hostile.zip"));
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-0"), archive,
                     QStringLiteral("config.txt")},
                    QFileInfo(archive).absolutePath()));
    // zip refuses to store "../" paths without help; -f rewrites the name.
    REQUIRE(runTool(QStringLiteral("zip"),
                    {QStringLiteral("-q"), QStringLiteral("-0"), QStringLiteral("--junk-paths"),
                     archive, escapee},
                    QFileInfo(archive).absolutePath()));

    // Rename the stored entry to a traversal path. Done with a python
    // rewrite because no archiver will produce one willingly.
    const QString hostile = scratch.filePath(QStringLiteral("traversal.zip"));
    const QString py = QStringLiteral(
        "import zipfile,sys\n"
        "src,dst=sys.argv[1],sys.argv[2]\n"
        "zi=zipfile.ZipFile(src)\n"
        "zo=zipfile.ZipFile(dst,'w',zipfile.ZIP_STORED)\n"
        "for n in zi.namelist():\n"
        "    data=zi.read(n)\n"
        "    out='config.txt' if n=='config.txt' else '../../../../tmp/rpi-imager-escaped.txt'\n"
        "    zo.writestr(out,data)\n"
        "zo.close()\n");
    REQUIRE(runTool(QStringLiteral("python3"),
                    {QStringLiteral("-c"), py, archive, hostile},
                    QFileInfo(archive).absolutePath()));

    const QString escapeTarget = QStringLiteral("/tmp/rpi-imager-escaped.txt");
    QFile::remove(escapeTarget);

    DownloadExtractThread dt(QByteArray("file://") + hostile.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    INFO("error: " << outcome.errorMessage.toStdString());

    // Whether the extraction as a whole succeeds or is refused is libarchive's
    // business. What must be true either way is that nothing was written
    // outside the mounted target.
    INFO("escape target exists: " << QFileInfo::exists(escapeTarget));
    CHECK_FALSE(QFileInfo::exists(escapeTarget));

    QFile::remove(escapeTarget);
}

TEST_CASE("DownloadExtractThread will not write to an absolute path",
          "[extract][multifile][security]")
{
    if (!canRunPrivileged())
        SKIP("passwordless sudo is unavailable, so no mounted device can be built");
    if (!haveMkfsVfat())
        SKIP("mkfs.vfat is not installed");
    if (!haveTool(QStringLiteral("zip")))
        SKIP("zip is not installed, so no archive can be built");

    MountedFatDevice device(48);
    if (!device.isReady())
        SKIP("the loop-backed FAT device could not be mounted");

    ScratchDir scratch;
    const QString hostile = scratch.filePath(QStringLiteral("absolute.zip"));
    const QString py = QStringLiteral(
        "import zipfile,sys\n"
        "zo=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_STORED)\n"
        "zo.writestr('config.txt', 'arm_64bit=1\\n')\n"
        "zo.writestr('/tmp/rpi-imager-absolute.txt', 'written by absolute path\\n')\n"
        "zo.close()\n");
    REQUIRE(runTool(QStringLiteral("python3"),
                    {QStringLiteral("-c"), py, hostile},
                    QFileInfo(hostile).absolutePath()));

    const QString escapeTarget = QStringLiteral("/tmp/rpi-imager-absolute.txt");
    QFile::remove(escapeTarget);

    DownloadExtractThread dt(QByteArray("file://") + hostile.toUtf8(),
                             device.device().toUtf8(), QByteArray());
    dt.setVerifyEnabled(false);
    dt.enableMultipleFileExtraction();

    const Outcome outcome = runToCompletion(dt, 240000);
    INFO("error: " << outcome.errorMessage.toStdString());
    INFO("escape target exists: " << QFileInfo::exists(escapeTarget));
    CHECK_FALSE(QFileInfo::exists(escapeTarget));

    QFile::remove(escapeTarget);
}
