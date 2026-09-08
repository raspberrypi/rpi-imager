// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// The command line as a script actually meets it.
//
// Cli::run() cannot be called from here: it builds its own QCoreApplication,
// and a test process already has one. That is why the decisions inside it
// were split into statics, and those are covered directly elsewhere. What
// stays out of reach that way is run() itself -- the order the checks happen
// in, and the text a script author sees when one of them fires -- which is
// most of the file and where two bugs were found this week.
//
// So the real binary is run as a subprocess. Every case here ends in a
// refusal that returns before any device is opened, and every destination is
// a path inside a temporary directory rather than anything under /dev, so a
// refusal that failed to fire would write to a scratch file and nothing else.
// --enable-writing-system-drives is never passed.
//
// Most of the checks sit behind the elevated-privileges test, so those cases
// need sudo and skip without it. The one that does not is the privileges
// refusal itself, which is the first thing anybody hits.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <unistd.h>

using Catch::Matchers::ContainsSubstring;

namespace {

constexpr int kCliTimeoutMs = 120000;

struct Run {
    int exitCode = -1;
    QString output;   // stdout and stderr together, which is what a terminal shows
    bool finished = false;
};

bool haveSudo()
{
    QProcess probe;
    probe.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    probe.waitForFinished(10000);
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

Run runImager(const QStringList &args, bool asRoot)
{
    Run r;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);

    if (asRoot) {
        QStringList sudoArgs{QStringLiteral("-n"), QStringLiteral(IMAGER_BINARY)};
        sudoArgs << args;
        p.start(QStringLiteral("sudo"), sudoArgs);
    } else {
        p.start(QStringLiteral(IMAGER_BINARY), args);
    }

    r.finished = p.waitForFinished(kCliTimeoutMs);
    r.output = QString::fromUtf8(p.readAll());
    r.exitCode = p.exitCode();
    return r;
}

// A source file that is really there, so the cases about *other* refusals get
// past the source check.
class Scratch
{
public:
    Scratch()
    {
        REQUIRE(_dir.isValid());
        _source = _dir.filePath(QStringLiteral("image.img"));
        QFile f(_source);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(64 * 1024, '\x5a'));
    }

    QString source() const { return _source; }
    // Never under /dev: a refusal that did not fire has to be harmless.
    QString notADevice() const { return _dir.filePath(QStringLiteral("not-a-device")); }
    QString missing() const { return _dir.filePath(QStringLiteral("not-here.img")); }
    QString dir() const { return _dir.path(); }

private:
    QTemporaryDir _dir;
    QString _source;
};

} // namespace

TEST_CASE("Run without privileges, and it says how to get them", "[cli][process]")
{
    // The first thing a script author meets. "Permission denied" from
    // somewhere deeper would leave them guessing; this has to name the
    // problem and the command that fixes it.
    if (::geteuid() == 0)
        SKIP("already root, so the check this is about does not fire");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"), scratch.source(),
                             scratch.notADevice()}, false);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Not running as root"));
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("sudo"));
    // Named, because on an AppImage the thing to type is not "rpi-imager".
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("--cli"));
    // And nothing was written to the destination that was named.
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A source that is not there is refused by name", "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available, and the checks below sit "
             "behind the privileges test");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"), scratch.missing(),
                             scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("source file does not exist"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A directory given as the image is refused", "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"), scratch.dir(),
                             scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("not a regular file"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A destination that is not a drive is refused, with the alternatives",
          "[cli][process][root]")
{
    // The one that stands between a mistyped script and somebody's disk. It
    // has to say what could have been written instead, and how to overrule it
    // for the person who really did mean a system drive.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"), scratch.source(),
                             scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(),
               ContainsSubstring("not in list of removable volumes"));
    CHECK_THAT(r.output.toStdString(),
               ContainsSubstring("--enable-writing-system-drives"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A cache file with no hash is refused before anything is fetched",
          "[cli][process][root]")
{
    // The static behind this is covered on its own; what this adds is that
    // run() actually consults it, and does so before the write rather than
    // after the download.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--cache-file"), scratch.missing(),
                             scratch.source(), scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("--cache-file requires --sha256"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A secure boot key that is not there is refused", "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--secure-boot-key"), scratch.missing(),
                             scratch.source(), scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("secure boot key file does not exist"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}
