/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * WinFile when the volume lock is granted and when the sync will not finish.
 *
 * Its own binary: the outcomes are forced by linker flags that apply to
 * everything linked with it. See winfile_fault.h.
 */

#include <catch2/catch_test_macros.hpp>

#include "windows/winfile.h"
#include "winfile_fault.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

namespace {

// A small file to point WinFile at. What it is matters less than that it
// opens; the controls under test are answered by the injector.
QString scratchFile(QTemporaryDir &dir)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("scratch.bin"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, '\x5a'));
    f.close();
    return path;
}

}  // namespace

TEST_CASE("A volume lock that is granted is taken and given back",
          "[winfile][fault]")
{
    // The other side of "locking a plain file is refused". Nothing had
    // exercised a lock being granted, so neither the state it records nor
    // the unlock that depends on it had run.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    WinFile f;
    f.setFileName(scratchFile(dir));
    REQUIRE(f.open(QIODevice::ReadWrite));

    rpi_test::grantVolumeLocks(true);
    CHECK(f.lockVolume());
    // And released. An unlock that did not run leaves a volume the OS will
    // not remount until the process exits.
    CHECK(f.unlockVolume());
    rpi_test::grantVolumeLocks(false);

    f.close();
}

TEST_CASE("Unlocking twice does not claim a second release", "[winfile][fault]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    WinFile f;
    f.setFileName(scratchFile(dir));
    REQUIRE(f.open(QIODevice::ReadWrite));

    rpi_test::grantVolumeLocks(true);
    REQUIRE(f.lockVolume());
    REQUIRE(f.unlockVolume());
    rpi_test::grantVolumeLocks(false);

    // Nothing is held any more, so this is a no-op rather than a release.
    CHECK_NOTHROW(f.unlockVolume());
    f.close();
}

TEST_CASE("A sync that cannot be completed is reported, not claimed",
          "[winfile][fault]")
{
    // The write path syncs before it calls a card written. A sync reported as
    // successful when the flush failed is the difference between telling
    // somebody their card is ready and telling them it is not.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    WinFile f;
    f.setFileName(scratchFile(dir));
    REQUIRE(f.open(QIODevice::ReadWrite));
    REQUIRE(f.write("hello", 5) == 5);

    // Succeeds when the device is there.
    CHECK(f.forceSync());

    rpi_test::failFlushes(true);
    CHECK_FALSE(f.forceSync());
    rpi_test::failFlushes(false);

    // And recovers once the device answers again, so the refusal is about
    // the flush rather than a state the file was left in.
    CHECK(f.forceSync());
    f.close();
}
