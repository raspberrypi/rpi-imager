/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Reading the board's revision code.
 *
 * The embedded build detects which Raspberry Pi it is running on and uses
 * that to filter the OS list, so a board only sees images that will boot on
 * it. Get it wrong in one direction and the list is empty; get it wrong in
 * the other and the user is offered an image their board cannot run.
 */

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace {

struct Detected {
    bool ran = false;
    int exitCode = -1;
    QString out;

    QString value(const QString &key) const
    {
        for (const QString &line : out.split(QLatin1Char('\n')))
            if (line.startsWith(key + QLatin1Char('=')))
                return line.mid(key.size() + 1);
        return {};
    }
};

bool haveMountNamespaces()
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("true")});
    if (!p.waitForFinished(10000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// A /proc/cpuinfo carrying the given revision word, in the shape the real one
// has on a Pi.
QString cpuinfoWith(QTemporaryDir &dir, const QString &revision)
{
    const QString path = dir.filePath(QStringLiteral("cpuinfo"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("processor\t: 0\n");
    f.write("model name\t: ARMv8 Processor rev 1 (v8l)\n");
    if (!revision.isNull())
        f.write(QStringLiteral("Revision\t: %1\n").arg(revision).toUtf8());
    f.write("Serial\t\t: 100000001234abcd\n");
    f.close();
    return path;
}

// Ask the probe what it makes of that cpuinfo.
Detected detect(const QString &cpuinfoPath, const QString &deviceListJson = {})
{
    Detected d;
    QStringList args{QStringLiteral("-rm"), QStringLiteral("--propagation"),
                     QStringLiteral("private"), QStringLiteral("sh"),
                     QStringLiteral("-c"),
                     QStringLiteral("mount --bind \"$1\" /proc/cpuinfo "
                                    "&& shift && exec \"$@\""),
                     QStringLiteral("_"), cpuinfoPath,
                     QStringLiteral(DEVICE_INFO_PROBE_BINARY)};
    if (!deviceListJson.isEmpty())
        args << deviceListJson;

    QProcess p;
    p.start(QStringLiteral("unshare"), args);
    if (!p.waitForFinished(30000)) {
        p.kill();
        p.waitForFinished(5000);
        return d;
    }
    d.ran = true;
    d.exitCode = p.exitCode();
    d.out = QString::fromUtf8(p.readAllStandardOutput());
    return d;
}

#define REQUIRE_NAMESPACES()                                                   \
    if (!haveMountNamespaces())                                                \
        SKIP("unprivileged mount namespaces are unavailable, so /proc/cpuinfo "\
             "cannot be replaced")

} // namespace

TEST_CASE("A board's revision code names the model", "[deviceinfo]")
{
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Bits 4-11 of the revision word are the device type. c04170 is 0x17,
    // a Pi 5 model B.
    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("c04170")));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("1"));
    CHECK(d.value(QStringLiteral("NAME")) == QStringLiteral("Raspberry Pi 5"));
    // The raw code is kept as it was read, for the log and the bug report.
    CHECK(d.value(QStringLiteral("REVISION")) == QStringLiteral("c04170"));
}

TEST_CASE("A different model is named differently", "[deviceinfo]")
{
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // 0x11 is a Pi 4 model B. Reading the wrong nibble here is how a board
    // ends up offered a 64-bit-only image it cannot boot.
    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("a03111")));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("1"));
    CHECK(d.value(QStringLiteral("NAME")) == QStringLiteral("Raspberry Pi 4"));
}

TEST_CASE("A revision code from a board that does not exist yet is survivable",
          "[deviceinfo]")
{
    // The one that matters. The model table stops at the newest board known
    // when it was written, and the next Pi will carry a device type that is
    // not in it. So will any of the codes the table skips over.
    //
    // The imager has to come up and say it does not recognise the board. It
    // must not take the process down: on the embedded build this runs at
    // startup, so the failure is the kiosk never appearing at all -- on
    // exactly the new hardware someone has just plugged in.
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // 0x1f: past the end of the table.
    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("d041f0")));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.exitCode == 0);
    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("0"));
    CHECK(d.value(QStringLiteral("NAME")).isEmpty());
}

TEST_CASE("A gap in the middle of the model table is survivable too",
          "[deviceinfo]")
{
    // 0x16 sits between two boards that are in the table and is not itself
    // assigned. It reaches the same place as a future code, by a route
    // somebody reading the table would not expect.
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("c04160")));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.exitCode == 0);
    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("0"));
}

TEST_CASE("A revision line that is not a number is not a Pi", "[deviceinfo]")
{
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("not-a-revision")));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.exitCode == 0);
    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("0"));
    CHECK(d.value(QStringLiteral("NAME")).isEmpty());
}

TEST_CASE("A cpuinfo with no revision at all is not a Pi", "[deviceinfo]")
{
    // An x86 desktop, or a board whose kernel does not report one.
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const Detected d = detect(cpuinfoWith(dir, QString()));
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.exitCode == 0);
    CHECK(d.value(QStringLiteral("PI")) == QStringLiteral("0"));
}

TEST_CASE("The tags for the detected board are the ones taken", "[deviceinfo]")
{
    // The OS list carries a tag set per board name. Taking the wrong one
    // offers images for another Pi; taking none leaves the list empty.
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString devices = QStringLiteral(
        "[{\"name\":\"Raspberry Pi 4\",\"tags\":[\"pi4-64bit\"]},"
        " {\"name\":\"Raspberry Pi 5\",\"tags\":[\"pi5\",\"pi5-64bit\"]}]");

    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("c04170")), devices);
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    CHECK(d.value(QStringLiteral("TAGS_SET")) == QStringLiteral("1"));
    const QString tags = d.value(QStringLiteral("TAGS"));
    CHECK(tags.contains(QStringLiteral("pi5")));
    // Not the other board's.
    CHECK_FALSE(tags.contains(QStringLiteral("pi4")));
}

TEST_CASE("A board the list does not mention gets no tags", "[deviceinfo]")
{
    REQUIRE_NAMESPACES();
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString devices = QStringLiteral(
        "[{\"name\":\"Raspberry Pi 4\",\"tags\":[\"pi4-64bit\"]}]");

    const Detected d = detect(cpuinfoWith(dir, QStringLiteral("c04170")), devices);
    REQUIRE(d.ran);
    INFO(d.out.toStdString());

    // Nothing claimed, rather than the first entry taken as a default.
    CHECK(d.value(QStringLiteral("TAGS_SET")) == QStringLiteral("0"));
}
