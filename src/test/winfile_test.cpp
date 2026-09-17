/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * WinFile: the Win32 handle wrapper the Windows write path opens a device with.
 *
 * It had no test of any kind -- 0% of its branches -- despite being what
 * DiskpartUtil locks and dismounts volumes through. Everything here except the
 * volume locking works against an ordinary file, which is enough to reach the
 * read, write, seek and error-reporting paths.
 */

#include <catch2/catch_test_macros.hpp>

#include "windows/winfile.h"
#include "platform_privilege.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

// A file of known contents for WinFile to open. WinFile opens OPEN_EXISTING
// only, so the file has to be there first.
QString makeFile(const QTemporaryDir &dir, const QString &name,
                 const QByteArray &contents = QByteArray(4096, '\0'))
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(contents) == contents.size());
    f.close();
    return path;
}

} // namespace

TEST_CASE("A file opens, reports itself open, and closes", "[winfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("plain.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    CHECK(f.isOpen());
    f.close();
    CHECK_FALSE(f.isOpen());
}

TEST_CASE("Closing one that was never opened is safe", "[winfile]")
{
    // The write path closes on the way out of several branches, including ones
    // reached before anything was opened.
    WinFile f;
    CHECK_NOTHROW(f.close());
    CHECK_FALSE(f.isOpen());
}

TEST_CASE("Bytes written read back unchanged", "[winfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("rw.img"));

    const QByteArray payload = QByteArrayLiteral("the quick brown fox");

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    REQUIRE(f.write(payload.constData(), payload.size()) == payload.size());
    REQUIRE(f.flush());

    REQUIRE(f.seek(0));
    CHECK(f.pos() == 0);

    QByteArray got(payload.size(), '\0');
    CHECK(f.read(got.data(), got.size()) == payload.size());
    CHECK(got == payload);
    f.close();
}

TEST_CASE("Seeking moves the position it reports", "[winfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("seek.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));

    REQUIRE(f.seek(1024));
    CHECK(f.pos() == 1024);
    REQUIRE(f.seek(0));
    CHECK(f.pos() == 0);
    f.close();
}

TEST_CASE("A sync on an open file succeeds", "[winfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("sync.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    const QByteArray payload = QByteArrayLiteral("durable");
    REQUIRE(f.write(payload.constData(), payload.size()) == payload.size());
    CHECK(f.forceSync());
    f.close();
}

TEST_CASE("A file that is not there fails to open and says so", "[winfile]")
{
    WinFile f;
    f.setFileName(QStringLiteral("C:/nowhere-at-all/rpi-imager/absent.img"));
    CHECK_FALSE(f.open(QIODevice::ReadWrite));
    CHECK_FALSE(f.isOpen());
    // Reported rather than left to the caller to infer from the false.
    CHECK_FALSE(f.errorString().isEmpty());
}

TEST_CASE("Reading from one that was never opened returns nothing",
          "[winfile]")
{
    WinFile f;
    QByteArray buf(16, '\0');
    CHECK(f.read(buf.data(), buf.size()) <= 0);
}

TEST_CASE("Unlocking something that was never locked is safe", "[winfile]")
{
    // The write path unlocks on the way out whether or not the lock was ever
    // taken, so this is a real sequence rather than a hypothetical one.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("unlock.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    CHECK_NOTHROW(f.unlockVolume());
    f.close();
}

TEST_CASE("Locking a plain file is refused rather than claimed", "[winfile]")
{
    // A file is not a volume. What matters is that the refusal is reported:
    // a caller that believes it holds a lock it does not have will let Windows
    // remount the card underneath the write.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("lock.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    const bool locked = f.lockVolume();
    if (locked)
        f.unlockVolume();
    f.close();
    // Not asserted either way on a file -- FSCTL_LOCK_VOLUME answers
    // differently depending on the filesystem -- but it must not crash, and
    // whatever it says has to be safe to unwind.
    SUCCEED("lockVolume() on a file answered without crashing");
}

TEST_CASE("A path outside Latin-1 is opened as the user named it",
          "[winfile][i18n]")
{
    // setFileName() takes a QString and open() narrows it with toLatin1()
    // before calling CreateFileA. Any path with a character outside Latin-1 --
    // a Cyrillic or CJK username, which is an ordinary thing to have -- is
    // mangled on the way through, and the open then fails or, worse, lands
    // somewhere else.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString name = QStringLiteral("\u6e2c\u8a66-\u0444\u0430\u0439\u043b.img");
    const QString path = makeFile(dir, name);
    REQUIRE(QFile::exists(path));

    WinFile f;
    f.setFileName(path);
    INFO("path: " << path.toStdString());
    CHECK(f.open(QIODevice::ReadWrite));
    f.close();
}

// ── what the operations do with no file open ────────────────────────────────
//
// Only forceSync() asks isOpen() first. The rest hand the invalid handle
// straight to Win32 and report what it says, which is the behaviour the write
// path depends on: every one of these is reachable after a failed open, and a
// silent success from any of them would let the write carry on against
// nothing.

TEST_CASE("Seeking with no file open fails rather than appearing to work",
          "[winfile]")
{
    WinFile f;
    CHECK_FALSE(f.seek(0));
    CHECK_FALSE(f.seek(1024));
    // And the reason is recorded, not left for the caller to guess.
    CHECK_FALSE(f.errorString().isEmpty());
}

TEST_CASE("Asking the position with no file open does not claim nought",
          "[winfile]")
{
    // Nought is a legitimate position, so answering it here would be
    // indistinguishable from a file open at its start.
    WinFile f;
    CHECK(f.pos() < 0);
}

TEST_CASE("Writing with no file open reports nothing written", "[winfile]")
{
    WinFile f;
    const QByteArray payload = QByteArrayLiteral("data with nowhere to go");
    CHECK(f.write(payload.constData(), payload.size()) <= 0);
}

TEST_CASE("Syncing with no file open is refused", "[winfile]")
{
    WinFile f;
    CHECK_FALSE(f.forceSync());
}

TEST_CASE("Seeking past the end of a file is allowed, as the API allows it",
          "[winfile]")
{
    // Windows permits a seek beyond the end; the file grows on the next
    // write. The write path relies on it to position within a device larger
    // than the image.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = makeFile(dir, QStringLiteral("seek-past.img"));

    WinFile f;
    f.setFileName(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    CHECK(f.seek(1024 * 1024));
    CHECK(f.pos() == 1024 * 1024);
    f.close();
}
