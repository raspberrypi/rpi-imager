// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// MacFile is how the app gets a writable descriptor for a card it does not
// own, and it was wholly untested: authOpen() needs an authorisation prompt,
// so nothing in the file ran. openViaHelper() is the part that does the work
// once authorisation is granted, and authopen_stub speaks the same protocol,
// so all of it is reachable here bar the Authorization call itself.

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "mac/macfile.h"

using rpi_imager::mac::openViaHelper;

namespace {

// Open descriptors for this process. A leak on an error path shows up as a
// count that did not come back down, which is the failure mode the old
// inline version had on every path it took.
int openDescriptorCount()
{
    DIR *d = ::opendir("/dev/fd");
    if (!d)
        return -1;
    int n = 0;
    while (::readdir(d))
        ++n;
    ::closedir(d);
    return n;
}

QString stubPath()
{
    return QStringLiteral(AUTHOPEN_STUB_BINARY);
}

// A file with known contents, so a descriptor that arrives can be shown to
// address the file that was asked for rather than merely being a number.
QString makeFile(const QTemporaryDir &dir, const QByteArray &contents)
{
    const QString path = QDir(dir.path()).filePath(QStringLiteral("target.bin"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(contents) == contents.size());
    f.close();
    return path;
}

int runStub(const QString &mode, const QString &target, const QByteArray &stdinData = "auth")
{
    const QByteArray prog = stubPath().toLocal8Bit();
    const QByteArray m = mode.toLocal8Bit();
    const QByteArray t = target.toLocal8Bit();
    const char *argv[] = {prog.constData(), m.constData(), t.constData(), nullptr};
    return openViaHelper(prog.constData(), argv, stdinData);
}

}  // namespace

TEST_CASE("A descriptor passed back by the helper is usable", "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QByteArray contents = "the card's first block";
    const QString target = makeFile(dir, contents);

    const int fd = runStub(QStringLiteral("fd"), target);
    REQUIRE(fd >= 0);

    // The descriptor is the point: it has to address the file the helper was
    // asked for, and be writable, which is what the app does with it.
    QByteArray got(contents.size(), '\0');
    CHECK(::pread(fd, got.data(), contents.size(), 0) == contents.size());
    CHECK(got == contents);
    CHECK(::pwrite(fd, "X", 1, 0) == 1);
    ::close(fd);
}

TEST_CASE("A helper that sends no descriptor is refused", "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = makeFile(dir, "unused");

    // Data arrives but carries no SCM_RIGHTS. Returning the uninitialised -1
    // is right; treating the message as success is not.
    CHECK(runStub(QStringLiteral("nofd"), target) == -1);
}

TEST_CASE("A helper that exits non-zero is refused", "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = makeFile(dir, "unused");

    CHECK(runStub(QStringLiteral("fail"), target) == -1);
    CHECK(runStub(QStringLiteral("silent"), target) == -1);
}

TEST_CASE("A descriptor from a helper that then fails is closed, not returned",
          "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = makeFile(dir, "unused");

    // authopen sending a descriptor and then failing is the case that used to
    // leak: the old code returned early on the exit status with the received
    // descriptor still open, and never closed its own socket either.
    const int before = openDescriptorCount();
    REQUIRE(before > 0);
    for (int i = 0; i < 20; ++i)
        CHECK(runStub(QStringLiteral("fd-then-fail"), target) == -1);
    CHECK(openDescriptorCount() == before);
}

TEST_CASE("Repeated successful opens do not leak descriptors", "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = makeFile(dir, "unused");

    const int before = openDescriptorCount();
    REQUIRE(before > 0);
    for (int i = 0; i < 20; ++i) {
        const int fd = runStub(QStringLiteral("fd"), target);
        REQUIRE(fd >= 0);
        ::close(fd);
    }
    CHECK(openDescriptorCount() == before);
}

TEST_CASE("A helper that is not there is refused", "[macfile]")
{
    const char *missing = "/usr/libexec/rpi-imager-no-such-helper";
    const char *argv[] = {missing, nullptr};
    CHECK(openViaHelper(missing, argv, "auth") == -1);
}

TEST_CASE("MacFile does not report a raw device as sequential", "[macfile]")
{
    // Qt infers sequential from a size of zero, which is what /dev/rdisk
    // reports; a sequential QFile refuses the seeks the writer depends on.
    MacFile f;
    CHECK_FALSE(f.isSequential());
}

TEST_CASE("forceSync refuses a closed file and syncs an open one", "[macfile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("sync.bin"));

    MacFile f;
    CHECK_FALSE(f.forceSync());

    f.setFileName(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write("payload") == 7);
    CHECK(f.forceSync());

    // Flushed through to the file, not sitting in Qt's buffer.
    QFile check(path);
    REQUIRE(check.open(QIODevice::ReadOnly));
    CHECK(check.readAll() == QByteArray("payload"));
}
