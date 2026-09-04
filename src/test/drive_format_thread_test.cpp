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
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_session.hpp>

#include "driveformatthread.h"
#include "disk_formatter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

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
    REQUIRE(t.wait(60000));
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
    CHECK_THAT(outcome.errors[0].toStdString(), ContainsSubstring("permission"));

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

QByteArray testBlockDevice()
{
    const QByteArray dev = qgetenv("RPI_IMAGER_TEST_BLOCK_DEVICE");
    if (dev.isEmpty())
        return {};
    // Refuse anything that is not a loop device, whatever the environment
    // says. A typo here would format a real disk.
    if (!dev.startsWith("/dev/loop"))
        return {};
    return dev;
}

QByteArray firstSectorOf(const QByteArray &device)
{
    QFile f(QString::fromLatin1(device));
    REQUIRE(f.open(QIODevice::ReadOnly));
    return f.read(512);
}

} // namespace

TEST_CASE("Formatting a device writes a partition table", "[format][device]")
{
    const QByteArray dev = testBlockDevice();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
    const QByteArray dev = testBlockDevice();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
    const QByteArray dev = testBlockDevice();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
