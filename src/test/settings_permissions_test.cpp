/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What secureSettingsFile() reports on Windows.
 *
 * The settings file holds whatever customisation somebody saved, which
 * includes a Wi-Fi passphrase and a hashed user password. Imager narrows it
 * at startup and warns when it could not. The warning is only worth anything
 * if it is absent the rest of the time, so these cases are mostly about the
 * ordinary installation being reported as fine.
 *
 * The POSIX side of the same function is covered in image_writer_test.cpp,
 * where there are mode bits to assert against.
 */

#include <catch2/catch_test_macros.hpp>

#include "settings_permissions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#ifdef _WIN32

namespace {

// Hand the file to Everyone, which is the exposure the warning exists for.
// icacls ships with Windows, but a machine policy can put it out of reach,
// so its absence skips rather than fails.
bool exposeToEveryone(const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("icacls"),
            {QDir::toNativeSeparators(path), QStringLiteral("/grant"),
             QStringLiteral("*S-1-1-0:(R)")});
    return p.waitForFinished(20000) && p.exitStatus() == QProcess::NormalExit &&
           p.exitCode() == 0;
}

} // namespace

TEST_CASE("An ordinary settings file is reported as secured",
          "[settingsperms][windows]")
{
    // A profile directory is already out of every other account's reach, so
    // the answer is yes without anything having to change. Reported as no,
    // Imager warns on every start about a file that was never exposed -- and
    // a warning that is always there is one nobody reads.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("nested/Imager.conf"));

    const auto result = rpi_imager::secureSettingsFile(path);

    CHECK(result.created);
    CHECK(QFileInfo::exists(path));
    CHECK(result.secured);
    CHECK(result.directorySecured);
    CHECK_FALSE(result.foreignOwner);
}

TEST_CASE("An existing settings file is reported as secured",
          "[settingsperms][windows]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));

    const QByteArray existing =
        "[imagecustomization]\nsshUserPassword=$y$jB5$notarealhash\n";
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(existing) == existing.size());
    }

    const auto result = rpi_imager::secureSettingsFile(path);

    CHECK_FALSE(result.created);
    CHECK(result.secured);

    // And what somebody saved is still there. Narrowing permissions must not
    // be a way to lose a customisation.
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll() == existing);
}

TEST_CASE("A settings file open to Everyone is reported as not secured",
          "[settingsperms][windows]")
{
    // The other half of the warning being worth reading. Imager cannot
    // repair this on Windows -- removing an inherited access entry would
    // mean writing a protected list, which it does not do -- so saying so is
    // the whole of what it has to offer.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    if (!exposeToEveryone(path))
        SKIP("icacls is not available to set up an exposed file");

    CHECK_FALSE(rpi_imager::secureSettingsFile(path).secured);
}

TEST_CASE("A settings file that could not be created is not called secured",
          "[settingsperms][windows]")
{
    // There is nothing at the path, and QFile::permissions answers nothing
    // for a file that is not there. Read as "no other access" that is a
    // pass, and the one case that most needs the warning gets none.
    if (QFileInfo::exists(QStringLiteral("Q:/")))
        SKIP("Q: exists on this machine, so it is not an unwritable path");

    const auto result = rpi_imager::secureSettingsFile(
        QStringLiteral("Q:/rpi-imager-no-such-volume/Imager.conf"));

    CHECK_FALSE(result.created);
    CHECK_FALSE(result.secured);
    CHECK_FALSE(result.directorySecured);
}

TEST_CASE("Handing the file to another account is not claimed on Windows",
          "[settingsperms][windows]")
{
    // The uid arguments are the Linux elevated-run handover. Windows has no
    // uid to hand anything to, and reporting the handover as done would have
    // the caller log that it happened.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    const auto result = rpi_imager::secureSettingsFile(path, 1000, 1000);

    CHECK_FALSE(result.reowned);
    CHECK(result.secured);
}

#endif // _WIN32

TEST_CASE("An empty settings path is refused rather than acted on",
          "[settingsperms]")
{
    // QSettings::fileName() is empty where no application name is set.
    const auto result = rpi_imager::secureSettingsFile(QString());
    CHECK_FALSE(result.created);
    CHECK_FALSE(result.tightened);
    CHECK_FALSE(result.secured);
    CHECK_FALSE(result.directorySecured);
}

TEST_CASE("Ownership is not restored without somebody to restore it to",
          "[settingsperms]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    // No invoking user known, which is every unelevated run.
    CHECK(rpi_imager::restoreUserOwnership(path, -1, -1) == 0);
    // And no path to act on.
    CHECK(rpi_imager::restoreUserOwnership(QString(), 1000, 1000) == 0);
}

TEST_CASE("Ownership of a path that is not there is not restored",
          "[settingsperms]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    CHECK(rpi_imager::restoreUserOwnership(tmp.filePath(QStringLiteral("absent")),
                                           1000, 1000) == 0);
}

TEST_CASE("Restoring ownership of a directory walks what is inside it",
          "[settingsperms]")
{
    // The cache directory an elevated run leaves behind: entries at more
    // than one level. Nothing here asserts a count -- on Windows there is no
    // ownership to hand over and the answer is nought -- but the walk itself
    // runs, over a tree with something in it.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("cache"));
    REQUIRE(QDir().mkpath(root + QStringLiteral("/deeper")));
    for (const QString &name : {QStringLiteral("cache/one.json"),
                                QStringLiteral("cache/deeper/two.json")}) {
        QFile f(tmp.filePath(name));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("{}");
    }

    CHECK(rpi_imager::restoreUserOwnership(root, 1000, 1000) >= 0);
}
