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
#include <QSignalSpy>
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
