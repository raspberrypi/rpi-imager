/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * DriveFormatThread is the "Erase" entry in the OS list: it wipes a card and
 * lays down a fresh FAT32 filesystem. Two things matter to a user here and
 * neither had a test.
 *
 * The first is that it refuses when it cannot write to the device, instead of
 * starting a destructive operation it cannot finish -- a half-formatted card
 * is worse than one that was left alone.
 *
 * The second is the message shown when a format fails. It is the only thing
 * the user has to go on, and "insufficient permissions" and "out of space"
 * call for completely different responses.
 *
 * Nothing here touches a real device: the failure paths are reached with
 * ordinary files and paths that do not exist.
 */

#include <catch2/catch_test_macros.hpp>

#include "faulty_block_device.h"
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_session.hpp>

#include "driveformatthread.h"
#include "disk_formatter.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QProcess>
#include "fixture_process.h"

#ifdef Q_OS_MACOS
#include <CoreFoundation/CFRunLoop.h>
#endif

using Catch::Matchers::ContainsSubstring;

namespace {

// formatErrorToString() is protected; the thread is otherwise used as-is.
class TestableFormatThread : public DriveFormatThread
{
public:
    using DriveFormatThread::DriveFormatThread;
    using DriveFormatThread::formatErrorToString;
};

// Run the thread to completion and report what it emitted.
struct Outcome
{
    bool succeeded = false;
    QStringList errors;
};

Outcome runToCompletion(DriveFormatThread &t)
{
    Outcome out;
    QObject::connect(&t, &DriveFormatThread::success, [&out] { out.succeeded = true; });
    QObject::connect(&t, &DriveFormatThread::error,
                     [&out](QString m) { out.errors << m; });
    t.start();

    // Wait by pumping, not by parking. The thread can need the main one:
    // PlatformQuirks::unmountDisk on macOS hands the DiskArbitration work to
    // the main queue with dispatch_sync and blocks the worker until it comes
    // back, which is fine under an application that runs an event loop and a
    // deadlock under a test binary whose main thread is sitting in
    // QThread::wait(). It deadlocked twice over: the 60s wait expired, the
    // REQUIRE threw, and ~DriveFormatThread waited on the same thread with no
    // timeout at all, so the run stopped there rather than failing.
    //
    // processEvents() runs the main run loop, which is what drains the main
    // queue. Elsewhere it costs a few microseconds a turn and changes nothing.
    QDeadlineTimer deadline(60000);
    bool finished = false;
    while (!(finished = t.wait(50)) && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
#ifdef Q_OS_MACOS
        // processEvents() alone is not enough here. The main dispatch queue is
        // drained by the main run loop, and Qt's dispatcher for a
        // QCoreApplication on macOS is the UNIX one, which never runs a
        // CFRunLoop. Run it directly, briefly, so the block the worker is
        // waiting on actually executes.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, true);
#endif
    }
    REQUIRE(finished);
    return out;
}

} // namespace

// ══════════════════════════════════════════════════════════════
// Refusing rather than half-formatting
// ══════════════════════════════════════════════════════════════

TEST_CASE("Formatting a device that does not exist is refused", "[format]")
{
    TestableFormatThread t("/dev/definitely-not-a-device-9e3f1a");
    const Outcome outcome = runToCompletion(t);

    CHECK_FALSE(outcome.succeeded);
    REQUIRE(outcome.errors.size() == 1);
    CHECK_FALSE(outcome.errors[0].isEmpty());
}

TEST_CASE("Formatting a path the user cannot write is refused", "[format]")
{
    // The common case on Linux: the card is there, but the app was not
    // started with the rights to write to it. The user needs to be told
    // that, not left with a partially wiped card.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("readonly.img"));

    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(1024, '\0'));
    f.close();
    REQUIRE(f.setPermissions(QFileDevice::ReadOwner));

    TestableFormatThread t(path.toUtf8());
    const Outcome outcome = runToCompletion(t);

    CHECK_FALSE(outcome.succeeded);
    REQUIRE(outcome.errors.size() == 1);
#ifdef Q_OS_MACOS
    // macOS never gets as far as the permission check. The format begins by
    // unmounting, and DiskArbitration will not unmount something that is not
    // a disk, so the refusal arrives one step earlier and says so. What the
    // case is really about -- refused, told why, card untouched -- still
    // holds; only the wording is out of reach here.
    CHECK_THAT(outcome.errors[0].toStdString(), ContainsSubstring("unmount"));
#else
    CHECK_THAT(outcome.errors[0].toStdString(), ContainsSubstring("permission"));
#endif

    // Nothing was written to it.
    QFile check(path);
    REQUIRE(check.open(QIODevice::ReadOnly));
    CHECK(check.readAll() == QByteArray(1024, '\0'));
}

TEST_CASE("Formatting an empty device path is refused", "[format]")
{
    TestableFormatThread t(QByteArray{});
    const Outcome outcome = runToCompletion(t);

    CHECK_FALSE(outcome.succeeded);
    CHECK_FALSE(outcome.errors.isEmpty());
}

TEST_CASE("A device too small to format says so, through the thread", "[format]")
{
    // The mapping from failure to message is checked directly above; this is
    // the path a user takes to it. Everything else that reaches the thread is
    // refused before the formatter runs -- a missing device, an unwritable
    // one -- so the one line that turns a formatter failure into something on
    // screen had never been reached.
    //
    // Writable and far too small: opening it works and the size query falls
    // back to an ioctl answer that means nothing for a regular file, so the
    // formatter gets as far as writing and runs off the end.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("tiny.img"));

    const QByteArray before(64 * 1024, '\0');
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(before) == before.size());
    }

    TestableFormatThread t(path.toUtf8());
    const Outcome outcome = runToCompletion(t);

    CHECK_FALSE(outcome.succeeded);
    REQUIRE(outcome.errors.size() == 1);
    INFO("said: " << outcome.errors[0].toStdString());
    // The formatter's own reason, carried through rather than collapsed into
    // "Unknown formatting error" -- running out of room and being refused
    // permission call for different things from the person holding the card.
    CHECK_THAT(outcome.errors[0].toStdString(), ContainsSubstring("Insufficient space"));

    // And it was left as it was. A refusal that had already written part of a
    // filesystem would be worse than not trying.
    QFile check(path);
    REQUIRE(check.open(QIODevice::ReadOnly));
    CHECK(check.readAll() == before);
}

// ══════════════════════════════════════════════════════════════
// What the user is told when it fails
// ══════════════════════════════════════════════════════════════

TEST_CASE("Every format failure has its own message", "[format]")
{
    using rpi_imager::FormatError;
    TestableFormatThread t("/dev/null");

    const QList<FormatError> all = {
        FormatError::kFileOpenError,
        FormatError::kFileWriteError,
        FormatError::kFileSeekError,
        FormatError::kInvalidParameters,
        FormatError::kInsufficientSpace,
        FormatError::kCancelled,
    };

    QStringList seen;
    for (FormatError e : all) {
        const QString msg = t.formatErrorToString(e);
        INFO("error " << static_cast<int>(e) << ": " << msg.toStdString());
        // A blank message leaves a dialog with nothing in it.
        CHECK_FALSE(msg.isEmpty());
        seen << msg;
    }

    // Distinct messages: two different failures that read the same way send
    // the user looking in the wrong place.
    QStringList unique = seen;
    unique.removeDuplicates();
    CHECK(unique.size() == seen.size());
}

TEST_CASE("A failure with no message of its own still says something", "[format]")
{
    // The switch deliberately has no `default:` arm so -Wswitch flags a new
    // FormatError nobody has written a message for. The fallback after it is
    // the other half of that promise: a value the switch does not recognise
    // must still produce a sentence, because the alternative is an empty
    // error dialog on a card that was not formatted.
    //
    // Reached the only way it can be, with a value from outside the enum --
    // which is what a stale library or a mismatched build would hand over.
    TestableFormatThread t("/dev/null");

    const QString said =
        t.formatErrorToString(static_cast<rpi_imager::FormatError>(9999));

    INFO(said.toStdString());
    CHECK_FALSE(said.isEmpty());
    CHECK_THAT(said.toStdString(), ContainsSubstring("formatting"));
}

TEST_CASE("Running out of space says so", "[format]")
{
    using rpi_imager::FormatError;
    TestableFormatThread t("/dev/null");
    CHECK_THAT(t.formatErrorToString(FormatError::kInsufficientSpace).toStdString(),
               ContainsSubstring("space"));
}

TEST_CASE("A cancelled format is not reported as a fault", "[format]")
{
    // The user chose to stop. Presenting that as an error is alarming and
    // suggests the card is damaged when it is not.
    using rpi_imager::FormatError;
    TestableFormatThread t("/dev/null");
    const QString msg = t.formatErrorToString(FormatError::kCancelled);
    INFO("message: " << msg.toStdString());
    CHECK_THAT(msg.toStdString(), ContainsSubstring("ancel"));
}

int main(int argc, char *argv[])
{
    int argcCopy = argc;
    QCoreApplication app(argcCopy, argv);
    return Catch::Session().run(argc, argv);
}

// ═══════════════════════════════════════════════════════════════════════════
// Actually formatting something
//
// Everything above is a refusal. None of it reaches DiskFormatter, the
// unmount, or the partition table that "Erase" exists to write -- and a
// format that reports success while leaving the card unreadable is exactly
// the failure a user cannot diagnose.
//
// These need a real block device. They run against whatever
// RPI_IMAGER_TEST_BLOCK_DEVICE points at, which is expected to be a loop
// device backed by a file; without it they skip rather than fail, and they
// refuse to touch anything that is not a loop device.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

QByteArray firstSectorOf(const QByteArray &device)
{
    QFile f(QString::fromLatin1(device));
    REQUIRE(f.open(QIODevice::ReadOnly));
    return f.read(512);
}

} // namespace

TEST_CASE("Formatting a device writes a partition table", "[format][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QByteArray dev = device.path().toLatin1();

    DriveFormatThread t(dev);
    const Outcome out = runToCompletion(t);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.errors.isEmpty());
    REQUIRE(out.succeeded);

    const QByteArray mbr = firstSectorOf(dev);
    REQUIRE(mbr.size() == 512);

    // Without the boot signature nothing will recognise the card as
    // partitioned, and the Pi will not boot from it.
    CHECK(quint8(mbr.at(510)) == 0x55);
    CHECK(quint8(mbr.at(511)) == 0xAA);

    // First partition entry: a FAT32 type, and not empty.
    const quint8 partitionType = quint8(mbr.at(0x1BE + 4));
    INFO("partition type byte: 0x" << QString::number(partitionType, 16).toStdString());
    CHECK((partitionType == 0x0B || partitionType == 0x0C));
}

TEST_CASE("Formatting the same device twice is fine", "[format][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QByteArray dev = device.path().toLatin1();

    // Erasing a card that was already erased is ordinary, and must not trip
    // over the table left by the previous run.
    DriveFormatThread first(dev);
    REQUIRE(runToCompletion(first).succeeded);

    DriveFormatThread second(dev);
    const Outcome out = runToCompletion(second);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.succeeded);
}

TEST_CASE("A formatted device reports its size", "[format][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QByteArray dev = device.path().toLatin1();

    DriveFormatThread t(dev);
    REQUIRE(runToCompletion(t).succeeded);

    // The partition must span something close to the device, not a token
    // few sectors -- a card that formats to 1 MB is useless and looks fine.
    const QByteArray mbr = firstSectorOf(dev);
    const quint32 sectors = quint32(quint8(mbr.at(0x1BE + 12)))
                          | (quint32(quint8(mbr.at(0x1BE + 13))) << 8)
                          | (quint32(quint8(mbr.at(0x1BE + 14))) << 16)
                          | (quint32(quint8(mbr.at(0x1BE + 15))) << 24);
    INFO("partition sectors: " << sectors);
    CHECK(sectors > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// FAT32 geometry across card sizes
//
// DiskFormatter picks a cluster size from the card's capacity -- five bands,
// from 512 bytes on a tiny card up to 16 KB above 16 GB. Everything else in
// the boot sector is derived from that choice, so getting the band wrong
// produces a filesystem that is valid-looking and wrong: the FAT is sized for
// a different cluster count, and what reads it disagrees about where files
// are.
//
// The formatter writes through the ordinary file interface, so this needs no
// block device and no privileges. The files are sparse; only the tables near
// the start are actually written.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

quint32 readLe32(const QByteArray &b, int off)
{
    return quint32(quint8(b.at(off)))
         | (quint32(quint8(b.at(off + 1))) << 8)
         | (quint32(quint8(b.at(off + 2))) << 16)
         | (quint32(quint8(b.at(off + 3))) << 24);
}

quint16 readLe16(const QByteArray &b, int off)
{
    return quint16(quint8(b.at(off))) | (quint16(quint8(b.at(off + 1))) << 8);
}

// Formats a sparse file of the given size and returns its boot sector.
struct Formatted
{
    bool ok = false;
    QByteArray mbr;
    QByteArray bootSector;
    quint32 partitionStartLba = 0;
};

Formatted formatSizedImage(const QString &path, quint64 bytes)
{
    Formatted out;
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.resize(qint64(bytes)));   // sparse
        f.close();
    }

    rpi_imager::DiskFormatter formatter;
    const auto result = formatter.FormatDrive(path.toStdString());
    if (!result.has_value())
        return out;

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    out.mbr = f.read(512);
    REQUIRE(out.mbr.size() == 512);

    out.partitionStartLba = readLe32(out.mbr, 0x1BE + 8);
    REQUIRE(out.partitionStartLba > 0);
    REQUIRE(f.seek(qint64(out.partitionStartLba) * 512));
    out.bootSector = f.read(512);
    REQUIRE(out.bootSector.size() == 512);

    out.ok = true;
    return out;
}

// Copies the partition out of a formatted image and asks fsck.vfat what it
// makes of it. An independent implementation is a far better oracle than
// re-reading the fields we just wrote.
bool haveFsck()
{
    return QFileInfo::exists(QStringLiteral("/usr/sbin/fsck.vfat"))
        || QFileInfo::exists(QStringLiteral("/sbin/fsck.vfat"));
}

QString fsckPath()
{
    return QFileInfo::exists(QStringLiteral("/usr/sbin/fsck.vfat"))
        ? QStringLiteral("/usr/sbin/fsck.vfat")
        : QStringLiteral("/sbin/fsck.vfat");
}

bool partitionPassesFsck(const QString &image, quint32 startLba, QString *output)
{
    const QString extracted = image + QStringLiteral(".part");
    {
        QFile in(image);
        if (!in.open(QIODevice::ReadOnly))
            return false;
        if (!in.seek(qint64(startLba) * 512))
            return false;
        QFile out(extracted);
        if (!out.open(QIODevice::WriteOnly))
            return false;
        while (!in.atEnd()) {
            const QByteArray chunk = in.read(4 * 1024 * 1024);
            if (chunk.isEmpty())
                break;
            out.write(chunk);
        }
    }

    QProcess proc;
    proc.start(fsckPath(), {QStringLiteral("-n"), extracted});
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    if (output)
        *output = QString::fromUtf8(proc.readAllStandardOutput()
                                    + proc.readAllStandardError());
    const bool clean = (proc.exitStatus() == QProcess::NormalExit) && (proc.exitCode() == 0);
    QFile::remove(extracted);
    return clean;
}

} // namespace

TEST_CASE("Cluster size follows the card's capacity", "[format][geometry]")
{
    struct Band
    {
        const char *name;
        quint64 bytes;
        quint8 expectedSectorsPerCluster;
    };

    // One size inside each band the formatter distinguishes.
    //
    // The smallest band used to be sampled at 16 MB, which the formatter now
    // refuses: a partition that small cannot hold the 65,525 clusters FAT32
    // requires at any cluster size, so what it wrote there was never a valid
    // FAT32. 40 MB is the smallest of these bands that can be.
    const Band bands[] = {
        {"40 MB",  40ull  * 1024 * 1024,        1},
        {"100 MB", 100ull * 1024 * 1024,        2},
        {"1 GB",   1024ull * 1024 * 1024,       8},
        {"10 GB",  10ull * 1024 * 1024 * 1024, 16},
        {"20 GB",  20ull * 1024 * 1024 * 1024, 32},
    };

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    for (const Band &band : bands) {
        INFO("band: " << band.name);
        const QString path = QDir(dir.path()).filePath(
            QStringLiteral("card-%1.img").arg(QString::fromLatin1(band.name).remove(' ')));

        const Formatted f = formatSizedImage(path, band.bytes);
        REQUIRE(f.ok);

        // A partition table anything will recognise.
        CHECK(quint8(f.mbr.at(510)) == 0x55);
        CHECK(quint8(f.mbr.at(511)) == 0xAA);
        const quint8 type = quint8(f.mbr.at(0x1BE + 4));
        CHECK((type == 0x0B || type == 0x0C));

        // BPB_SecPerClus, the byte every other size in the boot sector is
        // derived from.
        CHECK(quint8(f.bootSector.at(13)) == band.expectedSectorsPerCluster);

        // And the boot sector is a boot sector: 512-byte sectors, two FATs,
        // and its own signature.
        CHECK(readLe16(f.bootSector, 11) == 512);
        CHECK(quint8(f.bootSector.at(16)) == 2);
        CHECK(quint8(f.bootSector.at(510)) == 0x55);
        CHECK(quint8(f.bootSector.at(511)) == 0xAA);

        // Independent confirmation on the sizes cheap enough to copy out.
        if (haveFsck() && band.bytes <= 100ull * 1024 * 1024) {
            QString fsckOutput;
            const bool clean = partitionPassesFsck(path, f.partitionStartLba, &fsckOutput);
            INFO("fsck said: " << fsckOutput.toStdString());
            CHECK(clean);
        }

        QFile::remove(path);
    }
}

TEST_CASE("The FSInfo sector is written and signed", "[format][geometry]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("fsinfo.img"));

    const Formatted f = formatSizedImage(path, 512ull * 1024 * 1024);
    REQUIRE(f.ok);

    // FSInfo lives at the sector the boot sector nominates. Drivers use its
    // free-cluster hint; a missing or unsigned one makes them rescan the
    // whole FAT on mount.
    const quint16 fsInfoSector = readLe16(f.bootSector, 48);
    CHECK(fsInfoSector == 1);

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    REQUIRE(file.seek((qint64(f.partitionStartLba) + fsInfoSector) * 512));
    const QByteArray fsInfo = file.read(512);
    REQUIRE(fsInfo.size() == 512);

    CHECK(readLe32(fsInfo, 0) == 0x41615252);     // lead signature
    CHECK(readLe32(fsInfo, 484) == 0x61417272);   // struct signature
    CHECK(readLe32(fsInfo, 508) == 0xAA550000);   // trail signature
}

TEST_CASE("A device too small to hold a filesystem is refused", "[format][geometry]")
{
    // FAT32 needs 65,525 clusters before it is FAT32 at all, and below about
    // 37 MB no cluster size reaches that. Reporting success and leaving a
    // card carrying something that claims FAT32 and is not one is the worse
    // of the two failures, so these are refused rather than written.
    //
    // This case used to accept either answer and check with fsck if the
    // format went ahead, because the formatter would happily write a volume
    // of 1,984 clusters. fsck.fat does not enforce the minimum either, so it
    // passed.
    const quint64 sizes[] = {
        64ull * 1024,               // far below even the partition offset
        4ull * 1024 * 1024,         // below the 4 MB offset the partition starts at
        16ull * 1024 * 1024,        // room for a partition, too few clusters
        36ull * 1024 * 1024,        // just under the smallest valid FAT32
    };

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    for (const quint64 bytes : sizes) {
        INFO("device size: " << bytes << " bytes");
        const QString path = QDir(dir.path()).filePath(
            QStringLiteral("tiny-%1.img").arg(bytes));

        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.resize(qint64(bytes)));
        f.close();

        rpi_imager::DiskFormatter formatter;
        const auto result = formatter.FormatDrive(path.toStdString());

        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == rpi_imager::FormatError::kInsufficientSpace);

        // And it said so before touching the device, so a card the user
        // plugged in by mistake comes back the way it went in.
        QFile after(path);
        REQUIRE(after.open(QIODevice::ReadOnly));
        const QByteArray head = after.read(512);
        after.close();
        CHECK(head == QByteArray(head.size(), '\0'));

        QFile::remove(path);
    }
}

TEST_CASE("The smallest accepted card still holds a valid FAT32", "[format][geometry]")
{
    // The other side of the refusal above: just over the boundary the format
    // goes ahead, and what it writes has to be a real FAT32 -- enough
    // clusters, and a FAT long enough to hold an entry for each of them.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("smallest.img"));

    const Formatted f = formatSizedImage(path, 40ull * 1024 * 1024);
    REQUIRE(f.ok);

    const quint32 totalSectors = readLe32(f.bootSector, 32);
    const quint32 sectorsPerFat = readLe32(f.bootSector, 36);
    const quint8 sectorsPerCluster = quint8(f.bootSector.at(13));
    const quint8 numFats = quint8(f.bootSector.at(16));
    const quint16 reserved = readLe16(f.bootSector, 14);

    REQUIRE(sectorsPerCluster > 0);
    REQUIRE(numFats > 0);
    const quint64 overhead = reserved + quint64(numFats) * sectorsPerFat;
    REQUIRE(overhead < totalSectors);

    // The cluster count a driver derives from those fields.
    const quint64 clusters = (totalSectors - overhead) / sectorsPerCluster;
    CHECK(clusters >= 65525);

    // And a FAT with an entry for every one of them, plus the two reserved.
    CHECK(quint64(sectorsPerFat) * 512 / 4 >= clusters + 2);

    if (haveFsck()) {
        QString fsckOutput;
        const bool clean = partitionPassesFsck(path, f.partitionStartLba, &fsckOutput);
        INFO("fsck said: " << fsckOutput.toStdString());
        CHECK(clean);
    }
}
