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
#include <QCryptographicHash>
#include <QSet>
#include <QThread>
#include <chrono>
#include <QDir>

#include "platform_tools.h"
#include <QElapsedTimer>
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
#ifdef Q_OS_MACOS
    // PlatformQuirks::hasElevatedPrivileges() answers true unconditionally on
    // macOS -- the comment there says the permissions model makes the check
    // unnecessary -- so the CLI never reaches the message this case is about.
    SKIP("the CLI does not ask for root on macOS");
#endif

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

// ══════════════════════════════════════════════════════════════
// A write that finishes, and a process that then exits.
//
// Every other case here ends in a refusal, which is deliberate -- a refusal
// returns before a device is opened. But it left the whole success path
// untested: the progress line, "Write successful.", the exit code a script
// reads, and, as it turned out, whether the process comes back at all.
//
// It did not. The suspend inhibitor holds its lock by running `cat` on a
// FIFO and releases it by closing the write end. Where the write finishes
// before that `cat` reaches its open(), the open blocks for a writer that
// has already gone, and the unbounded wait in the inhibitor's teardown waits
// on it for ever. A small image, or a fast target, loses that race. The user
// sees "Write successful." and a prompt that never returns.
//
// The target here is a regular file in a temporary directory, never a
// device, so a guard that failed to fire cannot damage anything.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A write that succeeds says so and gives the shell back",
          "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available, and writing needs root");

    Scratch scratch;
    const QString target = scratch.notADevice();
    {
        QFile f(target);
        REQUIRE(f.open(QIODevice::WriteOnly));
    }

    QElapsedTimer elapsed;
    elapsed.start();
    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             scratch.source(), target}, true);
    const qint64 took = elapsed.elapsed();

    INFO("took " << took << "ms\n" << r.output.toStdString());

    // The regression this exists for. runImager waits two minutes before
    // giving up; the hang used every second of it.
    REQUIRE(r.finished);
    CHECK(took < 60000);

    CHECK(r.exitCode == 0);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Write successful."));
    // The progress line a script's user watches, which nothing reached
    // before either.
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Writing:"));
}

TEST_CASE("A completed write leaves no inhibitor behind",
          "[cli][process][root]")
{
    // The other half of the same fault. The inhibitor is a `systemd-inhibit`
    // process holding an idle:sleep lock and a FIFO under /run; a run that
    // hangs leaves both, and they accumulate. A machine that has written a
    // few cards should not be one that can no longer go to sleep.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");
#ifndef Q_OS_LINUX
    // The FIFO under /run and the systemd-inhibit holding it are the Linux
    // inhibitor's own arrangement; other platforms inhibit sleep through
    // their own APIs and leave nothing on disk to count.
    SKIP("the inhibitor this counts is the Linux one");
#endif

    // /run is shared and ctest runs these cases in parallel, so a sibling
    // case's write can have a FIFO open while this one looks. Comparing a
    // count would then fail on somebody else's work in progress -- it did,
    // once, in a -j4 run. So: name what was there before, and afterwards
    // wait for everything that is new to go away. A sibling's FIFO is new
    // too, and disappears when its run finishes; only a genuine leak stays.
    const auto fifoNames = []() {
        return QDir(QStringLiteral("/run"))
            .entryList({QStringLiteral("rpi-imager-suspend_*")},
                       QDir::System | QDir::Files);
    };
    // One call, held in a named list: taking begin() from one temporary and
    // end() from another is two different containers and a crash.
    const QStringList existing = fifoNames();
    const QSet<QString> before(existing.begin(), existing.end());

    Scratch scratch;
    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             scratch.source(), target}, true);
    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    REQUIRE(r.exitCode == 0);

    QStringList stillNew;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        stillNew.clear();
        for (const QString &name : fifoNames()) {
            if (!before.contains(name))
                stillNew << name;
        }
        if (stillNew.isEmpty() || std::chrono::steady_clock::now() >= deadline)
            break;
        QThread::msleep(200);
    }

    INFO("left behind: " << stillNew.join(QStringLiteral(", ")).toStdString());
    CHECK(stillNew.isEmpty());
}

namespace {
// Big enough to clear the progress throttle, which suppresses any output at
// all for a write that finishes in a couple of chunks. 16 MB is not enough;
// 48 MB is.
QString sizedImage(const QString &dir, int megabytes)
{
    const QString path = QDir(dir).filePath(QStringLiteral("sized.img"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    QByteArray chunk(1024 * 1024, '\0');
    for (int i = 0; i < chunk.size(); ++i)
        chunk[i] = static_cast<char>((i * 7 + 13) & 0xFF);
    for (int i = 0; i < megabytes; ++i)
        REQUIRE(f.write(chunk) == chunk.size());
    f.close();
    return path;
}
} // namespace

TEST_CASE("A verified write says that it verified", "[cli][process][root]")
{
    // Verification is on unless it is turned off, and it is the only reason
    // to believe the card holds what the image held. Somebody watching a
    // scripted run needs to see it happen -- a run that only ever says
    // "Writing" has not told them whether it was checked.
    if (!haveSudo())
        SKIP("passwordless sudo is not available, and writing needs root");

    Scratch scratch;
    const QString source = sizedImage(scratch.dir(), 48);
    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             source, target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 0);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Verifying"));
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Write successful."));
}

TEST_CASE("Asking not to verify means it does not", "[cli][process][root]")
{
    // The flag exists because verification doubles the time. One that
    // silently verified anyway would waste that time; one that silently
    // skipped when not asked would be worse.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const QString source = sizedImage(scratch.dir(), 48);
    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             source, target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 0);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Write successful."));
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("Verifying"));
}

TEST_CASE("Quiet means quiet, right up until something fails",
          "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;

    SECTION("a successful quiet run says nothing at all")
    {
        const QString target = scratch.notADevice();
        { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

        const Run r = runImager({QStringLiteral("--cli"), QStringLiteral("--quiet"),
                                 QStringLiteral("--enable-writing-system-drives"),
                                 QStringLiteral("--disable-verify"),
                                 scratch.source(), target}, true);

        INFO(r.output.toStdString());
        REQUIRE(r.finished);
        CHECK(r.exitCode == 0);
        CHECK_THAT(r.output.toStdString(), !ContainsSubstring("Writing:"));
        CHECK_THAT(r.output.toStdString(), !ContainsSubstring("Write successful."));
    }

    SECTION("a failure is reported even so")
    {
        // The one thing quiet must not swallow. A script whose write failed
        // and printed nothing leaves whoever runs it with an exit code and
        // no idea which of a dozen things went wrong.
        const QString unreachable =
            QDir(scratch.dir()).filePath(QStringLiteral("no-such-dir/target.img"));

        const Run r = runImager({QStringLiteral("--cli"), QStringLiteral("--quiet"),
                                 QStringLiteral("--enable-writing-system-drives"),
                                 QStringLiteral("--disable-verify"),
                                 scratch.source(), unreachable}, true);

        INFO(r.output.toStdString());
        REQUIRE(r.finished);
        CHECK(r.exitCode == 1);
        CHECK_THAT(r.output.toStdString(), ContainsSubstring("Error:"));
    }
}

TEST_CASE("Running as root, a failed open does not advise sudo",
          "[cli][process][root]")
{
    // Every open failure on Linux used to end with "Please run with elevated
    // privileges (sudo)", whatever had actually gone wrong. Somebody already
    // running under sudo -- which the CLI requires, so all of them -- was
    // told to do the thing they had just done, and sent round the same loop
    // with nothing to change.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");
#ifndef Q_OS_LINUX
    // The message this is about is the Linux branch of the open failure.
    // macOS has its own, which also opens a System Settings pane on the way
    // past -- not something a test run should do to somebody's desktop.
    SKIP("this message is the Linux one");
#endif

    Scratch scratch;
    const QString unreachable =
        QDir(scratch.dir()).filePath(QStringLiteral("no-such-dir/target.img"));

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             scratch.source(), unreachable}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);

    // Named, so the reader knows which path failed.
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Cannot open storage device"));
    // And told what actually happened, rather than advice they cannot act on.
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("No such file or directory"));
    // Not the advice itself. Matching on "sudo" alone would catch
    // applyQuirks logging "Running as root via sudo", which is a different
    // statement entirely and a true one.
    CHECK_THAT(r.output.toStdString(),
               !ContainsSubstring("Please run with elevated privileges"));
}

// ══════════════════════════════════════════════════════════════
// --sha256, and what a corrupt image looks like on a terminal.
//
// The hash is the user's only protection against writing an image that
// arrived damaged. Only its interaction with --cache-file was covered; that
// it is actually checked, and what it says when the check fails, was not.
//
// What it said was written for the GUI, whose dialogs render rich text. On
// a terminal the <br> separating the two hashes printed literally, in the
// middle of the one message where the reader needs to compare two long hex
// strings.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An image whose hash does not match is refused, legibly",
          "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available, and writing needs root");

    Scratch scratch;
    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    // A hash of something else entirely.
    const QByteArray wrong =
        QCryptographicHash::hash(QByteArrayLiteral("not this image"),
                                 QCryptographicHash::Sha256).toHex();

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--sha256"), QString::fromUtf8(wrong),
                             scratch.source(), target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("incorrect SHA256 hash"));

    // Both hashes, so the reader can see which one they got.
    CHECK_THAT(r.output.toStdString(), ContainsSubstring(wrong.toStdString()));

    // And no markup. This message carries <br> for the GUI's benefit; a
    // terminal shows that literally, right where two long hex strings have
    // to be compared by eye.
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("<br"));
}

TEST_CASE("An image whose hash matches is written", "[cli][process][root]")
{
    // The other side, so the refusal above is not simply "--sha256 always
    // fails".
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    QFile src(scratch.source());
    REQUIRE(src.open(QIODevice::ReadOnly));
    const QByteArray correct =
        QCryptographicHash::hash(src.readAll(), QCryptographicHash::Sha256).toHex();
    src.close();

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--sha256"), QString::fromUtf8(correct),
                             scratch.source(), target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 0);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Write successful."));
}

// ══════════════════════════════════════════════════════════════
// A compressed image, which is what people actually write.
//
// Every image Raspberry Pi ships is .img.xz, and nothing here had ever
// handed the CLI one. The extraction itself is covered thoroughly at the
// thread level; what was not covered is the whole path -- the CLI deciding
// what kind of source it has been given, the extractor running, and the
// decompressed bytes reaching the card.
//
// That last part is the assertion worth having. "It said Write successful"
// would pass on an empty target; comparing the bytes says the image came
// out the other end.
// ══════════════════════════════════════════════════════════════

namespace {
// A random plain image and its .xz, or an empty pair where xz is not
// installed. Random rather than patterned, so a target that merely happens
// to contain the right length of something cannot match.
struct CompressedImage
{
    QString plain;
    QString compressed;
    QByteArray plainBytes;

    explicit CompressedImage(const QString &dir, int bytes = 4 * 1024 * 1024)
    {
        const QString xz = rpi_test::toolPath(QStringLiteral("xz"));
        if (xz.isEmpty())
            return;

        plainBytes.resize(bytes);
        for (int i = 0; i < bytes; ++i)
            plainBytes[i] = static_cast<char>((i * 2654435761u) >> 13);

        plain = QDir(dir).filePath(QStringLiteral("os.img"));
        QFile f(plain);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(plainBytes) == plainBytes.size());
        f.close();

        QProcess p;
        p.start(xz, {QStringLiteral("-k"), QStringLiteral("-0"),
                     QStringLiteral("-q"), plain});
        if (!p.waitForFinished(60000) || p.exitCode() != 0)
            return;

        const QString made = plain + QStringLiteral(".xz");
        if (QFileInfo::exists(made))
            compressed = made;
    }

    bool usable() const { return !compressed.isEmpty(); }
};
} // namespace

TEST_CASE("A compressed image is decompressed on its way to the card",
          "[cli][process][root]")
{
    if (!haveSudo())
        SKIP("passwordless sudo is not available, and writing needs root");

    Scratch scratch;
    const CompressedImage image(scratch.dir());
    if (!image.usable())
        SKIP("xz is not installed, so no compressed image can be built");

    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             image.compressed, target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    REQUIRE(r.exitCode == 0);
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("Write successful."));

    // The bytes, not the claim. What is on the target is the image as it was
    // before compression -- so the extractor ran, and what it produced is
    // what was written.
    QFile written(target);
    REQUIRE(written.open(QIODevice::ReadOnly));
    const QByteArray head = written.read(image.plainBytes.size());
    CHECK(head.size() == image.plainBytes.size());
    CHECK(head == image.plainBytes);
}

TEST_CASE("A truncated compressed image is refused, not half-written",
          "[cli][process][root]")
{
    // A download that stopped early. The archive is well-formed until it
    // stops, so an extractor that trusts what it has been given writes the
    // part that decompressed and reports success -- and the user gets a card
    // that boots part-way, or not at all, with nothing to say why.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");

    Scratch scratch;
    const CompressedImage image(scratch.dir());
    if (!image.usable())
        SKIP("xz is not installed");

    // Two thirds of the archive, so the header and some data survive.
    QFile whole(image.compressed);
    REQUIRE(whole.open(QIODevice::ReadOnly));
    const QByteArray all = whole.readAll();
    whole.close();
    REQUIRE(all.size() > 1024);

    const QString truncated = QDir(scratch.dir()).filePath(QStringLiteral("cut.img.xz"));
    {
        QFile f(truncated);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(all.left(all.size() * 2 / 3)) > 0);
    }

    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             truncated, target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("Write successful."));

    // And says something the reader can act on. libarchive's own words for
    // this are "Lzma library error: No progress is possible", which is true
    // and useless; what they need to know is that the file is short.
    CHECK_THAT(r.output.toStdString(), ContainsSubstring("incomplete"));
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("No progress is possible"));
}

TEST_CASE("A truncated gzip image is refused too", "[cli][process][root]")
{
    // The format that was always refused correctly, kept honest. libarchive
    // words gzip truncation differently, which is the only reason it never
    // fell into the shortcut that swallowed the xz one -- so it is worth a
    // case of its own rather than an assumption.
    if (!haveSudo())
        SKIP("passwordless sudo is not available");
    if (!rpi_test::haveTool(QStringLiteral("gzip")))
        SKIP("gzip is not installed");

    Scratch scratch;
    const CompressedImage image(scratch.dir());
    if (!image.usable())
        SKIP("xz is not installed, and the plain image comes from that fixture");

    const QString gz = QDir(scratch.dir()).filePath(QStringLiteral("os.img.gz"));
    {
        QProcess p;
        p.setStandardOutputFile(gz);
        p.start(rpi_test::toolPath(QStringLiteral("gzip")),
                {QStringLiteral("-1"), QStringLiteral("-c"), image.plain});
        REQUIRE(p.waitForFinished(60000));
        REQUIRE(p.exitCode() == 0);
    }

    QFile whole(gz);
    REQUIRE(whole.open(QIODevice::ReadOnly));
    const QByteArray all = whole.readAll();
    whole.close();
    REQUIRE(all.size() > 1024);

    const QString truncated = QDir(scratch.dir()).filePath(QStringLiteral("cut.img.gz"));
    {
        QFile f(truncated);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(all.left(all.size() * 2 / 3)) > 0);
    }

    const QString target = scratch.notADevice();
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); }

    const Run r = runImager({QStringLiteral("--cli"),
                             QStringLiteral("--enable-writing-system-drives"),
                             QStringLiteral("--disable-verify"),
                             truncated, target}, true);

    INFO(r.output.toStdString());
    REQUIRE(r.finished);
    CHECK(r.exitCode == 1);
    CHECK_THAT(r.output.toStdString(), !ContainsSubstring("Write successful."));
}
