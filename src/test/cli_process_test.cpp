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
// Three cases do pass --enable-writing-system-drives, because the checks
// they are about sit behind the destination check and that flag is what
// skips it. Their destination is still a scratch file rather than a device,
// so the promise above is unchanged.
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

// ── Past the destination check ────────────────────────────────────────
//
// The customisation-file refusals sit after the destination check, so
// reaching them means getting past it. --enable-writing-system-drives is
// what does that: it skips the check outright. The destination stays a plain
// file in a temporary directory -- there is no device involved at all -- so
// the promise the top of this file makes is unchanged. If one of these
// refusals failed to fire, the write would land on a scratch file.
//
// Worth reaching, because the message is the whole of the help a script
// author gets: there is no dialog to work out what went wrong from. The code
// tells "not there" apart from "cannot be opened" deliberately, and the two
// need different fixes -- a typo against a permissions or path-kind problem
// -- so they must not collapse into one message.

namespace {

// A directory passed where a file was meant: it exists, so the first check
// passes, and it cannot be opened, so the second one fires. Fails that way
// for root too, which a file with no permissions would not.
QString directoryInPlaceOfFile(const Scratch &scratch)
{
    const QString path = QDir(scratch.dir()).filePath(QStringLiteral("not-a-file"));
    return QDir().mkpath(path) ? path : QString();
}

} // namespace

TEST_CASE("A first-run script that is not there is named as the problem", "[cli][process][root]")
{
    if (::geteuid() != 0 && !haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--first-run-script"), scratch.missing(),
                             scratch.source(), scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("firstrun script does not exist"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A user-data file that cannot be opened is told apart from one that is absent",
          "[cli][process][root]")
{
    if (::geteuid() != 0 && !haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const QString unopenable = directoryInPlaceOfFile(scratch);
    REQUIRE_FALSE(unopenable.isEmpty());

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--cloudinit-userdata"), unopenable,
                             scratch.source(), scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("opening user-data file"));
    // Not the message for a file that is not there: the author would go
    // looking for a path that is perfectly correct.
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("does not exist"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("A network-config file that cannot be opened says which of the two it was",
          "[cli][process][root]")
{
    // The two cloud-init files are read one after the other by the same
    // helper, and a run can name both. Saying only "opening the file" would
    // leave the author to guess which.
    if (::geteuid() != 0 && !haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const QString unopenable = directoryInPlaceOfFile(scratch);
    REQUIRE_FALSE(unopenable.isEmpty());

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--cloudinit-networkconfig"), unopenable,
                             scratch.source(), scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("opening network-config file"));
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("user-data"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}

TEST_CASE("The refusal never offers a choice from an empty list", "[cli][process][root]")
{
    // With no removable drive attached -- the ordinary way to arrive here --
    // the refusal used to print "Choose one of the following:" and then
    // nothing at all, which reads as the message having broken rather than
    // as a fact about the machine.
    //
    // This holds whatever is plugged in: either there are candidates and they
    // are listed, or there are none and the message says so.
    if (::geteuid() != 0 && !haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const Run r = runImager({QStringLiteral("--cli"), scratch.source(),
                             scratch.notADevice()}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);

    const bool invited = r.output.contains(QStringLiteral("Choose one of the following"));
    if (invited)
    {
        // Something has to follow the invitation, and it has to look like a
        // device rather than a blank line.
        const QStringList lines = r.output.split(QLatin1Char('\n'));
        int at = -1;
        for (int i = 0; i < lines.size() && at < 0; ++i)
            if (lines[i].contains(QStringLiteral("Choose one of the following")))
                at = i;
        REQUIRE(at >= 0);

        bool sawCandidate = false;
        for (int i = at + 1; i < lines.size(); ++i)
        {
            if (lines[i].contains(QStringLiteral("--enable-writing-system-drives")))
                break;
            if (!lines[i].trimmed().isEmpty())
                sawCandidate = true;
        }
        CHECK(sawCandidate);
    }
    else
    {
        CHECK_THAT(r.output.toStdString(),
                   ContainsSubstring("no removable volume was found"));
    }

    // Either way, the way out is still named.
    CHECK_THAT(r.output.toStdString(),
               ContainsSubstring("--enable-writing-system-drives"));
    CHECK_FALSE(QFile::exists(scratch.notADevice()));
}
