/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * ImageWriter is the object the QML talks to: it holds the chosen image and
 * the chosen drive, decides whether the Write button does anything, and
 * refuses combinations that cannot work. Every failure here is one the user
 * meets directly -- a Write button that stays dead with no explanation, or a
 * write that starts against a card too small for the image.
 *
 * It was excluded from coverage as unreachable, which was true only of the
 * Qt that was to hand. It needs no QML engine -- it builds its models as
 * members and leaves the engine null -- so a QGuiApplication on the offscreen
 * platform is enough, and the Qt this project ships provides one.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_session.hpp>

#include <unistd.h>

#include "imagewriter.h"
#include "platformquirks.h"
#include "curlnetworkconfig.h"
#include "config.h"
#include "cli.h"
#include "bootimgcreator.h"
#include "downloadthread.h"
#include "file_operations.h"
#include "app_resources.h"
#include "platform_tools.h"
#include "drivelistmodel.h"
#include "drivelistmodelpollthread.h"

#include <QCryptographicHash>
#include <QTimeZone>
#include "signal_log.h"
#include "local_http_server.h"
#include "faulty_block_device.h"
#include <QJsonParseError>
#include <QProcess>
#include "fixture_process.h"
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QGuiApplication>
#include <QStandardPaths>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>
#include <QVersionNumber>
#include <QVariantMap>

using Catch::Matchers::ContainsSubstring;

namespace {

// Collects what ImageWriter reports back to the UI.
//
// The signals carry QVariant rather than QString: they are declared for QML's
// benefit, which is the whole point of this object.
struct UiLog
{
    QStringList errors;

    explicit UiLog(ImageWriter *w)
    {
        QObject::connect(w, &ImageWriter::error,
                         [this](QVariant m) { errors << m.toString(); });
    }
};

// startWrite() checks the source exists before it checks it will fit, so the
// capacity cases need a file that is really there. Its size on disk is
// irrelevant: the decompressed size is what is compared, and that is passed
// to setSrc() explicitly.
class SourceFile
{
public:
    SourceFile()
    {
        REQUIRE(_dir.isValid());
        _path = QDir(_dir.path()).filePath(QStringLiteral("image.img"));
        QFile f(_path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, '\0'));
        f.close();
    }
    QUrl url() const { return QUrl::fromLocalFile(_path); }

private:
    QTemporaryDir _dir;
    QString _path;
};

} // namespace

// ══════════════════════════════════════════════════════════════
// Whether the Write button does anything
// ══════════════════════════════════════════════════════════════

TEST_CASE("Nothing is ready to write before an image and a drive are chosen",
          "[imagewriter]")
{
    ImageWriter writer(nullptr);
    CHECK_FALSE(writer.readyToWrite());

    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    CHECK_FALSE(writer.readyToWrite());   // still no drive

    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    CHECK(writer.readyToWrite());
}

TEST_CASE("Clearing the drive makes it not ready again", "[imagewriter]")
{
    // Removing the card while it is selected has to take the Write button
    // with it, rather than leaving it live against a drive that is gone.
    ImageWriter writer(nullptr);
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    REQUIRE(writer.readyToWrite());

    writer.setDst(QString(), 0);
    CHECK_FALSE(writer.readyToWrite());
}

TEST_CASE("Starting a write with nothing chosen says what is missing",
          "[imagewriter]")
{
    // "Cannot start write" on its own leaves the user with nowhere to go.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cannot start write"));
    // Both missing things are named, not just the first.
    CHECK(log.errors[0].size() > QStringLiteral("Cannot start write. ").size());
}

TEST_CASE("Starting a write with no drive chosen says so", "[imagewriter]")
{
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cannot start write"));
}

// ══════════════════════════════════════════════════════════════
// Refusing a card that cannot hold the image
// ══════════════════════════════════════════════════════════════

TEST_CASE("An image larger than the card is refused before anything is written",
          "[imagewriter]")
{
    // The check the user should meet: told up front, rather than part-way
    // through a write that cannot finish.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    SourceFile source;
    writer.setSrc(source.url(),
                  /*downloadLen=*/0, /*extrLen=*/8ull * 1024 * 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);

    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("capacity"));
    // The message tells them how much they would need.
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("requires at least"));
}

TEST_CASE("An image that fits is not refused on capacity", "[imagewriter]")
{
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    SourceFile source;
    writer.setSrc(source.url(), 0, 1ull * 1024 * 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    writer.startWrite();

    for (const QString &e : log.errors) {
        INFO("reported: " << e.toStdString());
        CHECK_THAT(e.toStdString(), !ContainsSubstring("capacity"));
        // Guards against passing for the wrong reason: an unreadable source
        // is rejected earlier, and would satisfy the check above by accident.
        CHECK_THAT(e.toStdString(), !ContainsSubstring("Source file not found"));
    }
}

TEST_CASE("An image exactly filling the card is allowed", "[imagewriter]")
{
    // The boundary. Images are often built to exactly the size they are meant
    // to occupy, so an off-by-one here -- >= where > was meant -- would refuse
    // a whole class of perfectly good writes, and the two cases either side of
    // it look identical in a diff.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    SourceFile source;
    const quint64 card = 4ull * 1024 * 1024 * 1024;
    writer.setSrc(source.url(), 0, card);
    writer.setDst(QStringLiteral("/dev/null"), card);
    writer.startWrite();

    for (const QString &e : log.errors) {
        INFO("reported: " << e.toStdString());
        CHECK_THAT(e.toStdString(), !ContainsSubstring("capacity"));
    }
}

TEST_CASE("An image one byte too big is refused", "[imagewriter]")
{
    // The other side of the same boundary, so the pair pins it from both
    // directions rather than leaving the comparison free to drift.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    SourceFile source;
    const quint64 card = 4ull * 1024 * 1024 * 1024;
    writer.setSrc(source.url(), 0, card + 1);
    writer.setDst(QStringLiteral("/dev/null"), card);
    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("capacity"));
}

TEST_CASE("A card of unknown size does not trigger the capacity check",
          "[imagewriter]")
{
    // _devLen of zero means "not known", not "zero bytes". Treating it as a
    // size would refuse every write to a drive whose capacity could not be
    // read.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    SourceFile source;
    writer.setSrc(source.url(), 0, 8ull * 1024 * 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 0);
    writer.startWrite();

    for (const QString &e : log.errors) {
        INFO("reported: " << e.toStdString());
        CHECK_THAT(e.toStdString(), !ContainsSubstring("capacity"));
        CHECK_THAT(e.toStdString(), !ContainsSubstring("Source file not found"));
    }
}

// ══════════════════════════════════════════════════════════════
// The card leaving between choosing it and pressing Write
//
// A card can be pulled out, or a reader can drop off the bus, at any point
// after it has been chosen. readyToWrite() goes false, and what the user is
// told then decides whether they understand what happened: "No storage
// device selected" sends them back to a list where the drive they picked is
// not there, with no hint that it ever was.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A drive that has gone is named as gone, not as never chosen",
          "[imagewriter][removal]")
{
    SourceFile source;
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(source.url(), 0, 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    REQUIRE(writer.readyToWrite());

    // The signal the drive poller raises when the device it was watching
    // stops being listed.
    REQUIRE(QMetaObject::invokeMethod(&writer, "onSelectedDeviceRemoved",
                                      Q_ARG(QString, QStringLiteral("/dev/null"))));
    CHECK_FALSE(writer.readyToWrite());

    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(),
               ContainsSubstring("no longer available"));
}

TEST_CASE("Unplugging some other drive leaves the chosen one alone",
          "[imagewriter][removal]")
{
    // Removals arrive for every device on the machine. Acting on one that is
    // not the target would cancel a write, or refuse to start one, because
    // somebody took out an unrelated USB stick.
    SourceFile source;
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(source.url(), 0, 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    REQUIRE(writer.readyToWrite());

    REQUIRE(QMetaObject::invokeMethod(&writer, "onSelectedDeviceRemoved",
                                      Q_ARG(QString, QStringLiteral("/dev/sdz"))));

    CHECK(writer.readyToWrite());
    for (const QString &e : log.errors) {
        INFO("reported: " << e.toStdString());
        CHECK_THAT(e.toStdString(), !ContainsSubstring("no longer available"));
    }
}


//
// startWrite() looks at a local source before it looks at anything else,
// because every later stage assumes there are bytes to read. Each refusal
// here is a file chooser dialog away from any user: the wrong entry
// double-clicked, a download that stopped part-way, a file on a volume
// mounted read-only for somebody else.
//
// The refusal has to name the file. There is no other way for someone who
// picked the wrong thing out of a folder of images to tell which one Imager
// objected to.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A folder picked instead of an image is refused", "[imagewriter]")
{
    // One entry above the file they meant, in a chooser that shows both.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(QUrl::fromLocalFile(dir.path()), 0, 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("not a regular file"));
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring(dir.path().toStdString()));
}

TEST_CASE("An image that cannot be read is refused", "[imagewriter]")
{
    if (geteuid() == 0)
        SKIP("running as root, which can read a file with no permissions at all");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("locked.img"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1024, '\0'));
    }
    REQUIRE(QFile::setPermissions(path, QFileDevice::Permissions()));

    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(QUrl::fromLocalFile(path), 0, 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    writer.startWrite();

    // Restored before the assertions, so a failing case still leaves a
    // directory that can be removed.
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("not readable"));
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring(path.toStdString()));
}

TEST_CASE("An empty image file is refused rather than written as a blank card",
          "[imagewriter]")
{
    // A download that stopped, or a copy off a card that failed: the file is
    // there, the name is right, and it holds nothing. Nothing downstream
    // objects to it -- it passes the capacity check comfortably, extracts to
    // no bytes, and the write reports success. The user is left with a card
    // they believe is imaged and nothing on screen suggesting otherwise, so
    // this is the only place it can be caught.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("truncated.img"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
    }
    REQUIRE(QFileInfo(path).size() == 0);

    ImageWriter writer(nullptr);
    UiLog log(&writer);

    writer.setSrc(QUrl::fromLocalFile(path), 0, 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("empty"));
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring(path.toStdString()));
}

TEST_CASE("A source that is not there is named before the card is measured",
          "[imagewriter]")
{
    // Both refusals apply at once: no such file, and an image far too big for
    // the card. The one the user is told about has to be the one they can do
    // something about -- being told the card is too small for a file that
    // does not exist sends them looking for a bigger card.
    ImageWriter writer(nullptr);
    UiLog log(&writer);

    const QString missing = QStringLiteral("/nonexistent-rpi-imager/gone.img");
    writer.setSrc(QUrl::fromLocalFile(missing), 0, 64ull * 1024 * 1024 * 1024);
    writer.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    writer.startWrite();

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("not found"));
    CHECK_THAT(log.errors[0].toStdString(), !ContainsSubstring("capacity"));
}

// ══════════════════════════════════════════════════════════════
// Update prompts
// ══════════════════════════════════════════════════════════════

TEST_CASE("A newer published version is recognised", "[imagewriter]")
{
    // Drives the "there is an update" prompt. Wrong in one direction and
    // users are nagged about a release they already have; wrong in the other
    // and they are never told.
    ImageWriter writer(nullptr);
    const QString current = writer.constantVersion();
    INFO("built as version " << current.toStdString());

    // A version far below anything shippable is never newer.
    CHECK_FALSE(writer.isVersionNewer(QStringLiteral("0.0.1")));
    // ...and one far above always is.
    CHECK(writer.isVersionNewer(QStringLiteral("999.0.0")));
    // The same version is not an update.
    CHECK_FALSE(writer.isVersionNewer(current));
}

TEST_CASE("A leading v on the version is tolerated", "[imagewriter]")
{
    // The releases feed tags versions "v1.2.3"; QVersionNumber wants a digit
    // first and silently parses "v1.2.3" as nothing at all, which reads as
    // "no update available" for every release.
    ImageWriter writer(nullptr);
    CHECK(writer.isVersionNewer(QStringLiteral("v999.0.0")));
    CHECK(writer.isVersionNewer(QStringLiteral("V999.0.0")));
    CHECK_FALSE(writer.isVersionNewer(QStringLiteral("v0.0.1")));
}

// ══════════════════════════════════════════════════════════════
// Naming a download
// ══════════════════════════════════════════════════════════════

TEST_CASE("A file name is taken from the URL path", "[imagewriter]")
{
    ImageWriter writer(nullptr);
    CHECK(writer.fileNameFromUrl(QUrl(QStringLiteral(
              "https://downloads.raspberrypi.org/raspios/2024-11-19-raspios.img.xz")))
          == QStringLiteral("2024-11-19-raspios.img.xz"));
    CHECK(writer.fileNameFromUrl(QUrl(QStringLiteral("file:///home/pi/my.img")))
          == QStringLiteral("my.img"));
}

TEST_CASE("A URL with a query string does not carry it into the file name",
          "[imagewriter]")
{
    // The name becomes a path on disk; a query string in it makes a mess of
    // the cache file at best.
    ImageWriter writer(nullptr);
    const QString name = writer.fileNameFromUrl(
        QUrl(QStringLiteral("https://example.com/os.img.xz?token=abc&x=1")));
    INFO("name: " << name.toStdString());
    CHECK_FALSE(name.contains(QLatin1Char('?')));
    CHECK_FALSE(name.contains(QLatin1Char('&')));
}

// ══════════════════════════════════════════════════════════════
// The models behind the pickers
// ══════════════════════════════════════════════════════════════

TEST_CASE("The drive list is reachable and starts empty of chosen state",
          "[imagewriter]")
{
    ImageWriter writer(nullptr);
    DriveListModel *drives = writer.getDriveList();
    REQUIRE(drives != nullptr);

    // Parented to the writer, so QML cannot delete it out from under us.
    CHECK(drives->parent() == &writer);
    // rowCount is whatever this machine has; asking must not crash.
    CHECK(drives->rowCount(QModelIndex()) >= 0);
}

TEST_CASE("The hardware and OS list models are reachable", "[imagewriter]")
{
    ImageWriter writer(nullptr);
    CHECK(writer.getHWList() != nullptr);
    CHECK(writer.getOSList() != nullptr);
}

// ── A settings file an earlier root run left behind ───────────────────
//
// Running the application once with sudo leaves the configuration file owned
// by root. Every run afterwards, as the user, then finds it unwritable --
// and QSettings does not complain. Nothing is saved: the last-used
// repository, the customisation the user staged, the "don't warn me again"
// they ticked. It all appears to work and none of it survives the restart,
// with nothing on screen to say why.
//
// So the constructor repairs it: read what is there, remove the file, write
// the contents back into a new one the user owns. That had never been
// exercised -- every test ran against a settings file it had just created
// itself.

TEST_CASE("A settings file left unwritable is repaired, keeping what it held",
          "[imagewriter][settings]")
{
    if (::geteuid() == 0)
        SKIP("root can write a file with no write bit, so the case this is "
             "about cannot arise");

    QString path;
    {
        QSettings seed;
        path = seed.fileName();
        REQUIRE_FALSE(path.isEmpty());
        seed.setValue(QStringLiteral("imager/repository"),
                      QStringLiteral("https://example.invalid/os_list.json"));
        seed.sync();
    }
    REQUIRE(QFile::exists(path));

    // Read-only, the way it comes back from a root-owned file this user
    // cannot write. Restored whatever happens below, so a failure here does
    // not leave the rest of the suite unable to save anything.
    struct Restore {
        QString path;
        ~Restore() {
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                            QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
    } restore{path};

    REQUIRE(QFile::setPermissions(path, QFileDevice::ReadOwner));
    {
        QSettings check;
#ifdef Q_OS_MACOS
        // QSettings is CFPreferences here, and it reports on the preferences
        // store rather than on the file backing it: clearing the write bit
        // leaves isWritable() true, so the state this case repairs cannot be
        // set up in the first place.
        if (check.isWritable())
            SKIP("QSettings on macOS does not report a read-only file as unwritable");
#endif
        REQUIRE_FALSE(check.isWritable());
    }

    // The repair is part of construction, which is where a user meets it.
    { ImageWriter writer(nullptr); }

    QSettings after;
    CHECK(after.isWritable());
    CHECK(after.value(QStringLiteral("imager/repository")).toString() ==
          QStringLiteral("https://example.invalid/os_list.json"));
}

TEST_CASE("A disable_warnings flag found in the settings does not survive startup",
          "[imagewriter][settings]")
{
    // The flag turns off the confirmations that stand between a user and
    // erasing the wrong disk. It is meant to last one session -- a
    // deployment sets it for the run it is doing -- and an older version
    // that wrote it to disk, or a copied configuration file, would otherwise
    // leave every later run silently unguarded on a machine whose owner
    // never asked for that.
    //
    // Belt and braces, so nothing else observes it: the only way to see it
    // work is that the flag is gone afterwards.
    {
        QSettings seed;
        seed.setValue(QStringLiteral("disable_warnings"), true);
        seed.sync();
        REQUIRE(seed.contains(QStringLiteral("disable_warnings")));
    }

    { ImageWriter writer(nullptr); }

    QSettings after;
    after.sync();
    CHECK_FALSE(after.contains(QStringLiteral("disable_warnings")));
}

// ══════════════════════════════════════════════════════════════
// The card being pulled out
//
// A user unplugging the card is not an exotic case: it is what happens when
// somebody grabs the wrong drive, or a hub loses power, or they simply think
// better of it. What Imager does about it depends on when: idle, the
// selection has to stop being usable; mid-write, the write has to stop and
// say the card went away rather than reporting whatever I/O error the driver
// produced; and after a successful write, removing the card is just ejecting
// it and must raise nothing at all.
//
// Each of those is a different signal to the UI, and none of them had a
// test.
// ══════════════════════════════════════════════════════════════

namespace {

// onSelectedDeviceRemoved() and onSuccess() are protected; everything else
// here is the writer as it ships.
class WriterWithRemoval : public ImageWriter
{
public:
    WriterWithRemoval() : ImageWriter(nullptr) {}
    using ImageWriter::onSelectedDeviceRemoved;
    using ImageWriter::onSuccess;
    using ImageWriter::onFinalizing;
    using ImageWriter::onCancelled;
};

// Which of the mutually exclusive outcomes the writer reported.
struct RemovalOutcome
{
    int selectedDeviceRemoved = 0;
    int cancelledPlain = 0;
    int cancelledByRemoval = 0;

    explicit RemovalOutcome(ImageWriter *w)
    {
        QObject::connect(w, &ImageWriter::selectedDeviceRemoved,
                         [this]() { ++selectedDeviceRemoved; });
        QObject::connect(w, &ImageWriter::cancelled,
                         [this]() { ++cancelledPlain; });
        QObject::connect(w, &ImageWriter::writeCancelledDueToDeviceRemoval,
                         [this]() { ++cancelledByRemoval; });
    }
};

} // namespace

TEST_CASE("Another drive being unplugged leaves the choice alone",
          "[imagewriter][removal]")
{
    // Pulling an unrelated stick out of the same hub must not disturb a
    // selection that is still there -- the Write button going dead for no
    // visible reason is worse than most errors, because nothing says why.
    WriterWithRemoval writer;
    RemovalOutcome outcome(&writer);
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    REQUIRE(writer.readyToWrite());

    writer.onSelectedDeviceRemoved(QStringLiteral("/dev/somethingelse"));

    CHECK(writer.readyToWrite());
    CHECK(outcome.selectedDeviceRemoved == 0);
}

TEST_CASE("The chosen drive being unplugged makes the write impossible and says so",
          "[imagewriter][removal]")
{
    WriterWithRemoval writer;
    RemovalOutcome outcome(&writer);
    UiLog log(&writer);
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    REQUIRE(writer.readyToWrite());

    writer.onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    // The screen is told, so it can drop the selection rather than leaving a
    // drive listed that is not there.
    CHECK(outcome.selectedDeviceRemoved == 1);
    CHECK_FALSE(writer.readyToWrite());

    // And pressing Write anyway names the drive as the thing that is wrong,
    // rather than saying nothing was selected -- the user did select one.
    writer.startWrite();
    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("no longer available"));
}

TEST_CASE("Unplugging after a successful write is just ejecting",
          "[imagewriter][removal]")
{
    // The card comes out because the write finished. An error here would
    // undo the one screen in the whole application that says everything
    // worked.
    WriterWithRemoval writer;
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    writer.onSuccess();
    RemovalOutcome outcome(&writer);
    UiLog log(&writer);

    writer.onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(outcome.selectedDeviceRemoved == 0);
    CHECK(outcome.cancelledPlain == 0);
    CHECK(outcome.cancelledByRemoval == 0);
    CHECK(log.errors.isEmpty());
}

TEST_CASE("Unplugging during preparation says the card went away",
          "[imagewriter][removal]")
{
    // The window between pressing Write and the writing thread existing:
    // preparation, which includes verifying a cached image and can run for
    // seconds. Pull the card here and the write has to stop -- and say which
    // of the two things happened, because "cancelled" reads as something the
    // user did.
    WriterWithRemoval writer;
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    writer.onFinalizing();   // a state the writer counts as in-progress
    RemovalOutcome outcome(&writer);

    writer.onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(outcome.cancelledByRemoval == 1);
    CHECK(outcome.cancelledPlain == 0);
}

TEST_CASE("A later cancellation is not still blamed on the card",
          "[imagewriter][removal]")
{
    // The other half of the case above. The reason must not stay set: the
    // next write the user cancels themselves would be reported as the card
    // having been removed, on a card sitting right there.
    //
    // The second cancellation is delivered through onCancelled(), which is
    // what the writing thread's completion calls -- and the only route that
    // consults the reason. Cancelling again the way the case above does
    // would take the no-thread path and not consult it, so nothing could be
    // observed.
    WriterWithRemoval writer;
    writer.setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    writer.onFinalizing();
    writer.onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    // A second write, cancelled by the user this time.
    writer.setDst(QStringLiteral("/dev/null"), 1024 * 1024);
    writer.onFinalizing();
    RemovalOutcome outcome(&writer);
    writer.onCancelled();

    CHECK(outcome.cancelledPlain == 1);
    CHECK(outcome.cancelledByRemoval == 0);
}

// ══════════════════════════════════════════════════════════════
// Noticing that the network came back
//
// isOnline() is polled, and it is where Imager decides whether to go and
// fetch the OS list. Two of its branches carry issue numbers in the source:
// #1212, where a firewall blocked the first fetch and the user later allowed
// it, and #809, where the application starts with no network at all. Both
// were user reports, and neither branch had a test.
//
// The property that matters as much as the retry is that it happens once.
// pollNetwork() calls this on a hundred-millisecond timer, so a missing
// guard would restart the fetch ten times a second and it would never
// finish.
//
// The repository is pointed at a file on disk, so the fetch these cases
// provoke is real but local -- nothing here contacts raspberrypi.com.
// ══════════════════════════════════════════════════════════════

namespace {

class OnlineWriter : public ImageWriter
{
public:
    OnlineWriter() : ImageWriter(nullptr) {}
    using ImageWriter::onOsListFetchComplete;
};

struct OnlineSpy
{
    int networkOnline = 0;
    int osListUnavailable = 0;

    explicit OnlineSpy(ImageWriter *w)
    {
        QObject::connect(w, &ImageWriter::networkOnline,
                         [this]() { ++networkOnline; });
        QObject::connect(w, &ImageWriter::osListUnavailableChanged,
                         [this]() { ++osListUnavailable; });
    }
};

QString writeRepositoryFile(const QString& dir, const QString& name)
{
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray body = QByteArrayLiteral(
        "{\"os_list\":[{\"name\":\"Test OS\",\"description\":\"d\","
        "\"url\":\"https://example.invalid/x.img.xz\",\"icon\":\"\","
        "\"release_date\":\"2026-01-01\",\"extract_size\":1048576,"
        "\"image_download_size\":524288,\"extract_sha256\":\"ab\"}]}");
    REQUIRE(f.write(body) == body.size());
    return path;
}

} // namespace

TEST_CASE("A network that comes back sends Imager to fetch the list again",
          "[imagewriter][online]")
{
    // Issue #1212: the first fetch was refused by a firewall, the user
    // allowed it, and nothing went back for the list -- so the OS list
    // stayed empty until the application was restarted.
    if (!PlatformQuirks::hasNetworkConnectivity())
        SKIP("this machine reports no network, so the branch under test is "
             "not the one that runs");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString repo = writeRepositoryFile(dir.path(), QStringLiteral("repo.json"));

    OnlineWriter writer;
    OnlineSpy spy(&writer);
    writer.setCustomOsListUrl(QUrl::fromLocalFile(repo));

    CHECK(writer.isOnline());
    CHECK(spy.networkOnline == 1);
}

TEST_CASE("Noticing the network twice does not fetch the list twice",
          "[imagewriter][online]")
{
    // The guard on the retry. Polled ten times a second, an unguarded branch
    // would abandon and restart the fetch on every tick, and the list would
    // never arrive at all.
    if (!PlatformQuirks::hasNetworkConnectivity())
        SKIP("this machine reports no network");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString repo = writeRepositoryFile(dir.path(), QStringLiteral("repo.json"));

    OnlineWriter writer;
    OnlineSpy spy(&writer);
    writer.setCustomOsListUrl(QUrl::fromLocalFile(repo));

    for (int i = 0; i < 5; ++i)
        writer.isOnline();

    CHECK(spy.networkOnline == 1);
}

TEST_CASE("With a list already in hand, coming online fetches nothing",
          "[imagewriter][online]")
{
    // The other online branch. There is a list, so there is nothing to go
    // back for: noticing the network is worth recording but not worth
    // another request.
    if (!PlatformQuirks::hasNetworkConnectivity())
        SKIP("this machine reports no network");

    OnlineWriter writer;
    writer.onOsListFetchComplete(
        QByteArrayLiteral("{\"os_list\":[{\"name\":\"Already here\","
                          "\"description\":\"d\",\"url\":\"\",\"icon\":\"\"}]}"),
        writer.osListUrl(), writer.osListUrl());

    OnlineSpy spy(&writer);
    CHECK(writer.isOnline());

    CHECK(spy.networkOnline == 0);
    CHECK(spy.osListUnavailable == 0);
}

// ══════════════════════════════════════════════════════════════
// When fetching the OS list fails
//
// Four different things depending on what failed and what is already on
// screen, and the differences are all visible to the user: whether they get
// the offline placeholder, whether the list they can already see survives,
// and whether Imager quietly tries again a different way.
//
// The repository is a file on disk throughout, so the retries these cases
// provoke are local. IPv4-only is process-wide state and is put back after
// each case.
// ══════════════════════════════════════════════════════════════

namespace {

class FetchFailWriter : public ImageWriter
{
public:
    FetchFailWriter() : ImageWriter(nullptr) {}
    using ImageWriter::onOsListFetchComplete;
    using ImageWriter::onOsListFetchError;
};

class Ipv4OnlyGuard
{
public:
    Ipv4OnlyGuard() : _saved(CurlNetworkConfig::instance().ipv4Only()) {}
    ~Ipv4OnlyGuard() { CurlNetworkConfig::instance().setIPv4Only(_saved); }

    Ipv4OnlyGuard(const Ipv4OnlyGuard&) = delete;
    Ipv4OnlyGuard& operator=(const Ipv4OnlyGuard&) = delete;

private:
    bool _saved;
};

} // namespace

TEST_CASE("A first failure retries the list over IPv4 before giving up",
          "[imagewriter][fetchfail]")
{
    // Windows 11 with broken IPv6 routing: DNS answers with AAAA records and
    // the connection then times out, where a browser would have fallen back
    // to IPv4 without anyone noticing. Imager does the same thing once
    // rather than showing an offline screen to a machine that is online.
    if (!PlatformQuirks::hasNetworkConnectivity())
        SKIP("the retry is conditional on connectivity being present");

    Ipv4OnlyGuard guard;
    CurlNetworkConfig::instance().setIPv4Only(false);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString repo = writeRepositoryFile(dir.path(), QStringLiteral("repo.json"));

    FetchFailWriter writer;
    OnlineSpy spy(&writer);
    writer.setCustomOsListUrl(QUrl::fromLocalFile(repo));

    writer.onOsListFetchError(QStringLiteral("Connection timed out"),
                              writer.osListUrl());

    CHECK(CurlNetworkConfig::instance().ipv4Only());
    // Not offline yet: it has not finished trying.
    CHECK(spy.osListUnavailable == 0);
}

TEST_CASE("A second failure gives up and shows the offline screen",
          "[imagewriter][fetchfail]")
{
    // The retry has already been spent, so this is the end of the road and
    // the user needs the placeholder and its Retry button rather than an
    // empty list that looks like there are no images.
    Ipv4OnlyGuard guard;
    CurlNetworkConfig::instance().setIPv4Only(true);

    FetchFailWriter writer;
    OnlineSpy spy(&writer);

    writer.onOsListFetchError(QStringLiteral("Connection timed out"),
                              writer.osListUrl());

    CHECK(spy.osListUnavailable == 1);
}

TEST_CASE("A failed refresh keeps the list already on screen",
          "[imagewriter][fetchfail]")
{
    // A refresh of a list that arrived earlier. Reporting this as offline
    // would replace a working list with the placeholder because of a blip,
    // and the user would be told to check a connection that is fine.
    Ipv4OnlyGuard guard;
    CurlNetworkConfig::instance().setIPv4Only(true);

    FetchFailWriter writer;
    writer.onOsListFetchComplete(
        QByteArrayLiteral("{\"os_list\":[{\"name\":\"Already here\","
                          "\"description\":\"d\",\"url\":\"\",\"icon\":\"\"}]}"),
        writer.osListUrl(), writer.osListUrl());
    // What the screen is showing, built-in "Erase" and "Use custom" entries
    // and all.
    const QJsonArray before = writer.getFilteredOSlistDocument().object()
                                  .value(QStringLiteral("os_list")).toArray();
    REQUIRE_FALSE(before.isEmpty());

    OnlineSpy spy(&writer);
    writer.onOsListFetchError(QStringLiteral("Temporary failure in name resolution"),
                              writer.osListUrl());

    CHECK(spy.osListUnavailable == 0);
    // Not merely non-empty: the same list, unchanged.
    const QJsonArray after = writer.getFilteredOSlistDocument().object()
                                 .value(QStringLiteral("os_list")).toArray();
    CHECK(after == before);
    bool stillListed = false;
    for (const auto& entry : after) {
        if (entry.toObject().value(QStringLiteral("name")).toString()
            == QStringLiteral("Already here"))
            stillListed = true;
    }
    CHECK(stillListed);
}

TEST_CASE("An error about some other address is not the list failing",
          "[imagewriter][fetchfail]")
{
    // Categories are fetched from their own addresses, so one of them being
    // unreachable is not the repository being unreachable, and only the
    // repository's own failure means offline.
    //
    // Checked with nothing fetched yet, deliberately. With a list already in
    // hand the previous case's route reaches the same answer whether or not
    // the address is compared, so treating every failure as the top-level one
    // would go unnoticed -- which it did, until this case was written this
    // way round.
    Ipv4OnlyGuard guard;
    CurlNetworkConfig::instance().setIPv4Only(true);

    FetchFailWriter writer;
    OnlineSpy spy(&writer);
    writer.onOsListFetchError(
        QStringLiteral("404"),
        QUrl(QStringLiteral("https://example.invalid/a-category.json")));

    CHECK(spy.osListUnavailable == 0);
}

// ══════════════════════════════════════════════════════════════
// The signing key's fingerprint
//
// Shown on the secure-boot step as "Public Key Fingerprint", and it is the
// only way a user can check that the key they have picked is the one already
// fused into the board in front of them. Getting that wrong is not
// recoverable: the OTP fuses are burned once, and a board fused to a
// different key will not boot again.
//
// So what matters is that it is derived from the key and nothing else, that
// two keys never look alike, that the same key always reads the same, and
// that an unusable key produces nothing rather than a plausible-looking
// string -- the screen turns an empty answer into "(unable to compute)",
// which is a prompt to go and look, where a wrong fingerprint is not.
// ══════════════════════════════════════════════════════════════

namespace {

bool haveOpensslBinary()
{
    QProcess p;
    p.start(QStringLiteral("openssl"), {QStringLiteral("version")});
    p.waitForFinished(10000);
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// A real 2048-bit RSA private key, since the fingerprint is taken over the
// public modulus and exponent the boot ROM would see.
bool generateRsaKeyAt(const QString& path)
{
    QProcess p;
    p.start(QStringLiteral("openssl"),
            {QStringLiteral("genrsa"), QStringLiteral("-out"), path,
             QStringLiteral("2048")});
    if (!p.waitForFinished(60000))
        return false;
    return p.exitCode() == 0 && QFileInfo(path).size() > 0;
}

} // namespace

TEST_CASE("A signing key's fingerprint is readable and stable",
          "[imagewriter][fingerprint]")
{
    if (!haveOpensslBinary())
        SKIP("openssl is not installed, so no signing key can be generated");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString key = QDir(dir.path()).filePath(QStringLiteral("secureboot.pem"));
    REQUIRE(generateRsaKeyAt(key));

    ImageWriter writer(nullptr);
    const QString fingerprint = writer.getRsaKeyFingerprint(key);
    INFO("fingerprint: " << fingerprint.toStdString());

    // The shape a user reads off the screen and compares by eye against what
    // the board reports: two groups of sixteen hex digits, upper case.
    REQUIRE(fingerprint.size() == 33);
    CHECK(fingerprint[16] == QLatin1Char(':'));
    const QString hex = fingerprint.left(16) + fingerprint.mid(17);
    CHECK(hex.size() == 32);
    for (const QChar& c : hex) {
        INFO("character: " << QString(c).toStdString());
        CHECK((c.isDigit() || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))));
    }

    // Reading it twice has to give the same answer, or it is no use for
    // comparing against anything.
    CHECK(writer.getRsaKeyFingerprint(key) == fingerprint);
}

TEST_CASE("Two signing keys never look alike", "[imagewriter][fingerprint]")
{
    // The whole point. Two keys sharing a fingerprint would let somebody
    // confirm a key that is not the one the board is fused to, and they would
    // find out after the fuses were burned.
    if (!haveOpensslBinary())
        SKIP("openssl is not installed");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString first = QDir(dir.path()).filePath(QStringLiteral("a.pem"));
    const QString second = QDir(dir.path()).filePath(QStringLiteral("b.pem"));
    REQUIRE(generateRsaKeyAt(first));
    REQUIRE(generateRsaKeyAt(second));

    ImageWriter writer(nullptr);
    const QString a = writer.getRsaKeyFingerprint(first);
    const QString b = writer.getRsaKeyFingerprint(second);

    INFO("a: " << a.toStdString() << "  b: " << b.toStdString());
    REQUIRE_FALSE(a.isEmpty());
    REQUIRE_FALSE(b.isEmpty());
    CHECK(a != b);
}

TEST_CASE("A copy of the same key reads the same", "[imagewriter][fingerprint]")
{
    // Moving the key, or keeping a second copy of it, must not change what
    // it is called. The fingerprint follows the key, not the path.
    if (!haveOpensslBinary())
        SKIP("openssl is not installed");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString original = QDir(dir.path()).filePath(QStringLiteral("orig.pem"));
    const QString copy = QDir(dir.path()).filePath(QStringLiteral("elsewhere.pem"));
    REQUIRE(generateRsaKeyAt(original));
    REQUIRE(QFile::copy(original, copy));

    ImageWriter writer(nullptr);
    const QString a = writer.getRsaKeyFingerprint(original);
    REQUIRE_FALSE(a.isEmpty());
    CHECK(writer.getRsaKeyFingerprint(copy) == a);
}

TEST_CASE("Something that is not a key produces no fingerprint at all",
          "[imagewriter][fingerprint]")
{
    // The screen shows "(unable to compute)" for an empty answer, which sends
    // the user back to the file picker. A string derived from whatever the
    // file happened to contain would instead be compared against the board
    // and disagree, with nothing saying why.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ImageWriter writer(nullptr);

    // Nothing chosen.
    CHECK(writer.getRsaKeyFingerprint(QString()).isEmpty());
    // A path that is not there.
    CHECK(writer.getRsaKeyFingerprint(
              QDir(dir.path()).filePath(QStringLiteral("absent.pem"))).isEmpty());

    // A file that exists and is not a key.
    const QString notAKey = QDir(dir.path()).filePath(QStringLiteral("notes.txt"));
    {
        QFile f(notAKey);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("this is not a key, it is a shopping list\n");
    }
    CHECK(writer.getRsaKeyFingerprint(notAKey).isEmpty());

    // A directory, which is what a user picks by accident.
    CHECK(writer.getRsaKeyFingerprint(dir.path()).isEmpty());

    // A public key where a private one is needed: the signer cannot use it,
    // so offering a fingerprint for it would be confirming a key that cannot
    // sign.
    if (haveOpensslBinary()) {
        const QString priv = QDir(dir.path()).filePath(QStringLiteral("p.pem"));
        const QString pub = QDir(dir.path()).filePath(QStringLiteral("p.pub"));
        REQUIRE(generateRsaKeyAt(priv));
        QProcess p;
        p.start(QStringLiteral("openssl"),
                {QStringLiteral("rsa"), QStringLiteral("-in"), priv,
                 QStringLiteral("-pubout"), QStringLiteral("-out"), pub});
        REQUIRE(p.waitForFinished(30000));
        REQUIRE(p.exitCode() == 0);
        INFO("public-key fingerprint: "
             << writer.getRsaKeyFingerprint(pub).toStdString());
        CHECK(writer.getRsaKeyFingerprint(pub).isEmpty());
    }
}

#ifdef KEYBOARD_PROBE_BINARY
// ══════════════════════════════════════════════════════════════
// Which keyboard the Pi-booted Imager thinks it has
//
// On the embedded build there is a keyboard plugged into the Pi and no
// desktop to have configured it, so Imager picks the layout itself: from the
// country code in the device tree, or failing that from the name of the node
// the kernel made in /dev/input/by-id.
//
// The first thing anybody types on that keyboard is a Wi-Fi password. A
// wrong layout means a password that looks right on screen and is not, a
// board that never joins the network, and nothing connecting the two -- so
// the mapping from the number in that filename to a country is worth
// pinning, being exactly the sort of table that survives being reordered.
//
// /dev/input/by-id does not exist on a machine with no such keyboard, so a
// synthetic /dev/input is bind-mounted over the real one inside an
// unprivileged mount namespace and a probe run inside.
// ══════════════════════════════════════════════════════════════

namespace {

bool haveMountNamespacesForKeyboard()
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("true")});
    if (!p.waitForFinished(10000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// A /dev/input containing a by-id directory with the given node names.
bool buildInputFixture(const QString& root, const QStringList& byIdNames)
{
    const QString byId = root + QStringLiteral("/by-id");
    if (!QDir().mkpath(byId))
        return false;
    for (const QString& name : byIdNames) {
        QFile f(QDir(byId).filePath(name));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write("not really a device node, but named like one\n");
    }
    return true;
}

// The layout the probe chose, or a null QString if it could not be run.
QString keyboardWith(const QString& fixture)
{
    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-rm"), QStringLiteral("--propagation"),
             QStringLiteral("private"), QStringLiteral("sh"), QStringLiteral("-c"),
             QStringLiteral("mount --bind \"$1\" /dev/input && exec \"$2\""),
             QStringLiteral("_"), fixture,
             QStringLiteral(KEYBOARD_PROBE_BINARY)});
    if (!p.waitForFinished(60000))
        return {};
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString& line : out.split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("KEYBOARD=")))
            return line.mid(QStringLiteral("KEYBOARD=").size());
    }
    return {};
}

#define REQUIRE_KEYBOARD_HARNESS()                                                        \
    if (QFile::exists(QStringLiteral("/proc/device-tree/chosen/rpi-country-code")))        \
        SKIP("this machine reports a country code in the device tree, which is "           \
             "consulted first, so the node name cannot be observed");                      \
    if (!haveMountNamespacesForKeyboard())                                                 \
        SKIP("unprivileged mount namespaces are unavailable, so /dev/input cannot "        \
             "be replaced")

} // namespace

TEST_CASE("The keyboard's number chooses its country", "[imagewriter][keyboard]")
{
    REQUIRE_KEYBOARD_HARNESS();

    struct Row { int number; const char* country; };
    // The order of the table in detectPiKeyboard(), stated independently so
    // that reordering it fails here rather than shipping.
    const Row rows[] = {
        {1, "gb"}, {2, "fr"}, {3, "es"}, {4, "us"}, {5, "de"}, {6, "it"},
        {7, "jp"}, {8, "pt"}, {9, "no"}, {10, "se"}, {11, "dk"}, {12, "ru"},
        {13, "tr"}, {14, "il"},
    };

    for (const Row& row : rows) {
        QTemporaryDir fixture;
        REQUIRE(fixture.isValid());
        const QString node =
            QStringLiteral("RPI_Wired_Keyboard_%1").arg(row.number);
        REQUIRE(buildInputFixture(fixture.path(), {node}));

        INFO("node: " << node.toStdString());
        CHECK(keyboardWith(fixture.path()) == QString::fromUtf8(row.country));
    }
}

TEST_CASE("A keyboard number nobody knows picks no layout at all",
          "[imagewriter][keyboard]")
{
    // Past the end of the table. Nothing is better than something wrong: an
    // unset layout leaves the default in place, where an index off the end
    // would be whichever country happened to sit there.
    REQUIRE_KEYBOARD_HARNESS();

    QTemporaryDir fixture;
    REQUIRE(fixture.isValid());
    REQUIRE(buildInputFixture(fixture.path(),
                              {QStringLiteral("RPI_Wired_Keyboard_99")}));

    CHECK(keyboardWith(fixture.path()).isEmpty());
}

TEST_CASE("Some other keyboard is not mistaken for a Pi one",
          "[imagewriter][keyboard]")
{
    // Anybody's USB keyboard shows up here. Only Raspberry Pi's own has a
    // country baked into its name, so everything else has to leave the
    // layout alone rather than matching on a number found in it.
    REQUIRE_KEYBOARD_HARNESS();

    // One fixture per name, so each stands on its own: the scan keeps the
    // last match it finds, and a name that would match is no use as a row if
    // some other entry sorts after it.
    //
    // The second one carries the weight. Its first run of digits is 2, which
    // is inside the country table -- so matching digits anywhere in the name,
    // rather than only after RPI_Wired_Keyboard_, would put a French layout
    // on somebody's Keychron.
    const QStringList names = {
        QStringLiteral("usb-Logitech_USB_Keyboard-event-kbd"),
        QStringLiteral("usb-Keychron_K2-event-kbd"),
        QStringLiteral("usb-Some_Vendor_Model_2000-event-kbd"),
        QStringLiteral("usb-Dell_KB216_Wired_Keyboard-event-kbd"),
    };

    for (const QString& name : names) {
        QTemporaryDir one;
        REQUIRE(one.isValid());
        REQUIRE(buildInputFixture(one.path(), {name}));
        INFO("node: " << name.toStdString());
        CHECK(keyboardWith(one.path()).isEmpty());
    }
}

TEST_CASE("No keyboard at all picks no layout", "[imagewriter][keyboard]")
{
    // Headless, or nothing plugged in yet. The by-id directory may not even
    // exist, which is the case on any machine without one.
    REQUIRE_KEYBOARD_HARNESS();

    QTemporaryDir empty;
    REQUIRE(empty.isValid());
    CHECK(keyboardWith(empty.path()).isEmpty());

    QTemporaryDir emptyByeId;
    REQUIRE(emptyByeId.isValid());
    REQUIRE(buildInputFixture(emptyByeId.path(), {}));
    CHECK(keyboardWith(emptyByeId.path()).isEmpty());
}
#endif // KEYBOARD_PROBE_BINARY

// ══════════════════════════════════════════════════════════════
// What the chosen OS says it can do
//
// The capability list arrives with each entry in the OS list and decides
// which customisation the wizard offers: whether the interfaces step appears
// at all, whether secure boot is on the menu, whether Pi Connect is. A
// capability that goes missing is an option the user cannot reach with
// nothing on screen explaining its absence; one that appears wrongly is a
// setting they configure and which is then silently dropped.
//
// Two spellings reach the setter -- a JSON array, and a comma-separated
// string for repository files written by hand -- and they are meant to mean
// the same thing.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Capabilities can be written as JSON or as a list", "[imagewriter][caps]")
{
    ImageWriter writer(nullptr);

    writer.setSWCapabilitiesList(QStringLiteral("[\"i2c\",\"spi\",\"usb_otg\"]"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("spi")));
    CHECK(writer.checkSWCapability(QStringLiteral("usb_otg")));
    CHECK_FALSE(writer.checkSWCapability(QStringLiteral("onewire")));

    // The same thing, comma separated, which is what a repository file
    // maintained by hand tends to carry.
    writer.setSWCapabilitiesList(QStringLiteral("i2c,spi,usb_otg"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("spi")));
    CHECK(writer.checkSWCapability(QStringLiteral("usb_otg")));
    CHECK_FALSE(writer.checkSWCapability(QStringLiteral("onewire")));
}

TEST_CASE("Choosing another OS forgets the last one's capabilities",
          "[imagewriter][caps]")
{
    // The list is replaced, not added to. Left to accumulate, an OS would
    // offer whatever the one looked at before it could do -- and the setting
    // would be written into an image that cannot honour it.
    ImageWriter writer(nullptr);

    writer.setSWCapabilitiesList(QStringLiteral("[\"i2c\"]"));
    REQUIRE(writer.checkSWCapability(QStringLiteral("i2c")));

    writer.setSWCapabilitiesList(QStringLiteral("[\"spi\"]"));
    CHECK(writer.checkSWCapability(QStringLiteral("spi")));
    CHECK_FALSE(writer.checkSWCapability(QStringLiteral("i2c")));

    // And "[]" is how the OS step clears it when nothing is chosen.
    writer.setSWCapabilitiesList(QStringLiteral("[]"));
    CHECK_FALSE(writer.checkSWCapability(QStringLiteral("spi")));
}

TEST_CASE("Capabilities are matched whatever the case", "[imagewriter][caps]")
{
    // Repository files are written by people. The names in the code are
    // lower case, so a list that shouts has to match anyway rather than
    // taking the option away.
    ImageWriter writer(nullptr);

    writer.setSWCapabilitiesList(QStringLiteral("[\"I2C\",\"Usb_Otg\"]"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("usb_otg")));

    writer.setSWCapabilitiesList(QStringLiteral("I2C,Usb_Otg"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("usb_otg")));
}

TEST_CASE("Stray spaces in a capability list do not lose the capability",
          "[imagewriter][caps]")
{
    // A list written with spaces after the commas, or inside the quotes, is
    // the same list. Dropping one over whitespace takes a customisation
    // option off the screen with nothing to say why.
    ImageWriter writer(nullptr);

    writer.setSWCapabilitiesList(QStringLiteral("i2c , spi"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("spi")));

    writer.setSWCapabilitiesList(QStringLiteral("[\" i2c \",\"spi \"]"));
    CHECK(writer.checkSWCapability(QStringLiteral("i2c")));
    CHECK(writer.checkSWCapability(QStringLiteral("spi")));

    // And the hardware list, which arrives the same way and decides which
    // board features are offered.
    writer.setHWCapabilitiesList(QJsonArray{QStringLiteral(" nvme"),
                                            QStringLiteral("USB-Boot ")});
    CHECK(writer.checkHWCapability(QStringLiteral("nvme")));
    CHECK(writer.checkHWCapability(QStringLiteral("usb-boot")));
}

TEST_CASE("A capability list that is neither form offers nothing",
          "[imagewriter][caps]")
{
    // Not a JSON array and not a list: whatever it is, it must not be read
    // as granting a capability. Offering an option the image cannot honour
    // is worse than offering none.
    ImageWriter writer(nullptr);

    for (const QString& junk : {QStringLiteral("{\"i2c\":true}"),
                                QStringLiteral("<capabilities/>"),
                                QStringLiteral(""),
                                QStringLiteral("   ")}) {
        INFO("list: " << junk.toStdString());
        writer.setSWCapabilitiesList(junk);
        CHECK_FALSE(writer.checkSWCapability(QStringLiteral("i2c")));
        CHECK_FALSE(writer.checkSWCapability(QStringLiteral("spi")));
    }
}

// ══════════════════════════════════════════════════════════════
// After a Compute Module has been bootstrapped
//
// A CM plugged in over USB comes up in rpiboot mode, and Imager sideloads
// firmware to get it into fastboot mode where it exposes its storage. Drive
// scanning is paused for the duration, because the scan and the sideload
// both talk to the same device over libusb.
//
// What happens when the bootstrap ends is bookkeeping, and it is the part
// that shows: scanning has to come back on, and the poll has to start
// looking for fastboot storage, or the board the user just watched get
// bootstrapped never appears in the list. Failure has to do the same --
// otherwise a bootstrap that went wrong leaves the drive list frozen and
// empty, and the user cannot even choose a different card.
// ══════════════════════════════════════════════════════════════

namespace {

class BootstrapWriter : public ImageWriter
{
public:
    BootstrapWriter() : ImageWriter(nullptr) {}
    using ImageWriter::onBootstrapComplete;
    using ImageWriter::onBootstrapError;
};

} // namespace

TEST_CASE("A finished bootstrap starts the drive scan looking for fastboot storage",
          "[imagewriter][bootstrap]")
{
    BootstrapWriter writer;
    DriveListModel *drives = writer.getDriveList();
    REQUIRE(drives != nullptr);

    // As the kickoff leaves things: paused, and not yet looking for fastboot
    // devices.
    drives->pausePolling();
    drives->setFastbootScanEnabled(false);
    REQUIRE(drives->scanMode() == DriveListModelPollThread::ScanMode::Paused);
    // Driven away from the expected answer first: the debug settings can
    // leave fastboot scanning already on, in which case the check below
    // would hold whether or not the handler did anything.
    REQUIRE_FALSE(drives->fastbootScanEnabled());

    writer.onBootstrapComplete(QStringLiteral("1.4"), QStringLiteral("fastboot-1"));

    CHECK(drives->scanMode() != DriveListModelPollThread::ScanMode::Paused);
    // And it is now looking for what the board has just become.
    CHECK(drives->fastbootScanEnabled());
}

TEST_CASE("A bootstrap that fails does not leave the drive list frozen",
          "[imagewriter][bootstrap]")
{
    // The sticky one. Scanning stays paused, the list stays empty, and there
    // is nothing on screen to connect that to the bootstrap having failed --
    // the user cannot even fall back to writing an SD card.
    BootstrapWriter writer;
    DriveListModel *drives = writer.getDriveList();
    REQUIRE(drives != nullptr);

    drives->pausePolling();
    REQUIRE(drives->scanMode() == DriveListModelPollThread::ScanMode::Paused);

    writer.onBootstrapError(QStringLiteral("1.4"),
                            QStringLiteral("device went away mid-sideload"));

    CHECK(drives->scanMode() != DriveListModelPollThread::ScanMode::Paused);
}

// ══════════════════════════════════════════════════════════════
// When the write cannot even be set up
//
// Building the writing thread allocates the ring buffers, and on a small
// machine with a large image that allocation can fail. Two handlers turn
// that into something on screen, and neither had ever run.
//
// Both have to do the same two things: say something the user can act on,
// and leave the writer able to try again. A failure that left the state at
// Preparing would have startWrite()'s re-entry guard reject every attempt
// afterwards -- the Write button would stop doing anything at all, with no
// error and no progress, and only restarting the application would clear it.
// ══════════════════════════════════════════════════════════════

namespace {

class SetupFailureWriter : public ImageWriter
{
public:
    SetupFailureWriter() : ImageWriter(nullptr) {}
    using ImageWriter::_handleMemoryAllocationFailure;
    using ImageWriter::_handleSetupException;
};

} // namespace

TEST_CASE("Running out of memory says so, and says what to do about it",
          "[imagewriter][setupfail]")
{
    SetupFailureWriter writer;
    UiLog log(&writer);

    writer._handleMemoryAllocationFailure("std::bad_alloc");

    REQUIRE(log.errors.size() == 1);
    const std::string said = log.errors[0].toStdString();
    INFO(said);

    // Named as memory rather than as a generic failure: on a Pi writing a
    // large image this is the likeliest way setup fails, and it is the one
    // the user can actually do something about.
    CHECK_THAT(said, ContainsSubstring("memory"));
    CHECK_THAT(said, ContainsSubstring("closing other applications"));
    // The exception's own text is carried, since it is the only clue to
    // which allocation it was.
    CHECK_THAT(said, ContainsSubstring("std::bad_alloc"));
}

TEST_CASE("A failed setup leaves the Write button usable",
          "[imagewriter][setupfail]")
{
    // The state machine half. startWrite() refuses to re-enter while a write
    // is in progress, and Preparing counts as in progress -- so a setup
    // failure that did not move the state on would wedge the application
    // until it was restarted.
    SetupFailureWriter writer;

    // Read through the property, which is the same route QML takes -- the
    // getter itself is private.
    const auto reportedState = [](ImageWriter &w) {
        return w.property("writeState").value<ImageWriter::WriteState>();
    };

    writer._handleMemoryAllocationFailure("std::bad_alloc");
    CHECK(reportedState(writer) == ImageWriter::WriteState::Failed);

    SetupFailureWriter other;
    other._handleSetupException("something else went wrong");
    CHECK(reportedState(other) == ImageWriter::WriteState::Failed);
}

TEST_CASE("Any other setup failure is still reported", "[imagewriter][setupfail]")
{
    // Not every failure building the thread is an allocation. Whatever it
    // was, the user has to be told rather than left watching a screen that
    // never moves.
    SetupFailureWriter writer;
    UiLog log(&writer);

    writer._handleSetupException("could not open device");

    REQUIRE(log.errors.size() == 1);
    const std::string said = log.errors[0].toStdString();
    INFO(said);
    CHECK_THAT(said, ContainsSubstring("could not open device"));
    // And not mislabelled as the memory case, which would send the user off
    // closing applications for no reason.
    CHECK_THAT(said, !ContainsSubstring("insufficient memory"));
}

int main(int argc, char *argv[])
{
    // Offscreen: ImageWriter asks QGuiApplication for the platform name, and
    // these run with no display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    initAppResources();
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("image_writer_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}

// ══════════════════════════════════════════════════════════════
// Filtering the OS list for the chosen board
//
// Each OS entry may carry a "devices" list of board tags it runs on. The
// chooser is filtered against the tags of the selected hardware, and both
// directions of error are user-facing: too strict and the image the user
// came for is not in the list at all, with nothing to say why; too loose and
// they are offered an image that will not boot on their board.
//
// Entries with no "devices" at all are the interesting case, and what
// happens to them is what the inclusive flag decides.
// ══════════════════════════════════════════════════════════════

namespace {

// Reaches onOsListFetchComplete(), a protected slot, so a list can be handed
// in without a network fetch.
class FeedableImageWriter : public ImageWriter
{
public:
    FeedableImageWriter() : ImageWriter(nullptr) {}

    // A top-level fetch is one whose URL matches the configured repository.
    void feedOsList(const QByteArray &json)
    {
        onOsListFetchComplete(json, osListUrl(), osListUrl());
    }

    // Anything else is treated as the contents of a category that declared
    // this URL in its subitems_url.
    void feedSubList(const QByteArray &json, const QUrl &url)
    {
        onOsListFetchComplete(json, url, url);
    }

    void reportOsListFailure(const QString &message)
    {
        onOsListFetchError(message, osListUrl());
    }
};

QByteArray taggedOsList()
{
    return QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Pi 5 only",   "devices": ["pi5-64bit"] },
            { "name": "Zero 2 only", "devices": ["pi-zero2-64bit"] },
            { "name": "Both",        "devices": ["pi5-64bit", "pi-zero2-64bit"] },
            { "name": "Untagged" }
        ]
    })JSON");
}

QStringList namesIn(const QJsonDocument &doc)
{
    QStringList names;
    for (const auto &v : doc.object().value(QStringLiteral("os_list")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

} // namespace

TEST_CASE("Only images for the chosen board are offered", "[imagewriter][oslist]")
{
    FeedableImageWriter writer;
    writer.feedOsList(taggedOsList());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, /*inclusive=*/false);

    const QStringList names = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());

    CHECK(names.contains(QStringLiteral("Pi 5 only")));
    CHECK(names.contains(QStringLiteral("Both")));
    CHECK_FALSE(names.contains(QStringLiteral("Zero 2 only")));
}

TEST_CASE("Exclusive filtering drops images that name no board",
          "[imagewriter][oslist]")
{
    // Exclusive: an entry has to say it supports this board. An untagged
    // entry makes no such claim, so it is left out.
    FeedableImageWriter writer;
    writer.feedOsList(taggedOsList());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    CHECK_FALSE(namesIn(writer.getFilteredOSlistDocument())
                    .contains(QStringLiteral("Untagged")));
}

TEST_CASE("Inclusive filtering keeps images that name no board",
          "[imagewriter][oslist]")
{
    // Inclusive: an entry without a devices list is assumed to run anywhere,
    // which is what keeps generic images in the chooser.
    FeedableImageWriter writer;
    writer.feedOsList(taggedOsList());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, true);

    const QStringList names = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK(names.contains(QStringLiteral("Untagged")));
    CHECK_FALSE(names.contains(QStringLiteral("Zero 2 only")));
}

TEST_CASE("A board with no tags at all is offered everything",
          "[imagewriter][oslist]")
{
    // No filter set: the chooser must not silently empty itself.
    FeedableImageWriter writer;
    writer.feedOsList(taggedOsList());

    const QStringList names = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK(names.contains(QStringLiteral("Pi 5 only")));
    CHECK(names.contains(QStringLiteral("Zero 2 only")));
    CHECK(names.contains(QStringLiteral("Untagged")));
}

TEST_CASE("Filtering reaches into nested categories", "[imagewriter][oslist]")
{
    // The real list nests images under categories. A category has to be
    // filtered by what is inside it, and one left with nothing must not
    // remain as an empty heading the user can open onto nothing.
    FeedableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Category", "subitems": [
                { "name": "Inner pi5",    "devices": ["pi5-64bit"] },
                { "name": "Inner zero2",  "devices": ["pi-zero2-64bit"] }
            ]},
            { "name": "Zero-only category", "subitems": [
                { "name": "Only zero2", "devices": ["pi-zero2-64bit"] }
            ]}
        ]
    })JSON"));
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    const QJsonDocument doc = writer.getFilteredOSlistDocument();
    const QStringList names = namesIn(doc);
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());

    CHECK(names.contains(QStringLiteral("Category")));
    CHECK_FALSE(names.contains(QStringLiteral("Zero-only category")));

    // ...and the category that survived carries only the matching image.
    for (const auto &v : doc.object().value(QStringLiteral("os_list")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() != QLatin1String("Category"))
            continue;
        QStringList inner;
        for (const auto &sv : o.value(QStringLiteral("subitems")).toArray())
            inner << sv.toObject().value(QStringLiteral("name")).toString();
        INFO("inner: " << inner.join(QStringLiteral(", ")).toStdString());
        CHECK(inner.contains(QStringLiteral("Inner pi5")));
        CHECK_FALSE(inner.contains(QStringLiteral("Inner zero2")));
    }
}

// Build a chain of categories nested `depth` deep, with one matching image
// at the bottom. Mirrors the shape of the real list, where categories hold
// categories, but taken further than any real one goes.
static QByteArray nestedOsList(int depth)
{
    QString inner = QStringLiteral(
        R"({ "name": "Deep image", "devices": ["pi5-64bit"] })");
    for (int i = depth; i > 0; --i) {
        inner = QStringLiteral(R"({ "name": "Level %1", "subitems": [ %2 ] })")
                    .arg(i).arg(inner);
    }
    return QStringLiteral(
               R"({ "imager": {}, "os_list": [ %1 ] })").arg(inner).toUtf8();
}

TEST_CASE("A deeply nested list is still filtered to the bottom",
          "[imagewriter][oslist]")
{
    // Just inside the recursion limit. The top level counts as depth 1, so
    // fifteen nested categories put the image at the last level the filter
    // will walk.
    FeedableImageWriter writer;
    writer.feedOsList(nestedOsList(15));
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    const QJsonDocument doc = writer.getFilteredOSlistDocument();
    const QStringList top = namesIn(doc);
    INFO("offered: " << top.join(QStringLiteral(", ")).toStdString());
    CHECK(top.contains(QStringLiteral("Level 1")));
}

TEST_CASE("A list nested past the limit loses the part below it, not the app",
          "[imagewriter][oslist]")
{
    // The list is fetched over the network, so its shape is not ours to
    // trust. Past the limit the filter gives up on that subtree and returns
    // nothing for it -- which empties the categories above it, so the whole
    // chain drops out rather than the recursion running away.
    FeedableImageWriter writer;
    writer.feedOsList(nestedOsList(40));
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    QJsonDocument doc;
    REQUIRE_NOTHROW(doc = writer.getFilteredOSlistDocument());
    const QStringList top = namesIn(doc);
    INFO("offered: " << top.join(QStringLiteral(", ")).toStdString());

    CHECK_FALSE(top.contains(QStringLiteral("Level 1")));
    CHECK(top.contains(QStringLiteral("Erase")));
    CHECK(top.contains(QStringLiteral("Use custom")));
}

TEST_CASE("A list nested past the limit leaves the rest of the list alone",
          "[imagewriter][oslist]")
{
    // The guard drops the offending subtree. Anything beside it at the top
    // level is unaffected, so one malformed entry does not empty the chooser.
    FeedableImageWriter writer;

    QString deep = QStringLiteral(
        R"({ "name": "Deep image", "devices": ["pi5-64bit"] })");
    for (int i = 40; i > 0; --i)
        deep = QStringLiteral(R"({ "name": "D%1", "subitems": [ %2 ] })")
                   .arg(i).arg(deep);

    const QByteArray json = QStringLiteral(R"({
        "imager": {},
        "os_list": [
            { "name": "Normal image", "devices": ["pi5-64bit"] },
            %1
        ]
    })").arg(deep).toUtf8();

    writer.feedOsList(json);
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    const QStringList top = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << top.join(QStringLiteral(", ")).toStdString());
    CHECK(top.contains(QStringLiteral("Normal image")));
    CHECK_FALSE(top.contains(QStringLiteral("D1")));
}

TEST_CASE("The built-in entries survive filtering", "[imagewriter][oslist]")
{
    // Erase and the custom-image entry are appended after filtering, so no
    // combination of tags can leave the user without them.
    FeedableImageWriter writer;
    writer.feedOsList(taggedOsList());
    writer.setHWFilterList(QJsonArray{QStringLiteral("no-such-board")}, false);

    const QStringList names = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Sizes the user reads
//
// formatSize() produces the figure in "The image requires at least %1", and
// the capacity of every entry in the drive chooser. A wrong unit or a wrong
// rounding is how somebody picks a card believing it is big enough.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Sizes are formatted in binary units", "[imagewriter][format]")
{
    ImageWriter w(nullptr);
    CHECK(w.formatSize(0, 0) == QStringLiteral("0 B"));
    CHECK(w.formatSize(512, 0) == QStringLiteral("512 B"));
    CHECK(w.formatSize(1024, 0) == QStringLiteral("1 KB"));
    CHECK(w.formatSize(1024ull * 1024, 0) == QStringLiteral("1 MB"));
    CHECK(w.formatSize(1024ull * 1024 * 1024, 0) == QStringLiteral("1 GB"));
    CHECK(w.formatSize(1024ull * 1024 * 1024 * 1024, 0) == QStringLiteral("1 TB"));
}

TEST_CASE("A size just below a unit boundary keeps the smaller unit",
          "[imagewriter][format]")
{
    // 1023 bytes is not "1 KB"; rounding it up would let a card look larger
    // than it is right at the boundary that matters.
    ImageWriter w(nullptr);
    CHECK(w.formatSize(1023, 0) == QStringLiteral("1023 B"));
    CHECK_THAT(w.formatSize(1024ull * 1024 - 1, 1).toStdString(),
               ContainsSubstring("KB"));
}

TEST_CASE("Decimal places are honoured", "[imagewriter][format]")
{
    ImageWriter w(nullptr);
    const QString oneAndAHalf = w.formatSize(1536ull * 1024 * 1024, 1);
    INFO("1.5 GB rendered as: " << oneAndAHalf.toStdString());
    CHECK_THAT(oneAndAHalf.toStdString(), ContainsSubstring("1.5"));
    CHECK_THAT(oneAndAHalf.toStdString(), ContainsSubstring("GB"));
}

TEST_CASE("A realistic card size reads sensibly", "[imagewriter][format]")
{
    // What a 32 GB card actually reports.
    ImageWriter w(nullptr);
    const QString s = w.formatSize(31914983424ull, 1);
    INFO("32 GB card rendered as: " << s.toStdString());
    CHECK_THAT(s.toStdString(), ContainsSubstring("GB"));
    CHECK_THAT(s.toStdString(), ContainsSubstring("29."));
}

// ══════════════════════════════════════════════════════════════
// Settings
// ══════════════════════════════════════════════════════════════

TEST_CASE("Settings round-trip", "[imagewriter][settings]")
{
    ImageWriter w(nullptr);
    w.setSetting(QStringLiteral("test_string_key"), QStringLiteral("a value"));
    w.setSetting(QStringLiteral("test_bool_key"), true);

    CHECK(w.getStringSetting(QStringLiteral("test_string_key")) == QStringLiteral("a value"));
    CHECK(w.getBoolSetting(QStringLiteral("test_bool_key")));
}

TEST_CASE("An unset setting reads as empty rather than throwing",
          "[imagewriter][settings]")
{
    // QML asks for settings that may never have been written.
    ImageWriter w(nullptr);
    CHECK(w.getStringSetting(QStringLiteral("never_written_key_9f2a")).isEmpty());
    CHECK_FALSE(w.getBoolSetting(QStringLiteral("never_written_key_9f2a")));
}

TEST_CASE("The debug I/O toggles persist", "[imagewriter][settings]")
{
    // These change how the write is performed, so a toggle that silently
    // fails to stick means the user is not running what they selected.
    ImageWriter w(nullptr);

    w.setDebugDirectIO(true);
    CHECK(w.getDebugDirectIO());
    w.setDebugDirectIO(false);
    CHECK_FALSE(w.getDebugDirectIO());

    w.setDebugPeriodicSync(true);
    CHECK(w.getDebugPeriodicSync());
    w.setDebugPeriodicSync(false);
    CHECK_FALSE(w.getDebugPeriodicSync());
}

// ══════════════════════════════════════════════════════════════
// Raspberry Pi Connect organisation registration
// ══════════════════════════════════════════════════════════════

TEST_CASE("Connect registration can be set, read back and cleared",
          "[imagewriter][connect]")
{
    // Clearing has to actually clear. A stale API key left behind would
    // enrol the next device somebody images into an organisation they have
    // nothing to do with.
    ImageWriter w(nullptr);
    CHECK_FALSE(w.hasConnectOrgRegistration());

    w.setConnectOrgRegistration(QStringLiteral("test-api-key-abc123"),
                                QStringLiteral("Lab bench"));
    CHECK(w.hasConnectOrgRegistration());
    CHECK(w.getConnectOrgDescription() == QStringLiteral("Lab bench"));

    w.clearConnectOrgRegistration();
    CHECK_FALSE(w.hasConnectOrgRegistration());
}

TEST_CASE("The Connect description can be changed on its own",
          "[imagewriter][connect]")
{
    ImageWriter w(nullptr);
    w.setConnectOrgRegistration(QStringLiteral("key"), QStringLiteral("First"));
    w.setConnectOrgDescription(QStringLiteral("Second"));
    CHECK(w.getConnectOrgDescription() == QStringLiteral("Second"));
    CHECK(w.hasConnectOrgRegistration());
    w.clearConnectOrgRegistration();
}

// ══════════════════════════════════════════════════════════════
// Capability gating
//
// These decide which options the UI offers at all. Answering yes for a
// capability the hardware lacks puts a control in front of the user that
// cannot work; answering no for one it has hides a feature they paid for.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An unknown capability is not claimed", "[imagewriter][capability]")
{
    ImageWriter w(nullptr);
    CHECK_FALSE(w.checkHWCapability(QStringLiteral("no-such-capability-9f2a")));
    CHECK_FALSE(w.checkSWCapability(QStringLiteral("no-such-capability-9f2a")));
}

TEST_CASE("A declared hardware capability is recognised", "[imagewriter][capability]")
{
    ImageWriter w(nullptr);
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("nvme"), QStringLiteral("usb-boot")});

    CHECK(w.checkHWCapability(QStringLiteral("nvme")));
    CHECK(w.checkHWCapability(QStringLiteral("usb-boot")));
    CHECK_FALSE(w.checkHWCapability(QStringLiteral("something-else")));
}

TEST_CASE("Replacing the capability list drops what was there before",
          "[imagewriter][capability]")
{
    // Switching board in the chooser replaces the list; a capability left
    // over from the previous selection would offer a feature this board
    // does not have.
    ImageWriter w(nullptr);
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("nvme")});
    REQUIRE(w.checkHWCapability(QStringLiteral("nvme")));

    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("usb-boot")});
    CHECK(w.checkHWCapability(QStringLiteral("usb-boot")));
    CHECK_FALSE(w.checkHWCapability(QStringLiteral("nvme")));
}

TEST_CASE("The hardware filter's inclusive flag is reported back",
          "[imagewriter][capability]")
{
    ImageWriter w(nullptr);
    w.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, true);
    CHECK(w.getHWFilterListInclusive());
    w.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);
    CHECK_FALSE(w.getHWFilterListInclusive());
}

// ══════════════════════════════════════════════════════════════
// Lists the customisation dialog is built from
// ══════════════════════════════════════════════════════════════

TEST_CASE("The customisation dialog has locale data to offer",
          "[imagewriter][locale]")
{
    // Empty here means empty dropdowns: no timezone, no country, no keyboard
    // layout, and a card customised with none of them set.
    ImageWriter w(nullptr);

    CHECK_FALSE(w.getTimezoneList().isEmpty());
    CHECK_FALSE(w.getCountryList().isEmpty());
    CHECK_FALSE(w.getKeymapLayoutList().isEmpty());
}

TEST_CASE("Timezones look like timezones", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList zones = w.getTimezoneList();
    REQUIRE_FALSE(zones.isEmpty());
    CHECK(zones.contains(QStringLiteral("Europe/London")));
}

TEST_CASE("Reading a file that is not there yields nothing",
          "[imagewriter][files]")
{
    ImageWriter w(nullptr);
    CHECK(w.readFileContents(QStringLiteral("/nonexistent-9f2a/nothing.txt")).isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Credentials written onto the card
//
// These two produce the wifi PSK and the account password hash that end up
// in the customisation written to the boot partition. Neither failure is
// visible until the board has been flashed and booted: a wrong PSK is a Pi
// that never joins the network, and a wrong hash is one nobody can log into.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A wifi passphrase is derived to the documented PSK",
          "[imagewriter][wifi]")
{
    // The IEEE 802.11i test vector. Checking against a published value
    // rather than against whatever this code happens to produce is the whole
    // point: PBKDF2-HMAC-SHA1, 4096 iterations, the SSID as salt, 256 bits.
    ImageWriter w(nullptr);
    const QString psk = w.deriveWifiPsk(QStringLiteral("IEEE"), QStringLiteral("password"));
    INFO("derived: " << psk.toStdString());
    CHECK(psk.compare(
              QStringLiteral("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"),
              Qt::CaseInsensitive) == 0);
}

TEST_CASE("The SSID is the salt, so the same passphrase differs per network",
          "[imagewriter][wifi]")
{
    // Salting with the SSID is what stops one derivation being reusable on
    // another network. If the salt were dropped, every card with the same
    // passphrase would carry an identical PSK.
    ImageWriter w(nullptr);
    const QString a = w.deriveWifiPsk(QStringLiteral("network-one"), QStringLiteral("samepass1"));
    const QString b = w.deriveWifiPsk(QStringLiteral("network-two"), QStringLiteral("samepass1"));
    CHECK_FALSE(a.isEmpty());
    CHECK(a != b);
}

TEST_CASE("An already-hexadecimal PSK is passed through untouched",
          "[imagewriter][wifi]")
{
    // A 64-character key is the PSK itself, not a passphrase. Running it
    // through the derivation again would produce a key that works nowhere.
    ImageWriter w(nullptr);
    const QString raw(64, QLatin1Char('a'));
    CHECK(w.deriveWifiPsk(QStringLiteral("somewhere"), raw) == raw);
}

TEST_CASE("A too-short passphrase is not derived", "[imagewriter][wifi]")
{
    // WPA requires at least eight characters; anything shorter is not a
    // passphrase and must not be silently turned into a key that looks
    // valid.
    ImageWriter w(nullptr);
    const QString shortPass = QStringLiteral("abc");
    CHECK(w.deriveWifiPsk(QStringLiteral("somewhere"), shortPass) == shortPass);
}

TEST_CASE("An empty passphrase derives nothing", "[imagewriter][wifi]")
{
    ImageWriter w(nullptr);
    CHECK(w.deriveWifiPsk(QStringLiteral("somewhere"), QString()).isEmpty());
}

TEST_CASE("A trailing newline in a passphrase does not change the key",
          "[imagewriter][wifi]")
{
    // Pasted credentials routinely carry one. Deriving from the newline as
    // well produces a key that differs from every other device on the
    // network, and the board simply never associates.
    ImageWriter w(nullptr);
    const QString clean = w.deriveWifiPsk(QStringLiteral("net"), QStringLiteral("passphrase1"));
    const QString pasted = w.deriveWifiPsk(QStringLiteral("net"), QStringLiteral("passphrase1\n"));
    const QString crlf = w.deriveWifiPsk(QStringLiteral("net"), QStringLiteral("passphrase1\r\n"));
    CHECK_FALSE(clean.isEmpty());
    CHECK(pasted == clean);
    CHECK(crlf == clean);
}

TEST_CASE("A user password is hashed into a crypt string", "[imagewriter][password]")
{
    // What lands in the customisation is a crypt(3) hash, never the
    // plaintext. The prefix identifies the scheme so the target OS knows how
    // to verify it.
    ImageWriter w(nullptr);
    const QString hash = w.hashUserPassword(QStringLiteral("hunter2"));
    INFO("hash: " << hash.toStdString());

    REQUIRE_FALSE(hash.isEmpty());
    CHECK_FALSE(hash.contains(QStringLiteral("hunter2")));
    CHECK(hash.startsWith(QLatin1Char('$')));
}

TEST_CASE("Hashing the same password twice gives different hashes",
          "[imagewriter][password]")
{
    // A fresh salt each time. Identical output would mean the salt is fixed,
    // and every card imaged anywhere would share it.
    ImageWriter w(nullptr);
    const QString a = w.hashUserPassword(QStringLiteral("hunter2"));
    const QString b = w.hashUserPassword(QStringLiteral("hunter2"));
    CHECK_FALSE(a.isEmpty());
    CHECK(a != b);
}

TEST_CASE("An empty password hashes to nothing", "[imagewriter][password]")
{
    // Rather than to the hash of an empty string, which would be an account
    // with a password of "".
    ImageWriter w(nullptr);
    CHECK(w.hashUserPassword(QString()).isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Customisation that survives between runs
// ══════════════════════════════════════════════════════════════

TEST_CASE("Persisted customisation settings round-trip",
          "[imagewriter][customisation]")
{
    ImageWriter w(nullptr);
    w.clearSavedCustomisationSettings();

    w.setPersistedCustomisationSetting(QStringLiteral("hostname"),
                                       QStringLiteral("test-pi"));
    w.setPersistedCustomisationSetting(QStringLiteral("sshEnabled"), true);

    const QVariantMap saved = w.getSavedCustomisationSettings();
    CHECK(saved.value(QStringLiteral("hostname")).toString() == QStringLiteral("test-pi"));
    CHECK(saved.value(QStringLiteral("sshEnabled")).toBool());

    w.clearSavedCustomisationSettings();
}

TEST_CASE("Clearing saved customisation really clears it",
          "[imagewriter][customisation]")
{
    // Somebody imaging a card for another person expects "clear" to mean
    // their wifi passphrase and password hash are gone, not hidden.
    ImageWriter w(nullptr);
    w.setPersistedCustomisationSetting(QStringLiteral("hostname"),
                                       QStringLiteral("private-name"));
    REQUIRE_FALSE(w.getSavedCustomisationSettings().isEmpty());

    w.clearSavedCustomisationSettings();
    CHECK(w.getSavedCustomisationSettings().isEmpty());
}

TEST_CASE("A single persisted setting can be removed on its own",
          "[imagewriter][customisation]")
{
    ImageWriter w(nullptr);
    w.clearSavedCustomisationSettings();
    w.setPersistedCustomisationSetting(QStringLiteral("keep"), QStringLiteral("yes"));
    w.setPersistedCustomisationSetting(QStringLiteral("drop"), QStringLiteral("no"));

    w.removePersistedCustomisationSetting(QStringLiteral("drop"));

    const QVariantMap saved = w.getSavedCustomisationSettings();
    CHECK(saved.contains(QStringLiteral("keep")));
    CHECK_FALSE(saved.contains(QStringLiteral("drop")));

    w.clearSavedCustomisationSettings();
}

// ══════════════════════════════════════════════════════════════
// Whether customisation is offered at all
//
// These three read the init_format the OS entry declared, and between them
// decide which parts of the customisation dialog appear. Wrong in one
// direction and a user cannot set a hostname on an image that supports it;
// wrong in the other and they fill in a form whose contents the image will
// silently ignore.
// ══════════════════════════════════════════════════════════════

namespace {

// setSrc() carries init_format as its eighth argument, which is how the
// selected OS entry tells the backend what it supports.
void selectImageWithFormat(ImageWriter &w, const QByteArray &initFormat,
                           const QString &releaseDate = QString())
{
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
             /*downloadLen=*/0, /*extrLen=*/0, /*expectedHash=*/QByteArray(),
             /*multifilesinzip=*/false, /*parentcategory=*/QString(),
             /*osname=*/QStringLiteral("Test OS"), initFormat, releaseDate);
}

} // namespace

TEST_CASE("An image declaring no init format supports no customisation",
          "[imagewriter][customisation]")
{
    ImageWriter w(nullptr);
    selectImageWithFormat(w, QByteArray());
    CHECK_FALSE(w.imageSupportsCustomization());
}

TEST_CASE("Each init format offers the right parts of the dialog",
          "[imagewriter][customisation]")
{
    struct Case {
        const char *format;
        bool customisation;
        bool ccRpi;
        bool interfaces;
    };

    const Case cases[] = {
        {"systemd",       true,  false, false},
        {"cloudinit",     true,  false, false},
        {"cloudinit-rpi", true,  true,  true },
        {"rpi-preseed",   true,  false, true },
    };

    for (const Case &c : cases) {
        ImageWriter w(nullptr);
        selectImageWithFormat(w, QByteArray(c.format));
        INFO("init_format: " << c.format);
        CHECK(w.imageSupportsCustomization() == c.customisation);
        CHECK(w.imageSupportsCcRpi() == c.ccRpi);
        CHECK(w.imageSupportsInterfaceCustomisation() == c.interfaces);
    }
}

TEST_CASE("Choosing another image updates what it supports",
          "[imagewriter][customisation]")
{
    // The dialog is rebuilt when the selection changes; a stale answer here
    // leaves controls on screen that the newly chosen image ignores.
    ImageWriter w(nullptr);
    selectImageWithFormat(w, QByteArray("cloudinit-rpi"));
    REQUIRE(w.imageSupportsCcRpi());

    selectImageWithFormat(w, QByteArray("systemd"));
    CHECK_FALSE(w.imageSupportsCcRpi());
    CHECK(w.imageSupportsCustomization());

    selectImageWithFormat(w, QByteArray());
    CHECK_FALSE(w.imageSupportsCustomization());
}

TEST_CASE("Customisation is generated for each supported format",
          "[imagewriter][customisation]")
{
    // The three branches produce quite different files; what matters here is
    // that each is reached and none throws on a realistic settings map.
    for (const char *format : {"systemd", "cloudinit", "cloudinit-rpi", "rpi-preseed"}) {
        ImageWriter w(nullptr);
        selectImageWithFormat(w, QByteArray(format), QStringLiteral("2024-11-19"));

        QVariantMap settings;
        settings.insert(QStringLiteral("hostname"), QStringLiteral("test-pi"));
        settings.insert(QStringLiteral("sshEnabled"), true);
        settings.insert(QStringLiteral("username"), QStringLiteral("pi"));
        settings.insert(QStringLiteral("password"), QStringLiteral("hunter2"));
        settings.insert(QStringLiteral("wifiSSID"), QStringLiteral("mynet"));
        settings.insert(QStringLiteral("wifiPassword"), QStringLiteral("passphrase1"));
        settings.insert(QStringLiteral("wifiCountry"), QStringLiteral("GB"));
        settings.insert(QStringLiteral("timezone"), QStringLiteral("Europe/London"));
        settings.insert(QStringLiteral("keyboardLayout"), QStringLiteral("gb"));

        INFO("init_format: " << format);
        CHECK_NOTHROW(w.applyCustomisationFromSettings(settings));
    }
}

// ══════════════════════════════════════════════════════════════
// Reusing a saved password on a different OS
// ══════════════════════════════════════════════════════════════

TEST_CASE("A saved password hash is reusable when the scheme matches",
          "[imagewriter][password]")
{
    // Older images cannot verify a yescrypt hash. Getting this wrong locks
    // the user out of the card they just wrote, or makes them retype a
    // password that would have worked.
    ImageWriter w(nullptr);

    // Nothing saved is always fine.
    CHECK(w.savedUserPasswordUsableWithCurrentOs(QString()));

    // A traditional SHA-256 crypt hash is understood everywhere.
    CHECK(w.savedUserPasswordUsableWithCurrentOs(
        QStringLiteral("$5$rounds=5000$abcdefgh$0123456789abcdefghijklmnopqrstuvwxyzABCDEF012")));
}

TEST_CASE("A yescrypt hash is rejected for an image too old to verify it",
          "[imagewriter][password]")
{
    ImageWriter w(nullptr);
    const QString yescryptHash =
        QStringLiteral("$y$j9T$MPabcdefghijklmnop$0123456789abcdefghijklmnopqrstuvwxyzABCD");

    selectImageWithFormat(w, QByteArray("systemd"), QStringLiteral("2018-06-27"));
    const bool oldOs = w.savedUserPasswordUsableWithCurrentOs(yescryptHash);

    selectImageWithFormat(w, QByteArray("systemd"), QStringLiteral("2099-01-01"));
    const bool newOs = w.savedUserPasswordUsableWithCurrentOs(yescryptHash);

    INFO("old image accepts yescrypt: " << oldOs << "  new image: " << newOs);
    CHECK(newOs);
    CHECK_FALSE(oldOs);
}

// ══════════════════════════════════════════════════════════════
// Where the OS list comes from, and what the user is shown
// ══════════════════════════════════════════════════════════════

TEST_CASE("The OS list URL defaults to the shipped repository",
          "[imagewriter][repo]")
{
    ImageWriter w(nullptr);
    const QUrl url = w.osListUrl();
    INFO("default: " << url.toString().toStdString());
    CHECK(url.isValid());
    CHECK_FALSE(url.isEmpty());
    CHECK(url.scheme() == QStringLiteral("https"));
}

TEST_CASE("A custom OS list URL replaces the default", "[imagewriter][repo]")
{
    // Used by anyone running their own repository, and by the --repo flag.
    ImageWriter w(nullptr);
    const QUrl custom(QStringLiteral("https://mirror.example.com/os_list.json"));
    w.setCustomOsListUrl(custom);

    CHECK(w.osListUrl() == custom);
    CHECK_THAT(w.osListUrlForDisplay().toStdString(),
               ContainsSubstring("mirror.example.com"));
}

TEST_CASE("A local OS list file is shown as a path, not a URL",
          "[imagewriter][repo]")
{
    // PreferLocalFile: a file:// URL in the window title is noise; the path
    // is what the person who passed it recognises.
    ImageWriter w(nullptr);
    w.setCustomOsListUrl(QUrl::fromLocalFile(QStringLiteral("/tmp/my_os_list.json")));

    const QString shown = w.osListUrlForDisplay();
    INFO("shown: " << shown.toStdString());
    CHECK(shown == QStringLiteral("/tmp/my_os_list.json"));
}

TEST_CASE("The selected image's file name is what gets displayed",
          "[imagewriter][repo]")
{
    ImageWriter w(nullptr);
    CHECK(w.srcFileName().isEmpty());

    w.setSrc(QUrl(QStringLiteral(
        "https://downloads.raspberrypi.org/raspios/2024-11-19-raspios-bookworm.img.xz")));
    CHECK(w.srcFileName() == QStringLiteral("2024-11-19-raspios-bookworm.img.xz"));
}

TEST_CASE("The size shown prefers the decompressed size", "[imagewriter][repo]")
{
    // What matters to the user is how much of their card it will occupy,
    // not how much will come down the wire.
    ImageWriter w(nullptr);
    CHECK(w.getSelectedSourceSize() == 0);

    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
             /*downloadLen=*/1000, /*extrLen=*/5000);
    CHECK(w.getSelectedSourceSize() == 5000);
}

TEST_CASE("With no decompressed size the download size is shown instead",
          "[imagewriter][repo]")
{
    // Which is the streaming-compressed case: the extracted size is not
    // recorded in the archive, so the download size is the only figure
    // there is.
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.zst")),
             /*downloadLen=*/1234, /*extrLen=*/0);
    CHECK(w.getSelectedSourceSize() == 1234);
}

TEST_CASE("The reported version is the one built in", "[imagewriter][repo]")
{
    ImageWriter w(nullptr);
    const QString v = w.constantVersion();
    INFO("version: " << v.toStdString());
    CHECK_FALSE(v.isEmpty());
    // Whatever it is, it has to parse as a version or the update check
    // silently compares against nothing.
    CHECK(QVersionNumber::fromString(
              v.startsWith(QLatin1Char('v')) ? v.mid(1) : v).majorVersion() >= 0);
}

// ══════════════════════════════════════════════════════════════
// Expanding a category
//
// A category can declare a subitems_url instead of carrying its images
// inline; opening it fetches that URL and merges the result into the entry
// that asked for it. If the merge misses, the category opens onto nothing
// and there is no error to show for it.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A fetched sub-list is merged into the category that asked for it",
          "[imagewriter][oslist]")
{
    FeedableImageWriter writer;
    const QUrl subUrl(QStringLiteral("https://example.invalid/other.json"));

    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Inline",   "url": "https://example.invalid/a.img.xz" },
            { "name": "Deferred", "subitems_url": "https://example.invalid/other.json" }
        ]
    })JSON"));

    writer.feedSubList(QByteArray(R"JSON({
        "os_list": [
            { "name": "Fetched child", "url": "https://example.invalid/b.img.xz" }
        ]
    })JSON"), subUrl);

    const QJsonDocument doc = writer.getFilteredOSlistDocument();
    bool found = false;
    for (const auto &v : doc.object().value(QStringLiteral("os_list")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() != QLatin1String("Deferred"))
            continue;
        found = true;

        QStringList inner;
        for (const auto &sv : o.value(QStringLiteral("subitems")).toArray())
            inner << sv.toObject().value(QStringLiteral("name")).toString();
        INFO("children: " << inner.join(QStringLiteral(", ")).toStdString());
        CHECK(inner.contains(QStringLiteral("Fetched child")));

        // The pending URL is cleared, or opening the category again refetches
        // it and appends the same images a second time.
        CHECK_FALSE(o.contains(QStringLiteral("subitems_url")));
    }
    CHECK(found);
}

TEST_CASE("A repository naming an unusable sub-list URL still gives a chooser",
          "[imagewriter][oslist]")
{
    // The repository can be pointed anywhere, including at something whose
    // subitems_url is not a URL at all. Those entries are refused before a
    // fetch is attempted; what matters here is that the rest of the list
    // survives and the user is not left staring at nothing.
    FeedableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Good image", "devices": [] },
            { "name": "No scheme",  "subitems_url": "example.invalid/x.json" },
            { "name": "No host",    "subitems_url": "https:///x.json" },
            { "name": "Empty",      "subitems_url": "" }
        ]
    })JSON"));

    QJsonDocument doc;
    REQUIRE_NOTHROW(doc = writer.getFilteredOSlistDocument());
    const QStringList names = namesIn(doc);
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());

    CHECK(names.contains(QStringLiteral("Good image")));
    CHECK(names.contains(QStringLiteral("Erase")));
    CHECK(names.contains(QStringLiteral("Use custom")));
}

TEST_CASE("A sub-list spliced into a deeply nested category does not run away",
          "[imagewriter][oslist]")
{
    // The splice walks the same tree the filter does and stops at the same
    // depth. Past it the category's contents are replaced with nothing
    // rather than the recursion continuing, so a list shaped to nest
    // forever costs the entries below the limit and no more.
    FeedableImageWriter writer;

    QString inner = QStringLiteral(
        R"({ "name": "Deferred", "subitems_url": "https://example.invalid/w.json" })");
    for (int i = 40; i > 0; --i)
        inner = QStringLiteral(R"({ "name": "N%1", "subitems": [ %2 ] })")
                    .arg(i).arg(inner);

    writer.feedOsList(QStringLiteral(R"({
        "imager": {},
        "os_list": [
            { "name": "Shallow", "devices": [] },
            %1
        ]
    })").arg(inner).toUtf8());

    REQUIRE_NOTHROW(writer.feedSubList(QByteArray(R"JSON({
        "os_list": [ { "name": "Spliced" } ]
    })JSON"), QUrl(QStringLiteral("https://example.invalid/w.json"))));

    const QStringList names = namesIn(writer.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    // The entry beside the deep one is unaffected.
    CHECK(names.contains(QStringLiteral("Shallow")));
    CHECK(names.contains(QStringLiteral("Erase")));
}

TEST_CASE("A sub-list for a URL nobody asked for changes nothing",
          "[imagewriter][oslist]")
{
    // A late or stray response must not be spliced into an unrelated entry.
    FeedableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Deferred", "subitems_url": "https://example.invalid/wanted.json" }
        ]
    })JSON"));

    writer.feedSubList(QByteArray(R"JSON({
        "os_list": [ { "name": "Unexpected" } ]
    })JSON"), QUrl(QStringLiteral("https://example.invalid/unrelated.json")));

    const QJsonDocument doc = writer.getFilteredOSlistDocument();
    for (const auto &v : doc.object().value(QStringLiteral("os_list")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() != QLatin1String("Deferred"))
            continue;
        // Still waiting for the response it actually asked for.
        CHECK(o.contains(QStringLiteral("subitems_url")));
        QStringList inner;
        for (const auto &sv : o.value(QStringLiteral("subitems")).toArray())
            inner << sv.toObject().value(QStringLiteral("name")).toString();
        CHECK_FALSE(inner.contains(QStringLiteral("Unexpected")));
    }
}

TEST_CASE("A sub-list is merged into a nested category too",
          "[imagewriter][oslist]")
{
    // Categories nest, and the entry waiting on a URL may be several levels
    // down rather than at the top.
    FeedableImageWriter writer;
    const QUrl subUrl(QStringLiteral("https://example.invalid/deep.json"));

    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [
            { "name": "Outer", "subitems": [
                { "name": "Inner", "subitems_url": "https://example.invalid/deep.json" }
            ]}
        ]
    })JSON"));

    writer.feedSubList(QByteArray(R"JSON({
        "os_list": [ { "name": "Deep child" } ]
    })JSON"), subUrl);

    const QJsonDocument doc = writer.getFilteredOSlistDocument();
    bool checked = false;
    for (const auto &v : doc.object().value(QStringLiteral("os_list")).toArray()) {
        const QJsonObject outer = v.toObject();
        if (outer.value(QStringLiteral("name")).toString() != QLatin1String("Outer"))
            continue;
        for (const auto &iv : outer.value(QStringLiteral("subitems")).toArray()) {
            const QJsonObject inner = iv.toObject();
            if (inner.value(QStringLiteral("name")).toString() != QLatin1String("Inner"))
                continue;
            checked = true;
            QStringList names;
            for (const auto &cv : inner.value(QStringLiteral("subitems")).toArray())
                names << cv.toObject().value(QStringLiteral("name")).toString();
            INFO("deep children: " << names.join(QStringLiteral(", ")).toStdString());
            CHECK(names.contains(QStringLiteral("Deep child")));
            CHECK_FALSE(inner.contains(QStringLiteral("subitems_url")));
        }
    }
    CHECK(checked);
}

// ══════════════════════════════════════════════════════════════
// Language, keyboard and the cache
// ══════════════════════════════════════════════════════════════

TEST_CASE("There are translations to choose between", "[imagewriter][locale]")
{
    // The language menu is built from this. Empty means a menu with nothing
    // in it, and English for everyone regardless of what they picked.
    ImageWriter w(nullptr);
    const QStringList langs = w.getTranslations();
    INFO("count: " << langs.size());
    CHECK_FALSE(langs.isEmpty());

    // Sorted, because the menu is shown in this order.
    QStringList sorted = langs;
    sorted.sort(Qt::CaseInsensitive);
    CHECK(langs == sorted);
}

TEST_CASE("A language starts out selected", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    CHECK_FALSE(w.getCurrentLanguage().isEmpty());
}

TEST_CASE("Changing the language takes effect", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList langs = w.getTranslations();
    REQUIRE(langs.size() > 1);

    // Pick something that is not already current.
    const QString current = w.getCurrentLanguage();
    QString target;
    for (const QString &l : langs) {
        if (l != current) { target = l; break; }
    }
    REQUIRE_FALSE(target.isEmpty());

    w.changeLanguage(target);
    CHECK(w.getCurrentLanguage() == target);
}

TEST_CASE("Changing to a language that does not exist is survivable",
          "[imagewriter][locale]")
{
    // The setting is persisted, so a stale or hand-edited value can name a
    // translation that has since been removed.
    ImageWriter w(nullptr);
    const QString before = w.getCurrentLanguage();
    CHECK_NOTHROW(w.changeLanguage(QStringLiteral("Klingon (no-such-locale)")));
    INFO("before: " << before.toStdString()
         << "  after: " << w.getCurrentLanguage().toStdString());
    CHECK_FALSE(w.getCurrentLanguage().isEmpty());
}

TEST_CASE("Changing the keyboard layout takes effect", "[imagewriter][locale]")
{
    // This is written into the customisation, so it is what the board comes
    // up with -- getting it wrong means a keyboard that types the wrong
    // characters on first boot.
    ImageWriter w(nullptr);
    w.changeKeyboard(QStringLiteral("gb"));
    CHECK(w.getCurrentKeyboard() == QStringLiteral("gb"));
    w.changeKeyboard(QStringLiteral("us"));
    CHECK(w.getCurrentKeyboard() == QStringLiteral("us"));
}

TEST_CASE("Nothing is cached for a hash that was never written",
          "[imagewriter][cache]")
{
    // A false hit here writes a completely different image to the card than
    // the one the user chose.
    ImageWriter w(nullptr);
    CHECK_FALSE(w.isCached(QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                           QByteArray("0000000000000000000000000000000000000000000000000000000000000000")));
}

TEST_CASE("An empty hash is never treated as cached", "[imagewriter][cache]")
{
    // An unverified download has no hash. Treating that as a cache key would
    // match anything else that also lacks one.
    ImageWriter w(nullptr);
    CHECK_FALSE(w.isCached(QUrl(QStringLiteral("https://example.invalid/os.img.xz")),
                           QByteArray()));
}

TEST_CASE("Hardware tags with no device list produce no filtering",
          "[imagewriter][hardware]")
{
    // It succeeds and sets an empty tag set rather than declining. That is
    // safe -- an empty filter filters nothing, so the chooser still shows
    // every image -- but it does mean the return value says nothing about
    // whether any devices were found. Pinned as the behaviour, since the
    // alternative reading (that true means tags were built) would be wrong.
    ImageWriter w(nullptr);
    CHECK(w.createHardwareTags());

    // The proof it is harmless: everything is still offered.
    FeedableImageWriter fed;
    fed.feedOsList(taggedOsList());
    REQUIRE(fed.createHardwareTags());
    const QStringList names = namesIn(fed.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK(names.contains(QStringLiteral("Pi 5 only")));
    CHECK(names.contains(QStringLiteral("Zero 2 only")));
}

TEST_CASE("Hardware tags are taken from the feed's device list",
          "[imagewriter][hardware]")
{
    // Once the feed declares devices, their tags become the filter the OS
    // chooser is narrowed by.
    FeedableImageWriter w;
    w.feedOsList(QByteArray(R"JSON({
        "imager": {
            "devices": [
                { "name": "Raspberry Pi 5", "tags": ["pi5-64bit"] }
            ]
        },
        "os_list": [
            { "name": "Pi 5 only",   "devices": ["pi5-64bit"] },
            { "name": "Zero 2 only", "devices": ["pi-zero2-64bit"] }
        ]
    })JSON"));

    CHECK(w.createHardwareTags());

    // Deliberately not asserting on getHardwareName(): it reports what this
    // machine is, so a test that checked it would pass or fail on the build
    // host rather than on the code. Only that asking is safe.
    CHECK_NOTHROW(w.getHardwareName());
}

// ══════════════════════════════════════════════════════════════
// A write, driven the way the UI drives it
//
// Everything above stops short of startWrite() actually starting one. This
// takes a local image and a scratch file as the target and runs it through:
// thread selection, the progress signals the UI binds to, and the terminal
// state. It is the path every successful write takes, and none of it had
// been executed by a test.
// ══════════════════════════════════════════════════════════════

namespace {

// Runs the Qt event loop until one of ImageWriter's terminal signals lands.
struct WriteOutcome
{
    bool succeeded = false;
    bool failed = false;
    bool finalizing = false;
    QStringList errors;
    QStringList statuses;
    bool sawProgress = false;
    QStringList progressKinds;
};

WriteOutcome runWrite(ImageWriter &w, int timeoutMs = 120000)
{
    WriteOutcome out;
    QEventLoop loop;

    // Scoped so nothing here outlives the loop it quits (see fetchOsList).
    QObject context;
    QObject::connect(&w, &ImageWriter::success, &context, [&] { out.succeeded = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::error, &context, [&](QVariant m) {
        out.failed = true;
        out.errors << m.toString();
        loop.quit();
    });
    QObject::connect(&w, &ImageWriter::finalizing, &context, [&] { out.finalizing = true; });
    QObject::connect(&w, &ImageWriter::preparationStatusUpdate, &context,
                     [&](QVariant m) { out.statuses << m.toString(); });
    QObject::connect(&w, &ImageWriter::writeProgress, &context,
                     [&](QVariant n, QVariant t) {
                         out.sawProgress = true;
                         out.progressKinds << QStringLiteral("write %1/%2")
                                                  .arg(n.toULongLong()).arg(t.toULongLong());
                     });
    QObject::connect(&w, &ImageWriter::downloadProgress, &context,
                     [&](QVariant n, QVariant t) {
                         out.progressKinds << QStringLiteral("download %1/%2")
                                                  .arg(n.toULongLong()).arg(t.toULongLong());
                     });
    QObject::connect(&w, &ImageWriter::verifyProgress, &context,
                     [&](QVariant n, QVariant t) {
                         out.progressKinds << QStringLiteral("verify %1/%2")
                                                  .arg(n.toULongLong()).arg(t.toULongLong());
                     });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);

    w.startWrite();
    loop.exec();
    return out;
}

// A small image of recognisable bytes, and somewhere to write it.
class WriteFixture
{
public:
    WriteFixture()
    {
        REQUIRE(_dir.isValid());
        _source = QDir(_dir.path()).filePath(QStringLiteral("source.img"));
        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));

        _payload.reserve(kSize);
        for (int i = 0; i < kSize; ++i)
            _payload.append(char('A' + (i * 31) % 26));

        QFile s(_source);
        REQUIRE(s.open(QIODevice::WriteOnly));
        REQUIRE(s.write(_payload) == _payload.size());
        s.close();

        // Pre-create the target at the same size: the write path opens it as
        // though it were a device rather than creating it.
        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        REQUIRE(t.write(QByteArray(kSize, '\0')) == kSize);
        t.close();
    }

    static constexpr int kSize = 4 * 1024 * 1024;
    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString target() const { return _target; }
    const QByteArray &payload() const { return _payload; }

private:
    QTemporaryDir _dir;
    QString _source, _target;
    QByteArray _payload;
};

// Same shape, sized so the copy loop runs many times.
class LargeWriteFixture
{
public:
    // Sized by the caller, because one case needs the write and the read-back
    // to last long enough for the 100 ms progress throttle to let a verify
    // tick through. The default is what every other case wants.
    static constexpr int kSizeMB = 64;
    static constexpr quint64 kSize = quint64(kSizeMB) * 1024 * 1024;

    explicit LargeWriteFixture(int sizeMB = kSizeMB)
        : _sizeMB(sizeMB)
    {
        REQUIRE(_dir.isValid());
        _source = QDir(_dir.path()).filePath(QStringLiteral("source.img"));
        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));

        QByteArray chunk(1024 * 1024, '\0');
        for (int i = 0; i < chunk.size(); ++i)
            chunk[i] = char('A' + (i * 17) % 26);

        QFile s(_source);
        REQUIRE(s.open(QIODevice::WriteOnly));
        for (int i = 0; i < _sizeMB; ++i)
            REQUIRE(s.write(chunk) == chunk.size());
        s.close();

        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        for (int i = 0; i < _sizeMB; ++i)
            REQUIRE(t.write(QByteArray(1024 * 1024, '\0')) == 1024 * 1024);
        t.close();
    }

    quint64 size() const { return quint64(_sizeMB) * 1024 * 1024; }
    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString target() const { return _target; }

private:
    int _sizeMB;
    QTemporaryDir _dir;
    QString _source, _target;
};

} // namespace

TEST_CASE("A local image is written through to the target", "[imagewriter][write]")
{
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);
    REQUIRE(w.readyToWrite());

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    INFO("statuses: " << out.statuses.join(QStringLiteral(" | ")).toStdString());

    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);

    // The bytes really arrived, not just a success signal.
    QFile t(fx.target());
    REQUIRE(t.open(QIODevice::ReadOnly));
    CHECK(t.read(WriteFixture::kSize) == fx.payload());
}

TEST_CASE("The UI is told what is happening during a write",
          "[imagewriter][write]")
{
    // The progress bar and status line are bound to these. A write that
    // completes without ever emitting leaves the window looking frozen.
    //
    // Deliberately larger than the other cases here. Progress is sampled
    // inside the copy loop and only emitted when the written count has
    // actually moved, so an image small enough to be consumed in a single
    // buffer can finish with the write still in flight at the one sample
    // point -- no progress, and nothing wrong. Several buffers' worth is
    // what a real image looks like and what the bar is there for.
    LargeWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, LargeWriteFixture::kSize);
    w.setDst(fx.target(), LargeWriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 600000);
    REQUIRE(out.succeeded);
    INFO("progress signals: " << out.progressKinds.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.sawProgress);
}

TEST_CASE("A write to somewhere unwritable reports rather than hangs",
          "[imagewriter][write]")
{
    // The failure the user meets when the card is write-protected or the
    // permissions are wrong. It has to come back as an error, not a
    // progress bar that never moves.
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(QStringLiteral("/nonexistent-9f2a/target.img"), WriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 60000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
    CHECK_FALSE(out.errors.isEmpty());
}

TEST_CASE("A verified write checks what it wrote", "[imagewriter][write]")
{
    // Verification reads the card back. With it on, a successful result
    // means the bytes on the device were compared, not just sent.
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("Verifying is reported as a stage of its own", "[imagewriter][write][verify]")
{
    // After the last byte is written Imager reads the card back and compares
    // it, which on a real card takes about as long as the write did. If the
    // state never moves off Writing, the user watches a full progress bar for
    // minutes with nothing to say why -- indistinguishable from a hang, and
    // the point at which people pull the card out.
    //
    // Big enough that the read-back cannot finish inside the progress
    // throttle. Progress is emitted at most every 100 ms, and the exemption
    // for the first update belongs to the write, not to the verify -- so a
    // read-back that completes in less than 100 ms reports nothing at all
    // and the state never leaves Writing. At 64 MB that is a coin toss
    // depending on what else the machine is doing: this case passed for a
    // day and then failed once under load, on a run where the read-back was
    // quicker than the throttle rather than slower.
    LargeWriteFixture fx(256);
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, fx.size());
    w.setDst(fx.target(), fx.size());

    QStringList states;
    QObject::connect(&w, &ImageWriter::writeStateChanged, &w, [&] {
        states << w.property("writeState").toString();
    });

    const WriteOutcome out = runWrite(w, 600000);

    INFO("states: " << states.join(QStringLiteral(" -> ")).toStdString());
    INFO("progress: " << out.progressKinds.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);

    const bool sawVerifyProgress =
        std::any_of(out.progressKinds.cbegin(), out.progressKinds.cend(),
                    [](const QString &k) { return k.startsWith(QStringLiteral("verify ")); });
    CHECK(sawVerifyProgress);
    CHECK(states.contains(QStringLiteral("Verifying")));
}

TEST_CASE("Skipping verification leaves a finished write, not an abandoned one",
          "[imagewriter][write][verify]")
{
    // The button offered on the writing screen. Somebody who trusts the card,
    // or who has waited long enough, can stop the read-back -- and what they
    // get has to be a finished write. Treating Skip as a cancel would throw
    // away a card that is already correctly written, and they would start
    // again for nothing.
    //
    // Pressed while the write is still running rather than part-way through
    // the read-back, which is both what the screen allows and the only way to
    // see it work: at any size a test can afford the read-back reports
    // progress exactly once, so a skip taken on that tick lands after the
    // checking has already finished and proves nothing.
    LargeWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, LargeWriteFixture::kSize);
    w.setDst(fx.target(), LargeWriteFixture::kSize);

    int verifyTicks = 0;
    bool skipped = false;
    QObject::connect(&w, &ImageWriter::verifyProgress, &w,
                     [&](QVariant, QVariant) { ++verifyTicks; });
    QObject::connect(&w, &ImageWriter::writeProgress, &w, [&](QVariant now, QVariant) {
        if (!skipped && now.toULongLong() > 0) {
            skipped = true;
            w.skipCurrentVerification();
        }
    });

    const WriteOutcome out = runWrite(w, 600000);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(skipped);
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
    // The checking really was skipped rather than the button being
    // decorative -- and the write still came back as done.
    CHECK(verifyTicks == 0);
}

// ══════════════════════════════════════════════════════════════
// Cancelling
//
// Pressing Cancel has to stop the write and say it stopped. A cancel that
// is ignored leaves the user watching a progress bar they have already told
// to stop, on a card that is being written to regardless.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Cancelling a write in progress reports cancelled, not success",
          "[imagewriter][write][cancel]")
{
    LargeWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, LargeWriteFixture::kSize);
    w.setDst(fx.target(), LargeWriteFixture::kSize);

    bool succeeded = false, cancelled = false;
    QStringList errors;
    QEventLoop loop;
    QObject::connect(&w, &ImageWriter::success, [&] { succeeded = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::cancelled, [&] { cancelled = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::error, [&](QVariant m) {
        errors << m.toString();
        loop.quit();
    });

    // Cancel once the write is genuinely under way rather than before it
    // starts -- stopping something that has not begun proves nothing.
    QObject::connect(&w, &ImageWriter::writeProgress, &w, [&w](QVariant now, QVariant) {
        if (now.toULongLong() > 0)
            w.cancelWrite();
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(600000);

    w.startWrite();
    loop.exec();

    INFO("succeeded=" << succeeded << " cancelled=" << cancelled
         << " errors=" << errors.join(QStringLiteral(" | ")).toStdString());
    // Whichever way it lands, it must not claim the card was written.
    CHECK_FALSE(succeeded);
}

TEST_CASE("A source file with nothing in it is refused", "[imagewriter][write]")
{
    // An interrupted copy, or a download that failed and still left a file
    // behind. Zero bytes clears the capacity check trivially and extracts to
    // nothing, so the write ran to the end and reported success -- the user is
    // told the card is imaged, and finds out otherwise when it will not boot.
    WriteFixture fx;
    const QString source = fx.sourceUrl().toLocalFile();
    {
        QFile s(source);
        REQUIRE(s.open(QIODevice::WriteOnly | QIODevice::Truncate));
    }
    REQUIRE(QFileInfo(source).size() == 0);

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, 0);
    w.setDst(fx.target(), WriteFixture::kSize);

    bool succeeded = false;
    QStringList errors;
    QEventLoop loop;
    QObject ctx;
    QObject::connect(&w, &ImageWriter::success, &ctx, [&] { succeeded = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::error, &ctx, [&](QVariant m) {
        errors << m.toString();
        loop.quit();
    });

    // Started from inside the loop so the refusal, which arrives
    // synchronously out of startWrite(), still ends the wait rather than
    // quitting a loop that has not begun.
    QTimer::singleShot(0, &w, [&w] { w.startWrite(); });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(120000);
    loop.exec();

    INFO("errors: " << errors.join(QStringLiteral(" | ")).toStdString());
    // The part that matters: it must not claim to have written the card.
    CHECK_FALSE(succeeded);
    CHECK_FALSE(errors.isEmpty());
}

TEST_CASE("Cancelling before anything starts is harmless",
          "[imagewriter][write][cancel]")
{
    // The Cancel button exists before a write does; pressing it must not
    // leave the backend in a state that refuses the next write.
    WriteFixture fx;
    ImageWriter w(nullptr);
    CHECK_NOTHROW(w.cancelWrite());

    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);
    CHECK(w.readyToWrite());

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.succeeded);
}

// ══════════════════════════════════════════════════════════════
// The debug switches
//
// Each of these changes how a write is performed. They are reachable from
// the UI, so one that silently fails to stick means somebody is not running
// what they selected -- and these are exactly the switches reached for when
// diagnosing a card that will not write.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Every debug switch persists what it was set to",
          "[imagewriter][settings]")
{
    ImageWriter w(nullptr);

    struct Toggle {
        const char *name;
        void (ImageWriter::*set)(bool);
        bool (ImageWriter::*get)() const;
    };

    const Toggle toggles[] = {
        {"directIO",           &ImageWriter::setDebugDirectIO,           &ImageWriter::getDebugDirectIO},
        {"periodicSync",       &ImageWriter::setDebugPeriodicSync,       &ImageWriter::getDebugPeriodicSync},
        {"verboseLogging",     &ImageWriter::setDebugVerboseLogging,     &ImageWriter::getDebugVerboseLogging},
        {"asyncIO",            &ImageWriter::setDebugAsyncIO,            &ImageWriter::getDebugAsyncIO},
        {"skipEndOfDevice",    &ImageWriter::setDebugSkipEndOfDevice,    &ImageWriter::getDebugSkipEndOfDevice},
        {"ignoreDeviceLimits", &ImageWriter::setDebugIgnoreDeviceLimits, &ImageWriter::getDebugIgnoreDeviceLimits},
        {"rpiboot",            &ImageWriter::setDebugRpiboot,            &ImageWriter::getDebugRpiboot},
        {"forceSecureBoot",    &ImageWriter::setDebugForceSecureBoot,    &ImageWriter::getDebugForceSecureBoot},
        {"signFastbootGadget", &ImageWriter::setDebugSignFastbootGadget, &ImageWriter::getDebugSignFastbootGadget},
    };

    for (const Toggle &t : toggles) {
        INFO("toggle: " << t.name);
        (w.*t.set)(true);
        CHECK((w.*t.get)());
        (w.*t.set)(false);
        CHECK_FALSE((w.*t.get)());
    }
}

TEST_CASE("The async queue depth persists", "[imagewriter][settings]")
{
    // Not a toggle: a number, and one that changes how much is in flight
    // against the card at once.
    ImageWriter w(nullptr);
    w.setDebugAsyncQueueDepth(16);
    CHECK(w.getDebugAsyncQueueDepth() == 16);
    w.setDebugAsyncQueueDepth(64);
    CHECK(w.getDebugAsyncQueueDepth() == 64);
}

TEST_CASE("A custom fastboot gadget path persists", "[imagewriter][settings]")
{
    ImageWriter w(nullptr);
    const QString path = QStringLiteral("/tmp/my-gadget.img");
    w.setDebugCustomFastbootGadget(path);
    CHECK(w.getDebugCustomFastbootGadget() == path);
    w.setDebugCustomFastbootGadget(QString());
    CHECK(w.getDebugCustomFastbootGadget().isEmpty());
}

// ══════════════════════════════════════════════════════════════
// The write-start body, by configuration
//
// startWrite() defers to _continueStartWriteAfterCacheVerification(), which
// is where the source is resolved, the thread chosen and verification wired
// up. A plain uncompressed local image takes one narrow path through it;
// these take the others.
// ══════════════════════════════════════════════════════════════

namespace {

// A compressed image and a target, so the decompressing thread is chosen
// rather than the plain local-file one.
class CompressedWriteFixture
{
public:
    explicit CompressedWriteFixture(const char *tool = "xz")
    {
        REQUIRE(_dir.isValid());
        const QString raw = QDir(_dir.path()).filePath(QStringLiteral("image.img"));
        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));

        _payload.reserve(kSize);
        for (int i = 0; i < kSize; ++i)
            _payload.append(char('A' + (i * 13) % 26));

        QFile s(raw);
        REQUIRE(s.open(QIODevice::WriteOnly));
        REQUIRE(s.write(_payload) == _payload.size());
        s.close();

        _hash = QCryptographicHash::hash(_payload, QCryptographicHash::Sha256).toHex();

        QProcess p;
        p.start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral("%1 -k -f %2").arg(QString::fromLatin1(tool), raw)});
        REQUIRE(p.waitForFinished(rpi_test::kFixtureProcessTimeoutMs));
        REQUIRE(p.exitCode() == 0);

        _source = raw + (qstrcmp(tool, "gzip") == 0 ? QStringLiteral(".gz")
                                                    : QStringLiteral(".xz"));
        REQUIRE(QFile::exists(_source));

        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        REQUIRE(t.write(QByteArray(kSize, '\0')) == kSize);
        t.close();
    }

    static constexpr int kSize = 2 * 1024 * 1024;
    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString target() const { return _target; }
    QByteArray hash() const { return _hash; }
    const QByteArray &payload() const { return _payload; }

private:
    QTemporaryDir _dir;
    QString _source, _target;
    QByteArray _payload, _hash;
};

} // namespace

TEST_CASE("A compressed image is decompressed on the way to the card",
          "[imagewriter][write]")
{
    // Chooses the extracting thread rather than the plain local-file one,
    // which is the path nearly every real image takes.
    CompressedWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, CompressedWriteFixture::kSize);
    w.setDst(fx.target(), CompressedWriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 600000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);

    QFile t(fx.target());
    REQUIRE(t.open(QIODevice::ReadOnly));
    CHECK(t.read(CompressedWriteFixture::kSize) == fx.payload());
}

TEST_CASE("A gzip image is decompressed too", "[imagewriter][write]")
{
    CompressedWriteFixture fx("gzip");
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, CompressedWriteFixture::kSize);
    w.setDst(fx.target(), CompressedWriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 600000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.succeeded);
}

TEST_CASE("A write with the right hash is accepted", "[imagewriter][write]")
{
    // Supplying an expected hash turns on the verification wiring, which is
    // a different path through the write setup than an unverified write.
    CompressedWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, CompressedWriteFixture::kSize, fx.hash());
    w.setDst(fx.target(), CompressedWriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 600000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("A write whose image does not match its hash is refused",
          "[imagewriter][write]")
{
    // The download was corrupted in transit or the repository is serving
    // something else. Reporting success here hands the user a card built
    // from bytes nobody vouched for.
    CompressedWriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, CompressedWriteFixture::kSize,
             QByteArray("0000000000000000000000000000000000000000000000000000000000000000"));
    w.setDst(fx.target(), CompressedWriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 600000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
}

TEST_CASE("A source file that vanished before the write starts is reported",
          "[imagewriter][write]")
{
    // Validated before any thread is spawned, so the user is told rather
    // than watching a write that cannot begin.
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(QStringLiteral("/nonexistent-9f2a/gone.img")),
             0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);

    const WriteOutcome out = runWrite(w, 60000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.failed);
    CHECK_FALSE(out.errors.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// URLs arriving from outside the application
//
// handleIncomingUrl() is the rpi-imager:// scheme handler: a link somebody
// clicks, or a hand-off from another application. It can point the OS list
// at a different repository and carry a Connect token, so what it accepts
// decides which images the user is offered and which organisation a device
// is enrolled into. Neither is a decision the sender should get to make
// unchecked.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A well-formed repository URL is accepted", "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    QStringList accepted;
    QObject::connect(&w, &ImageWriter::repositoryUrlReceived,
                     [&accepted](QString u) { accepted << u; });

    w.handleIncomingUrl(QUrl(QStringLiteral(
        "rpi-imager://open?repo=https://example.com/os_list.json")));

    INFO("accepted: " << accepted.join(QStringLiteral(", ")).toStdString());
    CHECK(accepted.size() == 1);
}

TEST_CASE("Repository URLs that are not http(s) JSON are refused",
          "[imagewriter][url]")
{
    // The repository decides which images the user is offered, so a link
    // from outside must not be able to point it anywhere it likes.
    ImageWriter w(nullptr);
    QStringList accepted;
    QObject::connect(&w, &ImageWriter::repositoryUrlReceived,
                     [&accepted](QString u) { accepted << u; });

    const QStringList hostile = {
        QStringLiteral("rpi-imager://open?repo=file:///etc/passwd"),
        QStringLiteral("rpi-imager://open?repo=javascript:alert(1)"),
        QStringLiteral("rpi-imager://open?repo=ftp://example.com/os_list.json"),
        // Right scheme, but not a manifest.
        QStringLiteral("rpi-imager://open?repo=https://example.com/payload.sh"),
        QStringLiteral("rpi-imager://open?repo=https://example.com/"),
        // Whitespace smuggling.
        QStringLiteral("rpi-imager://open?repo=https://example.com/a.json%20extra"),
    };

    for (const QString &u : hostile) {
        INFO("url: " << u.toStdString());
        w.handleIncomingUrl(QUrl(u));
    }

    INFO("accepted: " << accepted.join(QStringLiteral(", ")).toStdString());
    CHECK(accepted.isEmpty());
}

TEST_CASE("A repository URL with a query or fragment is still accepted",
          "[imagewriter][url]")
{
    // Mirrors and CDNs append these; refusing them would reject legitimate
    // manifests.
    ImageWriter w(nullptr);
    QStringList accepted;
    QObject::connect(&w, &ImageWriter::repositoryUrlReceived,
                     [&accepted](QString u) { accepted << u; });

    w.handleIncomingUrl(QUrl(QStringLiteral(
        "rpi-imager://open?repo=https://example.com/os_list.json%3Fv%3D2")));

    INFO("accepted: " << accepted.join(QStringLiteral(", ")).toStdString());
    CHECK(accepted.size() == 1);
}

TEST_CASE("A URL carrying nothing of interest is ignored", "[imagewriter][url]")
{
    // Not named "signals": that is a Qt keyword macro expanding to public.
    ImageWriter w(nullptr);
    int emitted = 0;
    QObject::connect(&w, &ImageWriter::repositoryUrlReceived, [&emitted](QString) { ++emitted; });
    QObject::connect(&w, &ImageWriter::connectTokenConflictDetected,
                     [&emitted](QString) { ++emitted; });

    CHECK_NOTHROW(w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open"))));
    CHECK_NOTHROW(w.handleIncomingUrl(QUrl()));
    CHECK(emitted == 0);
}

TEST_CASE("A local file that is not a manifest is ignored", "[imagewriter][url]")
{
    // Dropping an arbitrary file on the application must not be taken as an
    // OS list.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString notJson = QDir(dir.path()).filePath(QStringLiteral("notes.txt"));
    QFile f(notJson);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("nothing to see");
    f.close();

    ImageWriter w(nullptr);
    const QUrl before = w.osListUrl();
    CHECK_NOTHROW(w.handleIncomingUrl(QUrl::fromLocalFile(notJson)));
    CHECK(w.osListUrl() == before);
}

TEST_CASE("A local manifest that is not there changes nothing",
          "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    const QUrl before = w.osListUrl();
    CHECK_NOTHROW(w.handleIncomingUrl(
        QUrl::fromLocalFile(QStringLiteral("/nonexistent-9f2a/os_list.json"))));
    CHECK(w.osListUrl() == before);
}

// ══════════════════════════════════════════════════════════════
// When the OS list cannot be fetched
// ══════════════════════════════════════════════════════════════

TEST_CASE("A failed OS list fetch still leaves a usable chooser",
          "[imagewriter][oslist]")
{
    // Offline, or the repository is down. The chooser must still offer the
    // built-in entries rather than coming up empty with no explanation.
    FeedableImageWriter w;
    w.reportOsListFailure(QStringLiteral("Could not resolve host"));

    const QStringList names = namesIn(w.getFilteredOSlistDocument());
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.isEmpty());
}

TEST_CASE("A failed fetch does not discard a list already loaded",
          "[imagewriter][oslist]")
{
    // A later refresh failing must not empty a chooser that was working:
    // the user would rather see yesterday's list than none.
    FeedableImageWriter w;
    w.feedOsList(taggedOsList());
    const int before = namesIn(w.getFilteredOSlistDocument()).size();
    REQUIRE(before > 0);

    w.reportOsListFailure(QStringLiteral("Network unreachable"));

    const int after = namesIn(w.getFilteredOSlistDocument()).size();
    INFO("before " << before << ", after " << after);
    CHECK(after == before);
}

// ═══════════════════════════════════════════════════════════════════════════
// The disk cache, and the half of startWrite() that lives behind it
//
// When the image the user picked is already in the cache, startWrite() does
// not write anything. It kicks off a background integrity check and returns,
// and the write is resumed later by
// _continueStartWriteAfterCacheVerification() -- 594 branches that no test
// had ever executed, because reaching them needs a cache file on disk whose
// hash matches the selected image.
//
// What it decides is which bytes reach the card: the cached copy, or a fresh
// download. Getting that wrong writes the wrong image, or silently writes a
// corrupt one. The cases below set the cache file's contents and the source
// image's contents to different patterns, so the bytes on the target say
// which path was taken rather than the log doing it.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

QByteArray patternOf(char seed, int size)
{
    QByteArray out;
    out.reserve(size);
    for (int i = 0; i < size; ++i)
        out.append(char('A' + ((i * 31) + seed) % 26));
    return out;
}

QByteArray sha256HexOf(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

// A source image and a cache file holding deliberately different bytes.
class CacheFixture
{
public:
    explicit CacheFixture(char cacheSeed = 7)
    {
        REQUIRE(_dir.isValid());
        _source = QDir(_dir.path()).filePath(QStringLiteral("source.img"));
        _cache  = QDir(_dir.path()).filePath(QStringLiteral("cached.img"));
        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));

        _sourceBytes = patternOf(0, kSize);
        _cacheBytes  = patternOf(cacheSeed, kSize);
        REQUIRE(_sourceBytes != _cacheBytes);

        write(_source, _sourceBytes);
        write(_cache, _cacheBytes);
        write(_target, QByteArray(kSize, '\0'));
    }

    static constexpr int kSize = 2 * 1024 * 1024;

    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString cachePath() const { return _cache; }
    QString target() const { return _target; }
    const QByteArray &sourceBytes() const { return _sourceBytes; }
    const QByteArray &cacheBytes() const { return _cacheBytes; }

    QByteArray targetBytes() const
    {
        QFile f(_target);
        REQUIRE(f.open(QIODevice::ReadOnly));
        return f.read(kSize);
    }

private:
    static void write(const QString &path, const QByteArray &bytes)
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(bytes) == bytes.size());
        f.close();
    }

    QTemporaryDir _dir;
    QString _source, _cache, _target;
    QByteArray _sourceBytes, _cacheBytes;
};

} // namespace

TEST_CASE("A cache hit defers the write until the cache has been checked", "[imagewriter][cache]")
{
    CacheFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, CacheFixture::kSize, sha256HexOf(fx.cacheBytes()));
    w.setCustomCacheFile(fx.cachePath(), sha256HexOf(fx.cacheBytes()));
    w.setDst(fx.target(), CacheFixture::kSize);
    REQUIRE(w.readyToWrite());

    rpi_test::SignalLog started(&w, &ImageWriter::cacheVerificationStarted);
    rpi_test::SignalLog succeeded(&w, &ImageWriter::success);
    rpi_test::SignalLog failed(&w, &ImageWriter::error);

    w.startWrite();

    // startWrite() must return having started nothing: the UI shows a
    // "checking cached image" state and the card is not touched yet.
    CHECK(started.count() == 1);
    CHECK(succeeded.isEmpty());
    CHECK(failed.isEmpty());
}

TEST_CASE("A cache file that verifies is written instead of the source", "[imagewriter][cache]")
{
    CacheFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, CacheFixture::kSize, sha256HexOf(fx.cacheBytes()));
    w.setCustomCacheFile(fx.cachePath(), sha256HexOf(fx.cacheBytes()));
    w.setDst(fx.target(), CacheFixture::kSize);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // The bytes on the target are the cache's, not the source's. This is the
    // whole point of the cache and the only way to tell the two paths apart.
    CHECK(fx.targetBytes() == fx.cacheBytes());
}

TEST_CASE("A cache file that fails its check is discarded and the source used", "[imagewriter][cache]")
{
    CacheFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // The image the user picked hashes to the source's bytes, but the cache
    // file holds something else -- a truncated or corrupted earlier download.
    const QByteArray expected = sha256HexOf(fx.sourceBytes());
    w.setSrc(fx.sourceUrl(), 0, CacheFixture::kSize, expected);
    w.setCustomCacheFile(fx.cachePath(), expected);
    w.setDst(fx.target(), CacheFixture::kSize);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // A corrupt cache must never reach the card.
    CHECK(fx.targetBytes() == fx.sourceBytes());
}

TEST_CASE("Skipping the cache check falls back to the source", "[imagewriter][cache]")
{
    CacheFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // The selected image is the source; the cache file is a stale copy that
    // the user chooses not to wait for.
    const QByteArray expected = sha256HexOf(fx.sourceBytes());
    w.setSrc(fx.sourceUrl(), 0, CacheFixture::kSize, expected);
    w.setCustomCacheFile(fx.cachePath(), expected);
    w.setDst(fx.target(), CacheFixture::kSize);

    rpi_test::SignalLog finished(&w, &ImageWriter::cacheVerificationFinished);

    // The "Skip" the user gets while the cache is being checked.
    QObject::connect(&w, &ImageWriter::cacheVerificationStarted,
                     &w, &ImageWriter::skipCacheVerification, Qt::QueuedConnection);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    CHECK(finished.count() == 1);
    // Skipping means the cache is discarded unread, so the source is used.
    CHECK(fx.targetBytes() == fx.sourceBytes());
}

// ═══════════════════════════════════════════════════════════════════════════
// The locale data behind the customisation screen
//
// Picking a city on the customisation page is what fills in the timezone,
// keyboard layout and language that get written to the card. Both functions
// that do it read a pipe-separated resource file and had never been called.
//
// A malformed or truncated line there is not a crash: the city still appears
// in the picker, selecting it silently sets nothing, and the user ends up
// with a Pi on the wrong timezone and a keyboard that types the wrong
// symbols. The sweep at the end of this section is as much a check on the
// data as on the code.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("The capital-cities picker is populated", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList cities = w.getCapitalCitiesList();

    REQUIRE_FALSE(cities.isEmpty());
    CHECK(cities.contains(QStringLiteral("London (United Kingdom)")));

    // Every row must carry the "City (Country)" form the picker displays;
    // a bare city name is ambiguous across countries.
    for (const QString &c : cities) {
        INFO("entry: " << c.toStdString());
        REQUIRE(c.contains(QStringLiteral(" (")));
        REQUIRE(c.endsWith(QLatin1Char(')')));
    }
}

TEST_CASE("A capital city resolves to a complete locale set", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QVariantMap uk = w.getLocaleDataForCapital(QStringLiteral("London (United Kingdom)"));

    REQUIRE_FALSE(uk.isEmpty());
    CHECK(uk.value(QStringLiteral("cityName")).toString() == QStringLiteral("London"));
    CHECK(uk.value(QStringLiteral("countryName")).toString() == QStringLiteral("United Kingdom"));
    CHECK(uk.value(QStringLiteral("countryCode")).toString() == QStringLiteral("GB"));
    CHECK(uk.value(QStringLiteral("timezone")).toString() == QStringLiteral("Europe/London"));
    CHECK(uk.value(QStringLiteral("language")).toString() == QStringLiteral("English"));
    CHECK(uk.value(QStringLiteral("keyboard")).toString() == QStringLiteral("gb"));
}

TEST_CASE("The bare city name resolves the same as the displayed form", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    // QML passes back whatever the picker showed, but the lookup is also used
    // with a plain city name.
    CHECK(w.getLocaleDataForCapital(QStringLiteral("Paris"))
          == w.getLocaleDataForCapital(QStringLiteral("Paris (France)")));
}

TEST_CASE("An unknown city yields nothing rather than partial data", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    // Half-filled locale data would be written to the card as though chosen.
    CHECK(w.getLocaleDataForCapital(QStringLiteral("Atlantis (Nowhere)")).isEmpty());
    CHECK(w.getLocaleDataForCapital(QString()).isEmpty());
}

TEST_CASE("Every city offered resolves to complete locale data", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList cities = w.getCapitalCitiesList();
    REQUIRE(cities.size() > 100);

    const QStringList required{QStringLiteral("cityName"), QStringLiteral("countryName"),
                               QStringLiteral("countryCode"), QStringLiteral("timezone"),
                               QStringLiteral("language"), QStringLiteral("keyboard")};

    for (const QString &display : cities) {
        INFO("city: " << display.toStdString());
        const QVariantMap data = w.getLocaleDataForCapital(display);
        REQUIRE_FALSE(data.isEmpty());
        for (const QString &key : required) {
            INFO("field: " << key.toStdString());
            REQUIRE(data.contains(key));
            REQUIRE_FALSE(data.value(key).toString().isEmpty());
        }
        // A timezone that QTimeZone does not know is one systemd-timesyncd
        // will not either.
        REQUIRE(QTimeZone(data.value(QStringLiteral("timezone")).toString().toUtf8()).isValid());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Sizing a compressed image the user picked from disk
//
// "Use custom" hands setSrc() a local file with no manifest behind it, so the
// uncompressed size has to be read out of the container. That number is what
// the capacity check compares against the card, and what the progress bar is
// scaled to.
//
// Read it wrong and one of two things happens: a card that would have fitted
// is refused, or a write starts that cannot finish and dies part-way with the
// card left unbootable. Neither is diagnosable from the UI.
//
// The fixtures are real archives of a known 1 MiB payload, one per container
// the picker accepts.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

constexpr quint64 kFixturePayload = 1024 * 1024;

// setSrc() reads the file it is given, so each case gets its own copy.
class ArchiveFixture
{
public:
    explicit ArchiveFixture(const QString &archiveName)
    {
        REQUIRE(_dir.isValid());
        const QString from = QStringLiteral(IMAGER_TEST_DATA_DIR "/") + archiveName;
        REQUIRE(QFile::exists(from));
        _archive = QDir(_dir.path()).filePath(archiveName);
        REQUIRE(QFile::copy(from, _archive));

        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));
        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        REQUIRE(t.write(QByteArray(4096, '\0')) == 4096);
        t.close();
    }

    QUrl archiveUrl() const { return QUrl::fromLocalFile(_archive); }
    QString target() const { return _target; }

private:
    QTemporaryDir _dir;
    QString _archive, _target;
};

// Does startWrite() refuse this device as too small? Everything before the
// capacity check is satisfied, so the answer is about the parsed size alone.
bool refusedAsTooSmall(ImageWriter &w, const QString &target, quint64 deviceSize)
{
    w.setDst(target, deviceSize);
    rpi_test::SignalLog failed(&w, &ImageWriter::error);
    w.startWrite();
    for (int i = 0; i < failed.count(); ++i)
        if (failed.at(i).at(0).toString().contains(QStringLiteral("capacity")))
            return true;
    return false;
}

} // namespace

TEST_CASE("An xz image reports its uncompressed size", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    CHECK(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), kFixturePayload - 1));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), kFixturePayload));
}

TEST_CASE("A zstd image reports its uncompressed size", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.zst"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    CHECK(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), kFixturePayload - 1));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), kFixturePayload));
}

TEST_CASE("A zipped image reports its uncompressed size", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.zip"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    CHECK(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), kFixturePayload - 1));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), kFixturePayload));
}

TEST_CASE("A gzipped image is sized but not trusted", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.gz"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    // gzip records the uncompressed size in 32 bits, so it wraps above 4 GB.
    // The size is still parsed and still guards the capacity check -- it is
    // just not published as known, so progress is not scaled to it.
    CHECK_FALSE(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), kFixturePayload - 1));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), kFixturePayload));
}

TEST_CASE("An uncompressed image is sized from the file itself", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString plain = QDir(dir.path()).filePath(QStringLiteral("plain.img"));
    QFile f(plain);
    REQUIRE(f.open(QIODevice::WriteOnly));
    REQUIRE(f.write(QByteArray(int(kFixturePayload), 'Z')) == qint64(kFixturePayload));
    f.close();

    ImageWriter w(nullptr);
    w.setSrc(QUrl::fromLocalFile(plain));

    CHECK(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), kFixturePayload - 1));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), kFixturePayload));
}

TEST_CASE("A manifest size overrides what the container claims", "[imagewriter][archive]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    // The OS list gives an extract size directly; the file is not parsed.
    w.setSrc(fx.archiveUrl(), 0, 8 * 1024 * 1024);

    CHECK(w.isExtractSizeKnown());
    CHECK(refusedAsTooSmall(w, fx.target(), 4 * 1024 * 1024));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), 8 * 1024 * 1024));
}

// ═══════════════════════════════════════════════════════════════════════════
// Reloading the customisation the user saved last time
//
// The customisation dialog repopulates itself from this map. Anything it
// drops silently reverts to a default the user did not choose -- a hostname,
// a locale, or an SSH key that quietly is not installed.
//
// The SSH key list gets its own handling because the field is a paste target:
// the same key arriving twice is normal, and duplicates in authorized_keys
// are at best untidy.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Writes straight into the store ImageWriter reads, as a previous run would.
void saveCustomisation(const QVariantMap &values)
{
    QSettings s;
    s.beginGroup(QStringLiteral("imagecustomization"));
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
    s.sync();
}

void clearCustomisation()
{
    QSettings s;
    s.beginGroup(QStringLiteral("imagecustomization"));
    s.remove(QString());
    s.endGroup();
    s.sync();
}

} // namespace

TEST_CASE("Saved customisation settings come back as they went in", "[imagewriter][customisation]")
{
    clearCustomisation();
    saveCustomisation({{QStringLiteral("hostname"), QStringLiteral("raspberrypi")},
                       {QStringLiteral("sshEnabled"), true},
                       {QStringLiteral("timezone"), QStringLiteral("Europe/London")}});

    ImageWriter w(nullptr);
    const QVariantMap got = w.getSavedCustomisationSettings();

    CHECK(got.value(QStringLiteral("hostname")).toString() == QStringLiteral("raspberrypi"));
    CHECK(got.value(QStringLiteral("sshEnabled")).toBool());
    CHECK(got.value(QStringLiteral("timezone")).toString() == QStringLiteral("Europe/London"));
    clearCustomisation();
}

TEST_CASE("No saved customisation yields an empty map", "[imagewriter][customisation]")
{
    clearCustomisation();
    ImageWriter w(nullptr);
    CHECK(w.getSavedCustomisationSettings().isEmpty());
}

TEST_CASE("Duplicate SSH keys are collapsed to one", "[imagewriter][customisation]")
{
    clearCustomisation();
    const QString key1 = QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample1 a@host");
    const QString key2 = QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample2 b@host");
    saveCustomisation({{QStringLiteral("sshAuthorizedKeys"),
                        QStringList{key1, key2, key1}.join(QLatin1Char('\n'))}});

    ImageWriter w(nullptr);
    const QStringList got = w.getSavedCustomisationSettings()
                                .value(QStringLiteral("sshAuthorizedKeys"))
                                .toString()
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    // Order is preserved: the first occurrence wins, so the list the user
    // sees is the one they built.
    REQUIRE(got.size() == 2);
    CHECK(got.at(0) == key1);
    CHECK(got.at(1) == key2);
    clearCustomisation();
}

TEST_CASE("Blank lines and stray whitespace are dropped from SSH keys", "[imagewriter][customisation]")
{
    clearCustomisation();
    const QString key = QStringLiteral("ssh-rsa AAAAB3NzaC1yc2EAAAADAQABExample c@host");
    saveCustomisation({{QStringLiteral("sshAuthorizedKeys"),
                        QStringLiteral("\n  %1  \n\n%1\n\n").arg(key)}});

    ImageWriter w(nullptr);
    const QString got = w.getSavedCustomisationSettings()
                            .value(QStringLiteral("sshAuthorizedKeys")).toString();

    // A padded copy and a bare copy of the same key are the same key.
    CHECK(got == key);
    clearCustomisation();
}

TEST_CASE("A settings map with no SSH keys is left alone", "[imagewriter][customisation]")
{
    clearCustomisation();
    saveCustomisation({{QStringLiteral("hostname"), QStringLiteral("pi")}});

    ImageWriter w(nullptr);
    const QVariantMap got = w.getSavedCustomisationSettings();

    CHECK_FALSE(got.contains(QStringLiteral("sshAuthorizedKeys")));
    CHECK(got.size() == 1);
    clearCustomisation();
}

// ═══════════════════════════════════════════════════════════════════════════
// The OS list the chooser is built from
//
// Everything the user picks in step two comes from this JSON: the top-level
// list, and the sublists behind entries like "Raspberry Pi OS (other)" that
// are fetched separately and spliced in.
//
// The failure that matters is the quiet one. A list that fails to parse, a
// sublist that never arrives, or an entry that is dropped during the splice
// leaves the chooser looking finished but short of options -- and no error
// is shown, because as far as the fetch is concerned nothing went wrong.
//
// The lists here are served from disk, so nothing depends on the network or
// on what the real repository happens to be offering today.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Waits for one of the OS list's terminal outcomes.
struct OsListOutcome
{
    bool prepared = false;
    QStringList errors;
};

OsListOutcome fetchOsList(ImageWriter &w, const QUrl &url, int timeoutMs = 20000)
{
    OsListOutcome out;
    QEventLoop loop;

    // osListPrepared() is emitted again for every sublist that lands, which
    // is after this function has returned and its loop has gone. Binding the
    // connections to a scoped context object severs them on the way out.
    QObject context;
    QObject::connect(&w, &ImageWriter::osListPrepared, &context,
                     [&] { out.prepared = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::error, &context, [&](QVariant m) {
        out.errors << m.toString();
        loop.quit();
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);

    w.refreshOsListFrom(url);
    loop.exec();
    return out;
}

// Spins the event loop until a condition holds, or gives up.
bool waitUntil(const std::function<bool()> &done, int timeoutMs = 20000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

QStringList osNamesIn(const QJsonDocument &doc)
{
    QStringList names;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("os_list")).toArray())
        names << v.toObject().value(QStringLiteral("name")).toString();
    return names;
}

// Writes JSON files into a temp dir and hands back file:// URLs for them.
class OsListDir
{
public:
    OsListDir() { REQUIRE(_dir.isValid()); }

    QUrl put(const QString &name, const QString &json)
    {
        const QString path = QDir(_dir.path()).filePath(name);
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        REQUIRE(f.write(json.toUtf8()) > 0);
        f.close();
        return QUrl::fromLocalFile(path);
    }

    QUrl missing() const
    {
        return QUrl::fromLocalFile(QDir(_dir.path()).filePath(QStringLiteral("absent.json")));
    }

private:
    QTemporaryDir _dir;
};

QString oneOsList()
{
    return QStringLiteral(R"JSON({
      "imager": { "latest_version": "1.9.0" },
      "os_list": [
        { "name": "Raspberry Pi OS (64-bit)", "description": "Recommended",
          "url": "https://example.invalid/a.img.xz", "image_download_size": 100 },
        { "name": "Raspberry Pi OS Lite (64-bit)", "description": "No desktop",
          "url": "https://example.invalid/b.img.xz", "image_download_size": 50 }
      ]
    })JSON");
}

} // namespace

TEST_CASE("A fetched OS list reaches the chooser", "[imagewriter][oslist]")
{
    OsListDir dir;
    ImageWriter w(nullptr);

    const OsListOutcome out = fetchOsList(w, dir.put(QStringLiteral("list.json"), oneOsList()));
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.prepared);

    const QStringList names = osNamesIn(w.getFilteredOSlistDocument());
    CHECK(names.contains(QStringLiteral("Raspberry Pi OS (64-bit)")));
    CHECK(names.contains(QStringLiteral("Raspberry Pi OS Lite (64-bit)")));
}

TEST_CASE("The chooser always offers Erase and Use custom", "[imagewriter][oslist]")
{
    ImageWriter w(nullptr);
    // Before any fetch: the two built-in entries are appended regardless, so
    // a user with no network can still format a card or pick their own file.
    const QJsonDocument doc = w.getFilteredOSlistDocument();

    QStringList urls;
    for (const QJsonValue &v : doc.object().value(QStringLiteral("os_list")).toArray())
        urls << v.toObject().value(QStringLiteral("url")).toString();

    CHECK(urls.contains(QStringLiteral("internal://format")));
    CHECK(urls.contains(QStringLiteral("internal://custom")));
}

TEST_CASE("The built-in entries survive a fetched list", "[imagewriter][oslist]")
{
    OsListDir dir;
    ImageWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("list.json"), oneOsList())).prepared);

    QStringList urls;
    for (const QJsonValue &v : w.getFilteredOSlistDocument().object()
                                   .value(QStringLiteral("os_list")).toArray())
        urls << v.toObject().value(QStringLiteral("url")).toString();

    CHECK(urls.contains(QStringLiteral("internal://format")));
    CHECK(urls.contains(QStringLiteral("internal://custom")));
}

TEST_CASE("A sublist is fetched and spliced into its parent", "[imagewriter][oslist]")
{
    OsListDir dir;
    const QUrl sub = dir.put(QStringLiteral("sub.json"), QStringLiteral(R"JSON({
      "os_list": [
        { "name": "Raspberry Pi OS (Legacy, 32-bit)",
          "url": "https://example.invalid/legacy.img.xz", "image_download_size": 10 }
      ]
    })JSON"));

    const QString top = QStringLiteral(R"JSON({
      "imager": {},
      "os_list": [
        { "name": "Raspberry Pi OS (other)", "description": "More options",
          "subitems_url": "%1" }
      ]
    })JSON").arg(sub.toString());

    ImageWriter w(nullptr);
    const OsListOutcome out = fetchOsList(w, dir.put(QStringLiteral("top.json"), top));
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.prepared);

    // osListPrepared() fires as soon as the top level parses -- the sublist
    // fetches are only queued at that point, so the chooser is briefly
    // showing a parent it cannot expand.
    auto parentOf = [&w]() {
        for (const QJsonValue &v : w.getFilteredOSlistDocument().object()
                                       .value(QStringLiteral("os_list")).toArray())
            if (v.toObject().value(QStringLiteral("name")).toString()
                == QStringLiteral("Raspberry Pi OS (other)"))
                return v.toObject();
        return QJsonObject();
    };
    REQUIRE(waitUntil([&] { return parentOf().contains(QStringLiteral("subitems")); }));

    const QJsonObject parent = parentOf();
    REQUIRE_FALSE(parent.isEmpty());
    CHECK_FALSE(parent.contains(QStringLiteral("subitems_url")));
    REQUIRE(parent.contains(QStringLiteral("subitems")));

    QStringList subNames;
    for (const QJsonValue &v : parent.value(QStringLiteral("subitems")).toArray())
        subNames << v.toObject().value(QStringLiteral("name")).toString();
    CHECK(subNames.contains(QStringLiteral("Raspberry Pi OS (Legacy, 32-bit)")));
}

TEST_CASE("An OS list that will not parse is reported", "[imagewriter][oslist]")
{
    OsListDir dir;
    ImageWriter w(nullptr);

    const OsListOutcome out =
        fetchOsList(w, dir.put(QStringLiteral("bad.json"), QStringLiteral("{ not json at all")));

    // A broken list is not reported through error() -- the UI is told the OS
    // list is unavailable and shows its offline state instead.
    CHECK_FALSE(out.prepared);
    CHECK(waitUntil([&] { return w.isOsListUnavailable(); }));
}

TEST_CASE("An OS list that is not there is reported", "[imagewriter][oslist]")
{
    OsListDir dir;
    ImageWriter w(nullptr);

    const OsListOutcome out = fetchOsList(w, dir.missing());

    CHECK_FALSE(out.prepared);
    CHECK(waitUntil([&] { return w.isOsListUnavailable(); }));
}

TEST_CASE("An OS list with no os_list key yields an empty chooser, not a crash", "[imagewriter][oslist]")
{
    OsListDir dir;
    ImageWriter w(nullptr);

    fetchOsList(w, dir.put(QStringLiteral("empty.json"), QStringLiteral(R"JSON({"imager":{}})JSON")));

    const QStringList names = osNamesIn(w.getFilteredOSlistDocument());
    // Only the two built-ins.
    CHECK(names.size() == 2);
}

// ═══════════════════════════════════════════════════════════════════════════
// Decompressing the image on the way to the card
//
// Every image the chooser offers is compressed, so the extract path is on
// the critical route for every write that is not "Use custom" with a raw
// .img. Each container has its own decoder inside DownloadExtractThread.
//
// A decoder that stops early does not report an error -- the write finishes,
// the card is short of data, and the Pi does not boot. So these check the
// bytes that landed, not the exit status.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// The payload every archive fixture was built from.
QByteArray fixturePayload()
{
    QByteArray out;
    out.reserve(int(kFixturePayload));
    for (int i = 0; i < int(kFixturePayload); ++i)
        out.append(char(65 + (qint64(i) * 7) % 26));
    return out;
}

// Copies an archive fixture next to a target big enough to take it.
class ExtractFixture
{
public:
    explicit ExtractFixture(const QString &archiveName)
    {
        REQUIRE(_dir.isValid());
        const QString from = QStringLiteral(IMAGER_TEST_DATA_DIR "/") + archiveName;
        REQUIRE(QFile::exists(from));
        _archive = QDir(_dir.path()).filePath(archiveName);
        REQUIRE(QFile::copy(from, _archive));

        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));
        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        REQUIRE(t.write(QByteArray(int(kFixturePayload), '\0')) == qint64(kFixturePayload));
        t.close();
    }

    QUrl archiveUrl() const { return QUrl::fromLocalFile(_archive); }
    QString target() const { return _target; }

    QByteArray written() const
    {
        QFile f(_target);
        REQUIRE(f.open(QIODevice::ReadOnly));
        return f.read(qint64(kFixturePayload));
    }

private:
    QTemporaryDir _dir;
    QString _archive, _target;
};

void checkExtractsCleanly(const QString &archiveName)
{
    ExtractFixture fx(archiveName);
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);
    REQUIRE(w.readyToWrite());

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // Byte-for-byte, not "no error was reported".
    CHECK(fx.written() == fixturePayload());
}

} // namespace

TEST_CASE("An xz image is decompressed onto the card", "[imagewriter][extract]")
{
    checkExtractsCleanly(QStringLiteral("pattern-1MiB.img.xz"));
}

TEST_CASE("A gzipped image is decompressed onto the card", "[imagewriter][extract]")
{
    checkExtractsCleanly(QStringLiteral("pattern-1MiB.img.gz"));
}

TEST_CASE("A zstd image is decompressed onto the card", "[imagewriter][extract]")
{
    checkExtractsCleanly(QStringLiteral("pattern-1MiB.img.zst"));
}

TEST_CASE("A zipped image is decompressed onto the card", "[imagewriter][extract]")
{
    checkExtractsCleanly(QStringLiteral("pattern-1MiB.img.zip"));
}

TEST_CASE("A truncated archive fails rather than writing a short image", "[imagewriter][extract]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Half an xz stream: enough to start decoding, not enough to finish.
    QFile src(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"));
    REQUIRE(src.open(QIODevice::ReadOnly));
    const QByteArray whole = src.readAll();
    src.close();
    REQUIRE(whole.size() > 64);

    const QString truncated = QDir(dir.path()).filePath(QStringLiteral("cut.img.xz"));
    QFile t(truncated);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.write(whole.left(whole.size() / 2)) > 0);
    t.close();

    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile tf(target);
    REQUIRE(tf.open(QIODevice::WriteOnly));
    REQUIRE(tf.write(QByteArray(int(kFixturePayload), '\0')) == qint64(kFixturePayload));
    tf.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(truncated), 0, kFixturePayload);
    w.setDst(target, kFixturePayload);

    const WriteOutcome out = runWrite(w);

    QFile got(target);
    REQUIRE(got.open(QIODevice::ReadOnly));
    const QByteArray landed = got.read(qint64(kFixturePayload));
    got.close();

    const QByteArray expected = fixturePayload();
    int matching = 0;
    while (matching < landed.size() && landed.at(matching) == expected.at(matching))
        ++matching;

    INFO("reported success: " << out.succeeded << ", failed: " << out.failed);
    INFO("bytes matching the real image: " << matching << " of " << expected.size());

    // The user must be told. A silent success here is a card that looks
    // written and will not boot -- which is what happened before the guard in
    // LocalFileExtractThread::run(): libarchive could not extract the
    // truncated stream, the file was taken for a raw disk image, and its
    // compressed bytes were copied onto the card verbatim.
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
    CHECK(matching < expected.size());
}

TEST_CASE("A verified write lands the right bytes", "[imagewriter][extract]")
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    CHECK(fx.written() == fixturePayload());
}

// ═══════════════════════════════════════════════════════════════════════════
// Getting the customisation onto the card
//
// Steps three and four of the common path meet here. The user fills in the
// customisation dialog, the write runs, and the settings only take effect if
// the generated files actually land in the boot partition of the image that
// was just written.
//
// Nothing checked that they did. The generator has its own tests and the
// write path has its own tests, but the join between them -- the point where
// firstrun.sh and cmdline.txt are inserted into a FAT32 partition that has
// just been laid down sector by sector -- did not.
//
// When it fails there is no error: the card boots, and none of the hostname,
// user, Wi-Fi or SSH settings the user entered are there.
//
// The fixture is a real 48 MiB image with an MBR and an empty FAT32 boot
// partition. The oracle is the bytes on the target: both the 8.3 directory
// entry and the file's contents have to be findable in the written image.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

class BootPartitionFixture
{
public:
    BootPartitionFixture()
    {
        REQUIRE(_dir.isValid());
        const QString from = QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz");
        REQUIRE(QFile::exists(from));
        _source = QDir(_dir.path()).filePath(QStringLiteral("fat32.img.xz"));
        REQUIRE(QFile::copy(from, _source));

        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));
        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        REQUIRE(t.resize(qint64(kImageSize)));
        t.close();
    }

    static constexpr quint64 kImageSize = 48 * 1024 * 1024;

    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString target() const { return _target; }

    QByteArray targetBytes() const
    {
        QFile f(_target);
        REQUIRE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }

private:
    QTemporaryDir _dir;
    QString _source, _target;
};

// A long filename is stored UTF-16LE, but split across three fields inside
// each 32-byte directory entry (10 bytes, then 12, then 4), so the whole name
// is never contiguous in the image. The first five characters are, and that
// is enough to find the entry without guessing how the 8.3 alias was mangled
// -- "rpi-preseed.toml" becomes something like RPI-PR~1TOM.
QByteArray longNameHead(const QString &name)
{
    QByteArray out;
    for (const QChar c : name.left(5)) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char((c.unicode() >> 8) & 0xFF));
    }
    return out;
}

WriteOutcome writeWithCustomisation(BootPartitionFixture &fx,
                                    const QByteArray &config,
                                    const QByteArray &cmdline,
                                    const QByteArray &firstrun,
                                    const QByteArray &initFormat = QByteArray("systemd"))
{
    static ImageWriter *keepAlive = nullptr;
    Q_UNUSED(keepAlive)

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize);
    w.setDst(fx.target(), BootPartitionFixture::kImageSize);
    w.setImageCustomisation(config, cmdline, firstrun, QByteArray(), QByteArray(),
                            ImageOptions::NoAdvancedOptions, initFormat);
    REQUIRE(w.readyToWrite());
    return runWrite(w);
}

} // namespace

TEST_CASE("A firstrun script lands in the boot partition", "[imagewriter][customisation][boot]")
{
    BootPartitionFixture fx;
    const QByteArray script = "#!/bin/bash\n# rpi-imager-test-marker-firstrun\nexit 0\n";

    const WriteOutcome out = writeWithCustomisation(fx, QByteArray(), QByteArray(), script);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    // The 8.3 directory entry FAT stores for "firstrun.sh".
    CHECK(written.contains(QByteArray("FIRSTRUNSH")));
    // ...and the script itself, not just a zero-length entry.
    CHECK(written.contains(QByteArray("rpi-imager-test-marker-firstrun")));
}

TEST_CASE("A cmdline append lands in the boot partition", "[imagewriter][customisation][boot]")
{
    BootPartitionFixture fx;
    const QByteArray script = "#!/bin/bash\nexit 0\n";
    const QByteArray cmdline = " rpi_imager_test_marker=cmdline";

    const WriteOutcome out = writeWithCustomisation(fx, QByteArray(), cmdline, script);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // cmdline.txt is how the kernel is told to run the firstrun script; an
    // append that is dropped means the script never executes.
    CHECK(fx.targetBytes().contains(QByteArray("rpi_imager_test_marker=cmdline")));
}

TEST_CASE("Config entries land in the boot partition", "[imagewriter][customisation][boot]")
{
    BootPartitionFixture fx;
    const QByteArray config = "dtparam=rpi_imager_test_marker=on\n";

    const WriteOutcome out = writeWithCustomisation(fx, config, QByteArray(), QByteArray());
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    CHECK(fx.targetBytes().contains(QByteArray("dtparam=rpi_imager_test_marker=on")));
}

TEST_CASE("A write with no customisation leaves the image alone", "[imagewriter][customisation][boot]")
{
    BootPartitionFixture fx;

    const WriteOutcome out = writeWithCustomisation(fx, QByteArray(), QByteArray(), QByteArray());
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // Nothing was asked for, so nothing should have been inserted.
    const QByteArray written = fx.targetBytes();
    CHECK_FALSE(written.contains(QByteArray("FIRSTRUNSH")));
}

// ═══════════════════════════════════════════════════════════════════════════
// Writing an image the user chose from the OS list
//
// Everything above hands startWrite() a local file, which skips the half of
// it that exists for a remote image: the hash the manifest promised, the
// cache the download is written into as it goes, and the download progress
// the UI shows for minutes at a time.
//
// That is the path almost every real write takes. A hash check that does not
// happen means a corrupted download is written to the card and reported as
// successful, which is the same silent failure as a truncated archive but
// arrives over the wire instead of off the disk.
//
// A throwaway server on loopback reaches all of it without a network.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("An image fetched over HTTP is written to the card", "[imagewriter][http]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"),
                        QDir(served.path()).filePath(QStringLiteral("os.img.xz"))));

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl(QString::fromUtf8(server.urlFor(QStringLiteral("os.img.xz")))),
             0, kFixturePayload, sha256HexOf(fixturePayload()));
    w.setDst(target, kFixturePayload);
    REQUIRE(w.readyToWrite());

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    QFile got(target);
    REQUIRE(got.open(QIODevice::ReadOnly));
    CHECK(got.read(qint64(kFixturePayload)) == fixturePayload());
}

TEST_CASE("A download whose hash does not match is refused", "[imagewriter][http]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"),
                        QDir(served.path()).filePath(QStringLiteral("os.img.xz"))));

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // The manifest promises a hash the served bytes do not have -- a
    // corrupted mirror, or the wrong file behind the right URL.
    w.setSrc(QUrl(QString::fromUtf8(server.urlFor(QStringLiteral("os.img.xz")))),
             0, kFixturePayload,
             QByteArray("0000000000000000000000000000000000000000000000000000000000000000"));
    w.setDst(target, kFixturePayload);

    const WriteOutcome out = runWrite(w);

    // Reporting success here writes an image nobody vouched for.
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
}

TEST_CASE("An image that is not on the server is reported", "[imagewriter][http]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl(QString::fromUtf8(server.urlFor(QStringLiteral("absent.img.xz")))),
             0, kFixturePayload);
    w.setDst(target, kFixturePayload);

    const WriteOutcome out = runWrite(w);

    // A 404 must be an error, not an empty card written successfully.
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
}

TEST_CASE("Download progress is reported to the UI", "[imagewriter][http]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"),
                        QDir(served.path()).filePath(QStringLiteral("os.img.xz"))));

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl(QString::fromUtf8(server.urlFor(QStringLiteral("os.img.xz")))),
             0, kFixturePayload, sha256HexOf(fixturePayload()));
    w.setDst(target, kFixturePayload);

    const WriteOutcome out = runWrite(w);
    REQUIRE(out.succeeded);

    // Without this the progress bar sits at zero for the whole download and
    // the write looks hung.
    bool sawDownload = false;
    for (const QString &k : out.progressKinds)
        if (k.startsWith(QStringLiteral("download ")))
            sawDownload = true;
    CHECK(sawDownload);
}

// ═══════════════════════════════════════════════════════════════════════════
// A zstd image whose size is not recorded in the frame
//
// zstd only writes the uncompressed size into the frame header when it knows
// it up front. Compress by piping -- "cat x.img | zstd" -- and it does not,
// so ZSTD_findDecompressedSize() returns ZSTD_CONTENTSIZE_UNKNOWN, which is
// (0ULL - 1) rather than 0 or a negative.
//
// Taken as a size, that is 16 exabytes. The user sees a card described as
// needing 16,777,216 TB, and the capacity check in startWrite() refuses a
// perfectly good image (raspberrypi/rpi-imager#1726).
//
// imagesizeparser has its own case for this. This one is the level the user
// actually meets it at: the size ImageWriter derives, and whether the write
// is allowed to start.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A zstd image with no recorded size does not report a huge one", "[imagewriter][archive][zstd]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB-nofcs.img.zst"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    // Unknown is unknown: the progress bar falls back to the download size
    // rather than scaling against a number from nowhere.
    CHECK_FALSE(w.isExtractSizeKnown());
}

TEST_CASE("A zstd image with no recorded size is not refused for capacity", "[imagewriter][archive][zstd]")
{
    ArchiveFixture fx(QStringLiteral("pattern-1MiB-nofcs.img.zst"));
    ImageWriter w(nullptr);
    w.setSrc(fx.archiveUrl());

    // This is the reported symptom. An unknown size must not become
    // ULLONG_MAX and fail the "will it fit" comparison against every card
    // the user owns.
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), 8ULL * 1024 * 1024 * 1024));
    CHECK_FALSE(refusedAsTooSmall(w, fx.target(), 2ULL * 1024 * 1024));
}

TEST_CASE("A zstd image with no recorded size still writes", "[imagewriter][archive][zstd]")
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB-nofcs.img.zst"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.archiveUrl());
    w.setDst(fx.target(), kFixturePayload);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // Not knowing the size up front must not change the bytes written.
    CHECK(fx.written() == fixturePayload());
}

// ═══════════════════════════════════════════════════════════════════════════
// Writing to a real block device
//
// Every case above writes to a regular file, which is enough to check the
// bytes but skips what makes a card different: the alignment rules, the
// direct-I/O path, the discard before the write, and reading the size back
// out of the kernel rather than off a QFileInfo.
//
// Those are the parts that fail on a real card and cannot fail on a file.
//
// Runs against RPI_IMAGER_TEST_BLOCK_DEVICE and skips when it is unset, and
// refuses anything that is not a loop device whatever the environment says.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

} // namespace

TEST_CASE("An image is written to a real block device", "[imagewriter][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("os.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, kFixturePayload);
    w.setDst(dev, kFixturePayload);
    REQUIRE(w.readyToWrite());

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    QFile card(dev);
    REQUIRE(card.open(QIODevice::ReadOnly));
    CHECK(card.read(qint64(kFixturePayload)) == fixturePayload());
}

TEST_CASE("A verified write to a real block device reads back clean", "[imagewriter][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("os.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"), source));

    ImageWriter w(nullptr);
    // Verification re-reads the card through the same device path, which is
    // where a caching or alignment mistake shows up as a false mismatch.
    w.setVerifyEnabled(true);
    w.setSrc(QUrl::fromLocalFile(source), 0, kFixturePayload);
    w.setDst(dev, kFixturePayload);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("An image that supports no customisation is written without any",
          "[imagewriter][customisation]")
{
    // Settings from a previous selection must not reach an image that cannot
    // read them.
    //
    // This replaces a case named "Applying customisation to an image that
    // supports none clears it", whose only assertion was that nothing threw.
    // Writing the card and looking is the way to see it, since the staged
    // payloads have no accessor.
    //
    // What it pins is the outcome, not the mechanism. Two things guard it:
    // applyCustomisationFromSettings() drops the staged payloads when the
    // image declares no init format, and DownloadThread writes none of those
    // files unless the format asks for them. Removing the first leaves this
    // passing, because the second still holds -- so the name says what is
    // actually verified rather than which guard did it.
    BootPartitionFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);

    // First an image that does support customisation, with something
    // recognisable staged against it.
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize, QByteArray(), false,
             QString(), QStringLiteral("Supported OS"), QByteArray("systemd"));
    w.setImageCustomisation(QByteArray(), QByteArray(),
                            "#!/bin/bash\n# marker-should-be-cleared\nexit 0\n",
                            QByteArray(), QByteArray(),
                            ImageOptions::NoAdvancedOptions, QByteArray("systemd"));

    // Then the user picks an image that reads none of it. Re-applying the
    // settings against that image has to drop what was staged.
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize, QByteArray(), false,
             QString(), QStringLiteral("Plain OS"), QByteArray());
    REQUIRE_FALSE(w.imageSupportsCustomization());

    QVariantMap settings;
    settings.insert(QStringLiteral("hostname"), QStringLiteral("test-pi"));
    w.applyCustomisationFromSettings(settings);

    w.setDst(fx.target(), BootPartitionFixture::kImageSize);
    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    // Nothing from the earlier selection reached the card.
    QFile card(fx.target());
    REQUIRE(card.open(QIODevice::ReadOnly));
    const QByteArray written = card.readAll();
    CHECK_FALSE(written.contains(QByteArray("marker-should-be-cleared")));
    CHECK_FALSE(written.contains(QByteArray("test-pi")));
}

TEST_CASE("Customisation reaches a real card", "[imagewriter][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("fat32.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, BootPartitionFixture::kImageSize);
    w.setDst(dev, BootPartitionFixture::kImageSize);
    w.setImageCustomisation(QByteArray(), QByteArray(),
                            "#!/bin/bash\n# rpi-imager-device-marker\nexit 0\n",
                            QByteArray(), QByteArray(),
                            ImageOptions::NoAdvancedOptions, QByteArray("systemd"));

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // The FAT partition is edited in place after the image lands; on a block
    // device that is a re-open and a seek, not a rewrite of the file.
    QFile card(dev);
    REQUIRE(card.open(QIODevice::ReadOnly));
    const QByteArray written = card.read(qint64(BootPartitionFixture::kImageSize));
    CHECK(written.contains(QByteArray("rpi-imager-device-marker")));
    card.close();

    // Finding the bytes somewhere in 48 MiB is weaker than it looks: it holds
    // just as well if the script was written outside any directory entry,
    // where the OS would never run it. Ask an independent FAT reader whether
    // firstrun.sh is genuinely a file in the root, and whether it contains
    // what was asked for.
    if (QFileInfo::exists(QStringLiteral("/usr/bin/mtype"))) {
        // The image is a partitioned disk, so the filesystem does not start
        // at sector zero. Take the offset from the card's own partition table
        // rather than assuming it, so this keeps working if the fixture is
        // ever rebuilt with a different layout.
        REQUIRE(card.open(QIODevice::ReadOnly));
        const QByteArray mbr = card.read(512);
        card.close();
        REQUIRE(mbr.size() == 512);
        REQUIRE(static_cast<quint8>(mbr[510]) == 0x55);
        REQUIRE(static_cast<quint8>(mbr[511]) == 0xAA);

        quint32 firstLba = 0;
        for (int i = 0; i < 4; ++i)
            firstLba |= static_cast<quint32>(static_cast<quint8>(mbr[0x1BE + 8 + i])) << (8 * i);
        REQUIRE(firstLba > 0);

        QProcess mtype;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("MTOOLS_SKIP_CHECK"), QStringLiteral("1"));
        mtype.setProcessEnvironment(env);
        const QString atOffset =
            dev + QStringLiteral("@@") + QString::number(qint64(firstLba) * 512);
        mtype.start(QStringLiteral("/usr/bin/mtype"),
                    {QStringLiteral("-i"), atOffset, QStringLiteral("::firstrun.sh")});
        REQUIRE(mtype.waitForFinished(rpi_test::kFixtureProcessTimeoutMs));

        const QByteArray body = mtype.readAllStandardOutput();
        INFO("mtype stderr: " << QString::fromUtf8(mtype.readAllStandardError()).toStdString());
        CHECK(mtype.exitCode() == 0);
        CHECK(body.contains(QByteArray("rpi-imager-device-marker")));
    }
}

// Each container the picker accepts, truncated. The guard in
// LocalFileExtractThread::run() has to fire for all of them, not just the xz
// that found it -- a corrupt .gz or .zip copied raw onto the card is the same
// unbootable result.
namespace {

QString truncatedCopyOf(const QTemporaryDir &dir, const QString &fixture)
{
    QFile src(QStringLiteral(IMAGER_TEST_DATA_DIR "/") + fixture);
    REQUIRE(src.open(QIODevice::ReadOnly));
    const QByteArray whole = src.readAll();
    src.close();
    REQUIRE(whole.size() > 64);

    const QString cut = QDir(dir.path()).filePath(fixture);
    QFile out(cut);
    REQUIRE(out.open(QIODevice::WriteOnly));
    REQUIRE(out.write(whole.left(whole.size() / 2)) > 0);
    out.close();
    return cut;
}

void checkTruncatedIsRefused(const QString &fixture)
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString cut = truncatedCopyOf(dir, fixture);

    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(cut), 0, kFixturePayload);
    w.setDst(target, kFixturePayload);

    const WriteOutcome out = runWrite(w);
    INFO("fixture: " << fixture.toStdString());
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
}

} // namespace

// The family had gz, zip and zstd and not xz -- the format every Raspberry
// Pi image actually ships in. The fixture was already here, used by the
// case that writes a whole one; the missing member was a single line.
//
// What this reaches, and what it does not. Cut to half, the container will
// not open at all, and the guard that fires is the one for a file whose
// name claims compression and which libarchive cannot read -- the same
// guard its three siblings meet. That is worth having and is not the bug
// that was found here.
//
// The bug needed an archive that opens cleanly and then runs out: libarchive
// words that "No progress is possible", the extractor matched the string and
// read it as the end of the data, and half an image was written and called a
// success. It cannot be reached from this fixture, which is 316 bytes -- a
// megabyte of repeating pattern compresses to almost nothing, so there is no
// tail to remove without taking the header with it. Reproducing it needs
// incompressible data, which cli_process_test builds at runtime with xz;
// that case fails without the fix and this one does not.
TEST_CASE("A truncated xz is refused", "[imagewriter][extract]")
{
    checkTruncatedIsRefused(QStringLiteral("pattern-1MiB.img.xz"));
}

TEST_CASE("A truncated gzip is refused", "[imagewriter][extract]")
{
    checkTruncatedIsRefused(QStringLiteral("pattern-1MiB.img.gz"));
}

TEST_CASE("A truncated zip is refused", "[imagewriter][extract]")
{
    checkTruncatedIsRefused(QStringLiteral("pattern-1MiB.img.zip"));
}

TEST_CASE("A truncated zstd is refused", "[imagewriter][extract]")
{
    checkTruncatedIsRefused(QStringLiteral("pattern-1MiB.img.zst"));
}

// ═══════════════════════════════════════════════════════════════════════════
// Deciding when to look for a new OS list
//
// The repository can ask the imager to re-fetch on an interval, with jitter
// so a fleet of machines does not arrive at the same second. The imager
// reschedules after every successful top-level fetch, and a command-line
// override can replace whatever the list asked for.
//
// Both ways of being wrong are quiet. Too eager and every installation
// hammers the repository on a fixed cadence; not at all and a machine left
// running for weeks keeps offering images that have been superseded, with no
// indication the list is stale.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// _osListRefreshTimer is protected, which is enough to read the schedule back
// without adding an accessor to the shipping class.
class RefreshObservingWriter : public ImageWriter
{
public:
    using ImageWriter::ImageWriter;

    bool refreshScheduled() const { return _osListRefreshTimer.isActive(); }
    int refreshIntervalMs() const { return _osListRefreshTimer.interval(); }
};

QString osListWithRefresh(const QString &imagerFields)
{
    return QStringLiteral(R"JSON({
      "imager": { %1 },
      "os_list": [
        { "name": "Raspberry Pi OS (64-bit)", "url": "https://example.invalid/a.img.xz",
          "image_download_size": 100 }
      ]
    })JSON").arg(imagerFields);
}

} // namespace

TEST_CASE("A list that asks for no refresh schedules none", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"latest_version\":\"1.0\"")))).prepared);

    CHECK_FALSE(w.refreshScheduled());
}

TEST_CASE("A list that asks for a refresh gets one scheduled", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"refresh_interval_minutes\": 60")))).prepared);

    REQUIRE(w.refreshScheduled());
    // No jitter asked for, so the interval is exactly what was requested.
    CHECK(w.refreshIntervalMs() == 60 * 60 * 1000);
}

TEST_CASE("Jitter stays inside the window the list asked for", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral(
                                       "\"refresh_interval_minutes\": 60, \"refresh_jitter_minutes\": 10")))).prepared);

    REQUIRE(w.refreshScheduled());
    const int base = 60 * 60 * 1000;
    // Jitter exists to spread a fleet out; overshooting it would delay the
    // refresh past the window the repository budgeted for.
    INFO("interval: " << w.refreshIntervalMs());
    CHECK(w.refreshIntervalMs() >= base);
    CHECK(w.refreshIntervalMs() <= base + 10 * 60 * 1000);
}

TEST_CASE("A command-line override replaces what the list asked for", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    w.setOsListRefreshOverride(30, 0);

    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"refresh_interval_minutes\": 600")))).prepared);

    REQUIRE(w.refreshScheduled());
    CHECK(w.refreshIntervalMs() == 30 * 60 * 1000);
}

TEST_CASE("An override applied after the list reschedules immediately", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"refresh_interval_minutes\": 600")))).prepared);
    REQUIRE(w.refreshIntervalMs() == 600 * 60 * 1000);

    // Setting the override once a list is already loaded has to take effect
    // without waiting for the next fetch.
    w.setOsListRefreshOverride(15, 0);
    CHECK(w.refreshIntervalMs() == 15 * 60 * 1000);
}

TEST_CASE("A refresh interval of zero cancels the schedule", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"refresh_interval_minutes\": 60")))).prepared);
    REQUIRE(w.refreshScheduled());

    w.setOsListRefreshOverride(0, 0);
    CHECK_FALSE(w.refreshScheduled());
}

TEST_CASE("An absurd refresh interval is capped", "[imagewriter][refresh]")
{
    OsListDir dir;
    RefreshObservingWriter w(nullptr);
    REQUIRE(fetchOsList(w, dir.put(QStringLiteral("l.json"),
                                   osListWithRefresh(QStringLiteral("\"refresh_interval_minutes\": 99999999")))).prepared);

    REQUIRE(w.refreshScheduled());
    // QTimer's interval is an int, so the ceiling is INT_MAX milliseconds --
    // about 24.8 days. A cap above that wraps negative and the timer then
    // fires every millisecond, re-fetching the list in a tight loop.
    CHECK(w.refreshIntervalMs() == 24 * 24 * 60 * 60 * 1000);
    CHECK(w.refreshIntervalMs() > 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// What a link from outside is allowed to change
//
// The repository half of handleIncomingUrl() is covered above. The rest is
// not: a double-clicked manifest, and the Connect auth key.
//
// The key is written into the firstrun script on the card, so it decides
// which organisation a device enrols into. Accepting one in the wrong shape,
// or quietly replacing one the user already set up, both end with a Pi
// enrolled somewhere nobody chose.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A double-clicked manifest is loaded", "[imagewriter][url]")
{
    OsListDir dir;
    ImageWriter w(nullptr);

    QEventLoop loop;
    QObject context;
    bool prepared = false;
    QObject::connect(&w, &ImageWriter::osListPrepared, &context,
                     [&] { prepared = true; loop.quit(); });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(20000);

    // A local file the user opened deliberately is trusted and loaded
    // without the confirmation a remote link needs.
    w.handleIncomingUrl(dir.put(QStringLiteral("list.json"), oneOsList()));
    loop.exec();

    REQUIRE(prepared);
    CHECK(osNamesIn(w.getFilteredOSlistDocument())
              .contains(QStringLiteral("Raspberry Pi OS (64-bit)")));
}

TEST_CASE("A trailing newline does not sneak a repo URL through", "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    rpi_test::SignalLog repos(&w, &ImageWriter::repositoryUrlReceived);

    // A URL copied out of a browser carries one, and PCRE2's '$' matches
    // before a final newline -- which is how issue #1687 got in. The pattern
    // is anchored with \A..\z for exactly this.
    w.handleIncomingUrl(QUrl(QStringLiteral(
        "rpi-imager://open?repo=https%3A%2F%2Fexample.com%2Flist.json%0A")));

    CHECK(repos.isEmpty());
}

TEST_CASE("A well-formed auth key is accepted", "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    w.clearConnectToken();

    // rpuak_ followed by 24 Base58 characters is the shape Connect issues.
    const QString key = QStringLiteral("rpuak_123456789ABCDEFGHJKLMNPQ");
    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=") + key));

    CHECK(w.getRuntimeConnectToken() == key);
    w.clearConnectToken();
}

TEST_CASE("A malformed auth key is ignored", "[imagewriter][url]")
{
    ImageWriter w(nullptr);

    const QStringList rejected{
        QStringLiteral("nopreflx_123456789ABCDEFGHJKLMNP"),   // wrong prefix
        QStringLiteral("rpuak_short"),                        // payload too short
        QStringLiteral("rpuak_0OIl456789ABCDEFGHJKLMNPQ"),    // 0 O I l are not Base58
        QStringLiteral("rpuak_"),                             // no payload at all
    };

    for (const QString &bad : rejected) {
        INFO("candidate: " << bad.toStdString());
        w.clearConnectToken();
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=")
                                 + QUrl::toPercentEncoding(bad)));
        CHECK(w.getRuntimeConnectToken().isEmpty());
    }
    w.clearConnectToken();
}

TEST_CASE("A second, different auth key raises a conflict", "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    w.clearConnectToken();

    const QString first = QStringLiteral("rpuak_123456789ABCDEFGHJKLMNPQ");
    const QString second = QStringLiteral("rpuak_987654321ABCDEFGHJKLMNPQ");

    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=") + first));
    REQUIRE(w.getRuntimeConnectToken() == first);

    rpi_test::SignalLog conflicts(&w, &ImageWriter::connectTokenConflictDetected);
    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=") + second));

    // Replacing it silently would enrol the next card into a different
    // account than the one the user set up. QML gets to ask.
    REQUIRE(conflicts.count() == 1);
    CHECK(conflicts.at(0).at(0).toString() == second);
    CHECK(w.getRuntimeConnectToken() == first);

    w.clearConnectToken();
}

TEST_CASE("The same auth key arriving twice is not a conflict", "[imagewriter][url]")
{
    ImageWriter w(nullptr);
    w.clearConnectToken();

    const QString key = QStringLiteral("rpuak_123456789ABCDEFGHJKLMNPQ");
    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=") + key));
    REQUIRE(w.getRuntimeConnectToken() == key);

    // Following the same link again is ordinary; prompting for it would be
    // noise.
    rpi_test::SignalLog conflicts(&w, &ImageWriter::connectTokenConflictDetected);
    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://open?auth_key=") + key));

    CHECK(conflicts.isEmpty());
    CHECK(w.getRuntimeConnectToken() == key);
    w.clearConnectToken();
}

// ═══════════════════════════════════════════════════════════════════════════
// The debug switches, and the multi-file write
//
// The debug toggles are not developer-only: they are what support asks a
// user to change when a write fails on their machine. Direct I/O off,
// periodic sync off, async I/O off, a shallower queue, device limits
// ignored. Each one has to still produce a correct card, or the advice makes
// things worse.
//
// The multi-file write is the other shape of write entirely. An archive with
// several files in it -- which is how the OS list ships EEPROM and bootloader
// images -- is not written as a disk image at all: the card is formatted and
// the files are copied onto the filesystem.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

WriteOutcome writeWithToggles(const std::function<void(ImageWriter &)> &configure)
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    configure(w);
    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);

    WriteOutcome out = runWrite(w);
    if (out.succeeded)
        REQUIRE(fx.written() == fixturePayload());
    return out;
}

} // namespace

TEST_CASE("A write with direct I/O disabled still lands correctly", "[imagewriter][toggles]")
{
    const WriteOutcome out = writeWithToggles([](ImageWriter &w) { w.setDebugDirectIO(false); });
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("A write with async I/O disabled still lands correctly", "[imagewriter][toggles]")
{
    // The sync fallback support recommends when io_uring misbehaves.
    const WriteOutcome out = writeWithToggles([](ImageWriter &w) { w.setDebugAsyncIO(false); });
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("A write with periodic sync disabled still lands correctly", "[imagewriter][toggles]")
{
    const WriteOutcome out = writeWithToggles([](ImageWriter &w) { w.setDebugPeriodicSync(false); });
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("A write with a shallow async queue still lands correctly", "[imagewriter][toggles]")
{
    const WriteOutcome out = writeWithToggles([](ImageWriter &w) { w.setDebugAsyncQueueDepth(2); });
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("A write ignoring device limits still lands correctly", "[imagewriter][toggles]")
{
    // This one reallocates the ring buffers after the device is opened,
    // which is a different allocation path than every other write takes.
    const WriteOutcome out =
        writeWithToggles([](ImageWriter &w) { w.setDebugIgnoreDeviceLimits(true); });
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

TEST_CASE("The debug toggles read back what was set", "[imagewriter][toggles]")
{
    ImageWriter w(nullptr);

    w.setDebugDirectIO(false);
    w.setDebugPeriodicSync(false);
    w.setDebugAsyncIO(false);
    w.setDebugAsyncQueueDepth(4);
    w.setDebugIgnoreDeviceLimits(true);
    w.setDebugVerboseLogging(true);

    // QML binds to these; a setter that does not stick leaves the dialog
    // showing one thing and the write doing another.
    CHECK_FALSE(w.getDebugDirectIO());
    CHECK_FALSE(w.getDebugPeriodicSync());
    CHECK_FALSE(w.getDebugAsyncIO());
    CHECK(w.getDebugAsyncQueueDepth() == 4);
    CHECK(w.getDebugIgnoreDeviceLimits());
    CHECK(w.getDebugVerboseLogging());
}

TEST_CASE("A multi-file archive that cannot be mounted reports it", "[imagewriter][multifile]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString archive = QDir(dir.path()).filePath(QStringLiteral("bootloader.zip"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/bootloader-3files.zip"), archive));

    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(64 * 1024 * 1024));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // multifilesinzip: the card is formatted and the files copied onto the
    // filesystem, rather than the archive being written as a disk image.
    w.setSrc(QUrl::fromLocalFile(archive), 0, 0, QByteArray(), true);
    w.setDst(target, 64 * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 90000);

    // Mounting the freshly formatted partition needs privileges the imager
    // does not have here, so this ends in an error -- which is the point:
    // it has to be an error the user sees, not a silent success leaving a
    // formatted card with no bootloader files on it.
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
}

// ═══════════════════════════════════════════════════════════════════════════
// The performance report
//
// Every write records timings, throughput samples and phase transitions, and
// the user can export the lot as JSON. It is what gets attached to a bug
// report when somebody says a write was slow or stalled, so the export is
// only worth having if it actually describes the write that just happened.
//
// PerformanceStats has its own tests for the recording. This covers the
// export as ImageWriter drives it: after a real write, with everything a
// real write produces in it.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Exporting with nothing recorded is refused", "[imagewriter][perf]")
{
    ImageWriter w(nullptr);

    // Nothing to report yet; offering a file dialog would be confusing.
    CHECK_FALSE(w.exportPerformanceData());
}

TEST_CASE("A write produces an exportable report", "[imagewriter][perf]")
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(true);
    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString report = QDir(dir.path()).filePath(QStringLiteral("report.json"));
    REQUIRE(w.exportPerformanceDataToFile(report));

    QFile f(report);
    REQUIRE(f.open(QIODevice::ReadOnly));
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);

    // A report that will not parse is one nobody can read on the other end
    // of a bug report.
    INFO("parse error: " << err.errorString().toStdString());
    REQUIRE(err.error == QJsonParseError::NoError);
    REQUIRE(doc.isObject());

    const QJsonObject root = doc.object();
    CHECK_FALSE(root.isEmpty());

    // The write that just happened has to be in there: a session, and the
    // events recorded during it.
    bool sawEvents = false;
    for (auto it = root.constBegin(); it != root.constEnd(); ++it)
        if (it.value().isArray() && !it.value().toArray().isEmpty())
            sawEvents = true;
    CHECK(sawEvents);
}

TEST_CASE("A write leaves imaging data to report", "[imagewriter][perf]")
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);

    // Before: background events only, if anything.
    CHECK_FALSE(w.performanceStats()->hasImagingData());

    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);
    REQUIRE(runWrite(w).succeeded);

    // After: the write itself is recorded, which is the difference between a
    // report worth attaching to a bug and one that describes nothing.
    CHECK(w.performanceStats()->hasData());
    CHECK(w.performanceStats()->hasImagingData());
}

TEST_CASE("Exporting to an unwritable path fails rather than pretending", "[imagewriter][perf]")
{
    ExtractFixture fx(QStringLiteral("pattern-1MiB.img.xz"));
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.archiveUrl(), 0, kFixturePayload);
    w.setDst(fx.target(), kFixturePayload);
    REQUIRE(runWrite(w).succeeded);

    // Telling the user the report was saved when it was not leaves them
    // attaching nothing to their bug report.
    CHECK_FALSE(w.exportPerformanceDataToFile(
        QStringLiteral("/proc/definitely/not/writable/report.json")));
}

TEST_CASE("A verified customised write checks the files landed", "[imagewriter][customisation][boot]")
{
    BootPartitionFixture fx;
    ImageWriter w(nullptr);
    // Verification re-opens the card and reads the customisation back. It is
    // the only thing standing between "the files were written" and "the
    // files are on the card" -- a distinction that matters when the write
    // was buffered and the card pulled early.
    w.setVerifyEnabled(true);
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize);
    w.setDst(fx.target(), BootPartitionFixture::kImageSize);
    w.setImageCustomisation(QByteArray("dtparam=audio=on\n"),
                            QByteArray(" rpi_imager_verified=1"),
                            QByteArray("#!/bin/bash\n# rpi-imager-verified-marker\nexit 0\n"),
                            QByteArray(), QByteArray(),
                            ImageOptions::NoAdvancedOptions, QByteArray("systemd"));

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    CHECK(written.contains(QByteArray("rpi-imager-verified-marker")));
    CHECK(written.contains(QByteArray("rpi_imager_verified=1")));
    CHECK(written.contains(QByteArray("dtparam=audio=on")));
}


// ═══════════════════════════════════════════════════════════════════════════
// A card that fails part-way through
//
// The one hardware failure users actually hit: a counterfeit card that
// accepts writes up to its real capacity and errors beyond it, or a card
// that has started to fail. The write has to stop and say so.
//
// Reporting success here is the worst outcome the writer has. The user
// unplugs a card they have been told is ready, and finds out it is not when
// the Pi does not boot -- with nothing to connect the two.
//
// Backed by a device-mapper table that returns EIO past a set point. No real
// media is involved. Creating the mapping needs root, so these skip without
// it, matching the rest of the suite.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A card that fails part-way through is reported", "[imagewriter][faulty]")
{
    using namespace rpi_imager::testing;

    if (!canRunPrivileged())
        SKIP("needs root to create the device-mapper table that injects EIO");

    // 64 MB of device, of which only the first 8 MB accept writes.
    FaultyDevice card(64, 8);
    if (!card.isReady())
        SKIP("could not create the faulty device mapping");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("fat32.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, BootPartitionFixture::kImageSize);
    w.setDst(card.path(), 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);

    // The image is 48 MB and the card takes 8. It must fail, and it must say
    // so rather than finishing quietly.
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK(out.failed);
    CHECK_FALSE(out.succeeded);
    CHECK_FALSE(out.errors.isEmpty());
}

TEST_CASE("A card that fails is not reported as verified", "[imagewriter][faulty]")
{
    using namespace rpi_imager::testing;

    if (!canRunPrivileged())
        SKIP("needs root to create the device-mapper table that injects EIO");

    FaultyDevice card(64, 8);
    if (!card.isReady())
        SKIP("could not create the faulty device mapping");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("fat32.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz"), source));

    ImageWriter w(nullptr);
    // With verification on there are two chances to notice, and neither may
    // be skipped: a counterfeit card is exactly what verification is for.
    w.setVerifyEnabled(true);
    w.setSrc(QUrl::fromLocalFile(source), 0, BootPartitionFixture::kImageSize);
    w.setDst(card.path(), 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
}

TEST_CASE("A card large enough for the image succeeds on the same harness", "[imagewriter][faulty]")
{
    using namespace rpi_imager::testing;

    if (!canRunPrivileged())
        SKIP("needs root to create the device-mapper table that injects EIO");

    // Same mapping, but every megabyte is good. Without this the case above
    // would pass for any reason at all -- a broken harness included.
    FaultyDevice card(64, 64);
    if (!card.isReady())
        SKIP("could not create the faulty device mapping");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("pattern-1MiB.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, kFixturePayload);
    w.setDst(card.path(), 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.failed);
    CHECK(out.succeeded);
}

// ═══════════════════════════════════════════════════════════════════════════
// The three customisation formats
//
// Which files land on the card depends on what the OS declares in the
// manifest. systemd images get firstrun.sh plus a cmdline entry to run it;
// rpi-preseed images get rpi-preseed.toml; cloud-init images get user-data
// and meta-data. Every case so far has used systemd.
//
// Writing the wrong shape is silent: the card boots, the file is ignored
// because nothing on that image looks for it, and none of the user's
// settings are applied.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A systemd image gets firstrun.sh and the cmdline entry", "[imagewriter][formats]")
{
    BootPartitionFixture fx;
    const QByteArray script = "#!/bin/bash\n# marker-systemd\nexit 0\n";

    const WriteOutcome out = writeWithCustomisation(fx, QByteArray(), QByteArray(), script,
                                                    QByteArray("systemd"));
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    CHECK(written.contains(QByteArray("FIRSTRUNSH")));
    CHECK(written.contains(QByteArray("marker-systemd")));
    // Without the cmdline entry the script is on the card and never runs.
    CHECK(written.contains(QByteArray("systemd.run=/boot/firstrun.sh")));
}

TEST_CASE("An rpi-preseed image gets a toml, not a script", "[imagewriter][formats]")
{
    BootPartitionFixture fx;
    const QByteArray toml = "[system]\nhostname = \"marker-preseed\"\n";

    const WriteOutcome out = writeWithCustomisation(fx, QByteArray(), QByteArray(), toml,
                                                    QByteArray("rpi-preseed"));
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    CHECK(written.contains(QByteArray("marker-preseed")));
    CHECK(written.contains(longNameHead(QStringLiteral("rpi-preseed.toml"))));
    // And none of the systemd shape: no firstrun.sh, and nothing added to
    // cmdline.txt to run it. A preseed image ignores both.
    CHECK_FALSE(written.contains(QByteArray("FIRSTRUNSH")));
    CHECK_FALSE(written.contains(QByteArray("systemd.run=")));
}

TEST_CASE("A cloud-init image gets user-data and meta-data", "[imagewriter][formats]")
{
    BootPartitionFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize);
    w.setDst(fx.target(), BootPartitionFixture::kImageSize);
    w.setImageCustomisation(QByteArray(), QByteArray(), QByteArray(),
                            QByteArray("#cloud-config\nhostname: marker-cloudinit\n"),
                            QByteArray(),
                            ImageOptions::NoAdvancedOptions, QByteArray("cloudinit"));

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    CHECK(written.contains(QByteArray("marker-cloudinit")));
    // cloud-init needs both; user-data alone is ignored on first boot.
    CHECK(written.contains(longNameHead(QStringLiteral("user-data"))));
    CHECK(written.contains(longNameHead(QStringLiteral("meta-data"))));
    // Not the systemd shape.
    CHECK_FALSE(written.contains(QByteArray("systemd.run=")));
}

TEST_CASE("A cloud-init network config lands too", "[imagewriter][formats]")
{
    BootPartitionFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize);
    w.setDst(fx.target(), BootPartitionFixture::kImageSize);
    w.setImageCustomisation(QByteArray(), QByteArray(), QByteArray(),
                            QByteArray("#cloud-config\nhostname: pi\n"),
                            QByteArray("version: 2\nethernets:\n  eth0:\n    dhcp4: marker-net\n"),
                            ImageOptions::NoAdvancedOptions, QByteArray("cloudinit"));

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    // Wi-Fi and static addressing are configured here; dropping it is how a
    // headless Pi comes up with no network.
    CHECK(fx.targetBytes().contains(QByteArray("marker-net")));
}

TEST_CASE("An image that declares no format is not customised", "[imagewriter][formats]")
{
    BootPartitionFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, BootPartitionFixture::kImageSize);
    w.setDst(fx.target(), BootPartitionFixture::kImageSize);

    // applyCustomisationFromSettings() clears everything when the selected
    // image does not support customisation, so nothing is staged.
    w.applyCustomisationFromSettings(QVariantMap{
        {QStringLiteral("hostname"), QStringLiteral("marker-nothing")}});

    const WriteOutcome out = runWrite(w);
    REQUIRE(out.succeeded);

    // Writing settings an image cannot act on would leave stray files in its
    // boot partition.
    CHECK_FALSE(fx.targetBytes().contains(QByteArray("marker-nothing")));
}

TEST_CASE("Config entries are appended, not duplicated", "[imagewriter][formats]")
{
    BootPartitionFixture fx;

    const WriteOutcome out = writeWithCustomisation(
        fx, QByteArray("dtparam=marker_cfg=on\n"), QByteArray(), QByteArray("#!/bin/bash\nexit 0\n"));
    REQUIRE(out.succeeded);

    const QByteArray written = fx.targetBytes();
    // The image's own config.txt is edited rather than replaced, so an entry
    // must appear exactly once even though the file already had content.
    CHECK(written.count(QByteArray("dtparam=marker_cfg=on")) == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Destinations that cannot be written
//
// The capacity check in startWrite() catches a card that is too small. These
// are the failures that only surface once the writer tries to open the
// destination: a path that is not a file, one the user has no permission
// for, one whose directory has gone.
//
// Each has to end in an error the user can act on. A write that fails to
// open and then reports success would leave them believing a card was
// written that never was.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

WriteOutcome writeTo(const QString &destination, quint64 declaredSize)
{
    QTemporaryDir dir;
    const QString source = QDir(dir.path()).filePath(QStringLiteral("os.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, kFixturePayload);
    w.setDst(destination, declaredSize);
    return runWrite(w, 60000);
}

} // namespace

TEST_CASE("A destination that is a directory is refused", "[imagewriter][dest]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const WriteOutcome out = writeTo(dir.path(), kFixturePayload);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
}

TEST_CASE("A destination in a directory that does not exist is refused", "[imagewriter][dest]")
{
    const WriteOutcome out =
        writeTo(QStringLiteral("/nonexistent-directory-for-tests/target.img"), kFixturePayload);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
}

TEST_CASE("A destination the user cannot write is refused", "[imagewriter][dest]")
{
    if (::geteuid() == 0)
        SKIP("running as root, which can write a file with no permissions");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("readonly.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();
    REQUIRE(QFile::setPermissions(target, QFileDevice::ReadOwner));

    const WriteOutcome out = writeTo(target, kFixturePayload);

    // This is what a card with its lock tab set looks like from here.
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
    CHECK_FALSE(out.errors.isEmpty());

    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

TEST_CASE("An empty destination is refused before anything starts", "[imagewriter][dest]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("os.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setSrc(QUrl::fromLocalFile(source), 0, kFixturePayload);
    w.setDst(QString(), 0);

    // No destination chosen yet: the write button should not be live at all.
    CHECK_FALSE(w.readyToWrite());
}

TEST_CASE("An empty source is refused before anything starts", "[imagewriter][dest]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setDst(target, kFixturePayload);

    CHECK_FALSE(w.readyToWrite());
}

// ═══════════════════════════════════════════════════════════════════════════
// Erasing a card, and ejecting it afterwards
//
// Two things the user does that are not "write an image". Erase is the first
// entry in the OS chooser; eject is what the imager offers when a write
// finishes, and on Linux it is what stops the desktop remounting a card the
// user is about to pull out.
//
// Neither had been driven through ImageWriter. The eject state machine
// matters most: QML binds a button to it, so a state that never leaves
// "in progress" is a button that stays disabled for the rest of the session.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

ImageWriter::EjectState ejectStateOf(ImageWriter &w)
{
    // ejectState() is private but published as a Q_PROPERTY for QML.
    return w.property("ejectState").value<ImageWriter::EjectState>();
}

bool waitForEjectToSettle(ImageWriter &w, int timeoutMs = 30000)
{
    QElapsedTimer t;
    t.start();
    while (ejectStateOf(w) == ImageWriter::EjectState::EjectInProgress
           && t.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ejectStateOf(w) != ImageWriter::EjectState::EjectInProgress;
}

} // namespace

TEST_CASE("Ejecting with no card chosen does nothing", "[imagewriter][eject]")
{
    ImageWriter w(nullptr);
    w.ejectDrive();

    // No destination: there is nothing to eject and nothing to report.
    CHECK(ejectStateOf(w) == ImageWriter::EjectState::EjectIdle);
}

TEST_CASE("Ejecting a fastboot target does nothing", "[imagewriter][eject]")
{
    ImageWriter w(nullptr);
    w.setDst(QStringLiteral("fastboot://1:6"), 0);
    w.ejectDrive();

    // A device in fastboot is not a removable disk; there is no eject for it.
    CHECK(ejectStateOf(w) == ImageWriter::EjectState::EjectIdle);
}

TEST_CASE("Ejecting always reaches a terminal state", "[imagewriter][eject]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(1024 * 1024));
    t.close();

    ImageWriter w(nullptr);
    w.setDst(target, 1024 * 1024);
    w.ejectDrive();

    // A regular file is not ejectable, so this may well fail -- what matters
    // is that it stops. QML binds a button to this state, and one stuck at
    // "in progress" is disabled for the rest of the session.
    REQUIRE(waitForEjectToSettle(w));
    const auto state = ejectStateOf(w);
    CHECK((state == ImageWriter::EjectState::EjectSucceeded
           || state == ImageWriter::EjectState::EjectFailed));
}

TEST_CASE("A second eject while one is running is ignored", "[imagewriter][eject]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(1024 * 1024));
    t.close();

    ImageWriter w(nullptr);
    w.setDst(target, 1024 * 1024);

    w.ejectDrive();
    // Double-clicking the button must not start a second worker over the
    // first one's state.
    CHECK_NOTHROW(w.ejectDrive());
    REQUIRE(waitForEjectToSettle(w));
}

TEST_CASE("Ejecting a real block device settles", "[imagewriter][eject][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    ImageWriter w(nullptr);
    w.setDst(dev, 64ull * 1024 * 1024);
    w.ejectDrive();

    REQUIRE(waitForEjectToSettle(w));
    CHECK(ejectStateOf(w) != ImageWriter::EjectState::EjectInProgress);
}

TEST_CASE("Erase formats a card through the writer", "[imagewriter][erase][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // The first entry in the OS chooser.
    w.setSrc(QUrl(QStringLiteral("internal://format")));
    w.setDst(dev, 64ull * 1024 * 1024);
    REQUIRE(w.readyToWrite());

    const WriteOutcome out = runWrite(w, 180000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    QFile card(dev);
    REQUIRE(card.open(QIODevice::ReadOnly));
    const QByteArray mbr = card.read(512);
    REQUIRE(mbr.size() == 512);

    // A FAT32 partition table, or the card is not usable by anything.
    CHECK(quint8(mbr.at(510)) == 0x55);
    CHECK(quint8(mbr.at(511)) == 0xAA);
    const quint8 type = quint8(mbr.at(0x1BE + 4));
    INFO("partition type: 0x" << QString::number(type, 16).toStdString());
    CHECK((type == 0x0B || type == 0x0C));
}

// ═══════════════════════════════════════════════════════════════════════════
// The SSH key offered in the customisation screen
//
// "Allow public-key authentication only" needs a key. The imager reads the
// user's existing one and, if there is none, offers to generate a pair --
// and whatever it reads is written into the card's authorized_keys.
//
// Reading the wrong thing, or nothing, means a headless Pi the user cannot
// log into: the failure is discovered after the card is written, at the
// point where it is least convenient.
//
// These redirect HOME to a scratch directory, and skip outright if the
// redirect does not take -- writing an SSH key into the real account is not
// something a test gets to do by accident.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Points HOME at a scratch directory for as long as it is alive.
class ScopedHome
{
public:
    explicit ScopedHome(const QString &path) : _saved(qgetenv("HOME"))
    {
        qputenv("HOME", path.toLocal8Bit());
    }
    ~ScopedHome() { qputenv("HOME", _saved); }

private:
    QByteArray _saved;
};

} // namespace

TEST_CASE("With no key in place none is reported", "[imagewriter][sshkey]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    ScopedHome scoped(home.path());
    if (QDir::homePath() != home.path())
        SKIP("HOME redirect did not take; refusing to touch the real ~/.ssh");

    ImageWriter w(nullptr);
    CHECK_FALSE(w.hasPubKey());
    CHECK(w.getDefaultPubKey().isEmpty());
}

TEST_CASE("An existing public key is read back verbatim", "[imagewriter][sshkey]")
{
    QTemporaryDir home;
    REQUIRE(home.isValid());
    ScopedHome scoped(home.path());
    if (QDir::homePath() != home.path())
        SKIP("HOME redirect did not take; refusing to touch the real ~/.ssh");

    REQUIRE(QDir().mkpath(home.path() + QStringLiteral("/.ssh")));
    const QString key =
        QStringLiteral("ssh-rsa AAAAB3NzaC1yc2EAAAADAQABmarkerkey user@example");
    QFile f(home.path() + QStringLiteral("/.ssh/id_rsa.pub"));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(key.toUtf8() + "\n");
    f.close();

    ImageWriter w(nullptr);
    REQUIRE(w.hasPubKey());
    // Byte-for-byte: a key mangled on the way through does not authenticate,
    // and the user finds out only after the card is written.
    CHECK(w.getDefaultPubKey().trimmed() == key);
}

TEST_CASE("Generating a key creates a usable pair", "[imagewriter][sshkey]")
{
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/ssh-keygen")))
        SKIP("ssh-keygen is not installed");

    QTemporaryDir home;
    REQUIRE(home.isValid());
    ScopedHome scoped(home.path());
    if (QDir::homePath() != home.path())
        SKIP("HOME redirect did not take; refusing to touch the real ~/.ssh");

    ImageWriter w(nullptr);
    REQUIRE_FALSE(w.hasPubKey());

    w.generatePubKey();

    // Both halves, and the directory created if it was missing.
    CHECK(QFile::exists(home.path() + QStringLiteral("/.ssh/id_rsa")));
    REQUIRE(w.hasPubKey());
    CHECK(w.getDefaultPubKey().startsWith(QStringLiteral("ssh-rsa ")));
}

TEST_CASE("Generating a key does not replace one already there", "[imagewriter][sshkey]")
{
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/ssh-keygen")))
        SKIP("ssh-keygen is not installed");

    QTemporaryDir home;
    REQUIRE(home.isValid());
    ScopedHome scoped(home.path());
    if (QDir::homePath() != home.path())
        SKIP("HOME redirect did not take; refusing to touch the real ~/.ssh");

    REQUIRE(QDir().mkpath(home.path() + QStringLiteral("/.ssh")));
    const QString key = QStringLiteral("ssh-rsa AAAAB3NzaC1yc2EAAAADAQABkeepme user@example");
    QFile f(home.path() + QStringLiteral("/.ssh/id_rsa.pub"));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(key.toUtf8() + "\n");
    f.close();

    ImageWriter w(nullptr);
    w.generatePubKey();

    // Overwriting somebody's SSH key would be unforgivable.
    CHECK(w.getDefaultPubKey().trimmed() == key);
}

// ═══════════════════════════════════════════════════════════════════════════
// Sweeping the customisation dropdowns
//
// The existing cases check these lists are not empty. That catches a
// resource that failed to compile in, but not a list with a malformed row in
// the middle of it -- which is what the user meets as a dropdown entry that
// sets nothing, or one that writes an invalid value into the card's locale
// configuration and leaves the Pi with a keyboard that types the wrong keys.
//
// Every entry, checked in full, is cheap here and impossible to notice by
// eye across several hundred rows.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Every timezone offered is one the system knows", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList zones = w.getTimezoneList();
    REQUIRE(zones.size() > 100);

    int unknown = 0;
    QStringList examples;
    for (const QString &zone : zones) {
        if (zone.trimmed().isEmpty()) {
            ++unknown;
            continue;
        }
        // A zone the tz database does not have is one systemd-timesyncd will
        // reject on first boot, leaving the Pi on UTC.
        if (!QTimeZone(zone.toUtf8()).isValid()) {
            ++unknown;
            if (examples.size() < 5)
                examples << zone;
        }
    }
    INFO("unrecognised: " << examples.join(QStringLiteral(", ")).toStdString());
    CHECK(unknown == 0);
}

TEST_CASE("Every country offered is well formed", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList countries = w.getCountryList();
    REQUIRE(countries.size() > 50);

    QStringList bad;
    for (const QString &c : countries) {
        // These become the Wi-Fi regulatory domain, which the kernel takes as
        // a two-letter ISO code.
        if (c.trimmed().isEmpty() || c != c.trimmed()) {
            if (bad.size() < 5) bad << QStringLiteral("[%1]").arg(c);
        }
    }
    INFO("malformed: " << bad.join(QStringLiteral(", ")).toStdString());
    CHECK(bad.isEmpty());

    CHECK(countries.contains(QStringLiteral("GB")));
}

TEST_CASE("Every keyboard layout offered is well formed", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList layouts = w.getKeymapLayoutList();
    REQUIRE(layouts.size() > 20);

    QStringList bad;
    for (const QString &l : layouts) {
        if (l.trimmed().isEmpty() || l != l.trimmed() || l.contains(QLatin1Char(' '))) {
            if (bad.size() < 5) bad << QStringLiteral("[%1]").arg(l);
        }
    }
    // A layout name with stray whitespace is written into the card's keyboard
    // configuration verbatim and silently ignored on boot.
    INFO("malformed: " << bad.join(QStringLiteral(", ")).toStdString());
    CHECK(bad.isEmpty());

    CHECK(layouts.contains(QStringLiteral("gb")));
}

TEST_CASE("The lists have no duplicate entries", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);

    // A duplicate is a dropdown showing the same choice twice, which looks
    // like a bug to the user even though either one works.
    const QStringList countries = w.getCountryList();
    CHECK(QSet<QString>(countries.cbegin(), countries.cend()).size() == countries.size());

    const QStringList layouts = w.getKeymapLayoutList();
    CHECK(QSet<QString>(layouts.cbegin(), layouts.cend()).size() == layouts.size());

    const QStringList zones = w.getTimezoneList();
    CHECK(QSet<QString>(zones.cbegin(), zones.cend()).size() == zones.size());
}

TEST_CASE("Every language offered actually applies", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QStringList langs = w.getTranslations();
    REQUIRE(langs.size() > 1);

    // changeLanguage() loads :/i18n/rpi-imager_<code>.qm and, if the load
    // fails, silently does nothing. So a language in the menu whose
    // translation did not make it into the resource is one the user can
    // select and watch nothing happen -- no error, no change.
    //
    // The existing case changes to one language. This one changes to every
    // one, which is the only way to find the single entry that does not.
    QStringList inert;
    for (const QString &lang : langs) {
        w.changeLanguage(lang);
        if (w.getCurrentLanguage() != lang)
            inert << lang;
    }

    INFO("did not apply: " << inert.join(QStringLiteral(", ")).toStdString());
    CHECK(inert.isEmpty());
}

TEST_CASE("An unknown language is ignored rather than applied", "[imagewriter][locale]")
{
    ImageWriter w(nullptr);
    const QString before = w.getCurrentLanguage();

    w.changeLanguage(QStringLiteral("Klingon"));
    w.changeLanguage(QString());

    // Neither should move the selection; a name not in the list has no
    // translation behind it.
    CHECK(w.getCurrentLanguage() == before);
}

TEST_CASE("A card that fails mid-write reports it", "[imagewriter][faulty]")
{
    using namespace rpi_imager::testing;

    if (!canRunPrivileged())
        SKIP("needs root to create the device-mapper table that injects EIO");

    // Writable at both ends, failing from 16 MB to 32 MB. Both ends matter:
    // the end-of-device check during preparation writes to the last
    // megabyte, so a device whose tail is bad never gets as far as writing
    // image data. This one does, and then fails part-way through -- which is
    // how a counterfeit card actually behaves, and the only way to reach the
    // writer's mid-stream error handling.
    FaultyDevice card(64, FaultyDevice::BadBand{16, 16});
    if (!card.isReady())
        SKIP("could not create the faulty device mapping");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("fat32.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, BootPartitionFixture::kImageSize);
    w.setDst(card.path(), 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
    REQUIRE_FALSE(out.errors.isEmpty());
    // The message has to say something a user can act on, not just fail.
    CHECK(out.errors.first().size() > 20);
}

TEST_CASE("A card failing mid-write in sync mode reports it too", "[imagewriter][faulty]")
{
    using namespace rpi_imager::testing;

    if (!canRunPrivileged())
        SKIP("needs root to create the device-mapper table that injects EIO");

    FaultyDevice card(64, FaultyDevice::BadBand{16, 16});
    if (!card.isReady())
        SKIP("could not create the faulty device mapping");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("fat32.img.xz"));
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/fat32-48MiB.img.xz"), source));

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // With async I/O the failure returns through a completion callback; in
    // sync mode the write call itself returns it. Different code, same
    // requirement.
    w.setDebugAsyncIO(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, BootPartitionFixture::kImageSize);
    w.setDst(card.path(), 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);

    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
    CHECK(out.failed);
}

TEST_CASE("Ignoring device limits reallocates the buffers", "[imagewriter][device]")
{
    rpi_imager::testing::TestBlockDevice device(64);
    if (!device.isReady())
        SKIP("no loop device to write to: allow passwordless sudo so one can be provisioned, or set RPI_IMAGER_TEST_BLOCK_DEVICE to one");
    const QString dev = device.path();

    QTemporaryDir served;
    REQUIRE(served.isValid());
    REQUIRE(QFile::copy(QStringLiteral(IMAGER_TEST_DATA_DIR "/pattern-1MiB.img.xz"),
                        QDir(served.path()).filePath(QStringLiteral("os.img.xz"))));

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    // A block device advertises a maximum transfer size, and the writer sizes
    // its ring buffers to fit. Writing to a file never caps anything, so the
    // reallocation this option performs -- after the device is open, which is
    // an allocation path nothing else takes -- only happens against a real
    // device whose limit is below the RAM-based optimum. A loop device's is.
    //
    // It also only happens on the download path: the hook is called from
    // DownloadThread::run(), and LocalFileExtractThread overrides run(). So
    // the image is served over loopback rather than read off disk.
    w.setDebugIgnoreDeviceLimits(true);
    w.setSrc(QUrl(QString::fromUtf8(server.urlFor(QStringLiteral("os.img.xz")))),
             0, kFixturePayload, sha256HexOf(fixturePayload()));
    w.setDst(dev, 64ull * 1024 * 1024);

    const WriteOutcome out = runWrite(w, 180000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE_FALSE(out.failed);
    REQUIRE(out.succeeded);

    // Reallocating mid-flight must not lose or reorder anything.
    QFile card(dev);
    REQUIRE(card.open(QIODevice::ReadOnly));
    CHECK(card.read(qint64(kFixturePayload)) == fixturePayload());
}

// ═══════════════════════════════════════════════════════════════════════════
// Which extraction path a plain image takes
//
// LocalFileExtractThread chooses between libarchive and a direct copy by
// asking libarchive whether it can extract anything. libarchive is
// configured with format_raw, which matches any file at all and hands back
// its contents -- so a plain uncompressed .img is taken for an archive and
// goes through the libarchive path, not the direct copy written for it.
//
// That is worth pinning rather than assuming, because it decides which of
// two implementations every "Use custom" write of a raw image exercises.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A zero-length image is refused rather than written", "[imagewriter][extract]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString source = QDir(dir.path()).filePath(QStringLiteral("empty.img"));
    QFile s(source);
    REQUIRE(s.open(QIODevice::WriteOnly));
    s.close();

    const QString target = QDir(dir.path()).filePath(QStringLiteral("target.img"));
    QFile t(target);
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.resize(qint64(kFixturePayload)));
    t.close();

    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(QUrl::fromLocalFile(source), 0, 0);
    w.setDst(target, kFixturePayload);

    // An empty file is the one case libarchive cannot extract anything from,
    // so this is also the only way into the direct-copy path. Writing
    // nothing and calling it a success would leave the user with a card they
    // believe is imaged.
    const WriteOutcome out = runWrite(w, 60000);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    CHECK_FALSE(out.succeeded);
}

// ══════════════════════════════════════════════════════════════
// The handoff from rpiboot to the fastboot write
//
// When a compute module comes back in fastboot mode, two decisions get made
// on the user's behalf before anything is written: where the image is read
// from, and which storage on the board it goes to.
// ══════════════════════════════════════════════════════════════

TEST_CASE("With no cache the fastboot write reads from where it was told",
          "[imagewriter][rpiboot-handoff]")
{
    ImageWriter w(nullptr);
    const QUrl src(QStringLiteral("https://example.invalid/os.img.xz"));
    w.setSrc(src, 0, 0, QByteArray("deadbeef"));

    CHECK(w.resolveFlashSource() == src);
}

TEST_CASE("Without an expected hash there is no cache to consult",
          "[imagewriter][rpiboot-handoff]")
{
    // Nothing to match a cached file against, so the network URL stands.
    ImageWriter w(nullptr);
    const QUrl src(QStringLiteral("https://example.invalid/os.img.xz"));
    w.setSrc(src);

    CHECK(w.resolveFlashSource() == src);
}

TEST_CASE("A local source is handed through unchanged",
          "[imagewriter][rpiboot-handoff]")
{
    ImageWriter w(nullptr);
    const QUrl src = QUrl::fromLocalFile(QStringLiteral("/tmp/custom.img"));
    w.setSrc(src);

    CHECK(w.resolveFlashSource() == src);
}

TEST_CASE("The storage the user picked is the storage written",
          "[imagewriter][rpiboot-handoff]")
{
    ImageWriter w(nullptr);
    w.setRpibootDevice(QStringLiteral("rpiboot://1:4:1.2:5"),
                       QStringLiteral("nvme0n1"));

    CHECK(w.resolveFastbootStorageTarget() == QStringLiteral("nvme0n1"));
}

TEST_CASE("A handoff that carried no storage target falls back to eMMC",
          "[imagewriter][rpiboot-handoff]")
{
    // eMMC is the only storage every compute module is guaranteed to have,
    // so it is the one safe guess. The BOOT_ORDER written afterwards will
    // say SD/eMMC rather than what the user picked, which is why the code
    // warns about it -- but writing nowhere would be worse.
    ImageWriter w(nullptr);
    w.setRpibootDevice(QStringLiteral("rpiboot://1:4:1.2:5"), QString());

    CHECK(w.resolveFastbootStorageTarget() == QStringLiteral("mmcblk0"));
}

TEST_CASE("A storage target of only whitespace is taken at its word",
          "[imagewriter][rpiboot-handoff]")
{
    // Recorded rather than desired: the check is isEmpty(), so a target that
    // is blank but not empty passes through to the flash thread as-is. No
    // caller produces one today.
    ImageWriter w(nullptr);
    w.setRpibootDevice(QStringLiteral("rpiboot://1:4:1.2:5"),
                       QStringLiteral(" "));

    CHECK(w.resolveFastbootStorageTarget() == QStringLiteral(" "));
}

// ══════════════════════════════════════════════════════════════
// Images found on inserted media
//
// On a kiosk or an embedded build there may be no network, so a USB stick
// with an image on it is how the OS gets chosen. The scan walks each mounted
// volume and offers what it finds as OS entries.
// ══════════════════════════════════════════════════════════════

namespace {

class MediaImageWriter : public ImageWriter
{
public:
    MediaImageWriter() : ImageWriter(nullptr) {}

    QString mediaRoot;
    QString blockRoot;

    // What would have been mounted, and what to pretend happened. Nothing
    // is mounted for real: the point is the choice of device, not the
    // syscall.
    QStringList mountAttempts;
    int mountResult = 0;

protected:
    QString usbMediaRoot() const override { return mediaRoot; }
    QString sysBlockRoot() const override { return blockRoot; }
    int mountReadOnly(const QString &devicePath, const QString &) override
    {
        mountAttempts << devicePath;
        return mountResult;
    }
};

// Fake /sys/class/block: a directory per device. `virtualDevices` get a
// symlink under devices/virtual, the way loop and ram devices really do.
void layOutBlockDevices(const QString &root, const QStringList &devices,
                        const QStringList &virtualDevices = {})
{
    REQUIRE(QDir().mkpath(root));
    const QString virtRoot = root + "/../devices/virtual/block";
    REQUIRE(QDir().mkpath(virtRoot));

    for (const QString &d : devices) {
        if (virtualDevices.contains(d)) {
            REQUIRE(QDir().mkpath(virtRoot + "/" + d));
            QFile::link(QFileInfo(virtRoot + "/" + d).absoluteFilePath(),
                        root + "/" + d);
        } else {
            REQUIRE(QDir().mkpath(root + "/" + d));
        }
    }
}

// Lay out volumes under a root: { "STICK": { "os.img", "notes.txt" } }
void layOutMedia(const QString &root,
                 const QList<QPair<QString, QStringList>> &volumes)
{
    for (const auto &[volume, files] : volumes) {
        const QString dir = root + "/" + volume;
        REQUIRE(QDir().mkpath(dir));
        for (const QString &name : files) {
            QFile f(dir + "/" + name);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1024, 'x'));
            f.close();
        }
    }
}

QJsonArray mediaEntries(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).array();
}

} // namespace

// Both groups below name functions that are #ifdef Q_OS_LINUX from end to
// end: getUsbSourceOSlist() reads /media, and mountUsbSourceMedia() reads
// /sys/class/block and mounts what it finds. Everywhere else they do nothing
// and report nothing, so these cases were not exercising the behaviour they
// describe -- the ones expecting a mount or a listing failed, and the ones
// expecting neither passed for a reason unrelated to the code they name.
// Both are worth saying out loud rather than reading as a result.
#ifdef Q_OS_LINUX
#define SKIP_WITHOUT_LINUX_MEDIA() do { } while (false)
#else
#define SKIP_WITHOUT_LINUX_MEDIA() \
    SKIP("reading images from mounted media is implemented on Linux only")
#endif

TEST_CASE("An image on inserted media is offered", "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir media;
    REQUIRE(media.isValid());
    layOutMedia(media.path(), {{QStringLiteral("STICK"),
                                {QStringLiteral("raspios.img")}}});

    MediaImageWriter w;
    w.mediaRoot = media.path();

    const auto entries = mediaEntries(w.getUsbSourceOSlist());
    REQUIRE(entries.size() == 1);

    const auto o = entries[0].toObject();
    CHECK(o.value(QStringLiteral("name")).toString() == QStringLiteral("raspios.img"));
    CHECK(o.value(QStringLiteral("description")).toString()
          == QStringLiteral("STICK/raspios.img"));
    CHECK(o.value(QStringLiteral("url")).toString().startsWith(QStringLiteral("file://")));
    CHECK(o.value(QStringLiteral("image_download_size")).toInt() == 1024);
}

TEST_CASE("Only image-shaped files are offered", "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // A stick with holiday photos on it should not fill the OS list with
    // them, and selecting a text file to write to a card helps nobody.
    QTemporaryDir media;
    REQUIRE(media.isValid());
    layOutMedia(media.path(), {{QStringLiteral("STICK"), {
        QStringLiteral("os.img"), QStringLiteral("os.zip"),
        QStringLiteral("os.gz"),  QStringLiteral("os.xz"),
        QStringLiteral("os.zst"), QStringLiteral("os.wic"),
        QStringLiteral("notes.txt"), QStringLiteral("photo.jpg"),
        QStringLiteral("README"),
    }}});

    MediaImageWriter w;
    w.mediaRoot = media.path();

    const auto entries = mediaEntries(w.getUsbSourceOSlist());
    CHECK(entries.size() == 6);

    QStringList names;
    for (const auto &e : entries)
        names << e.toObject().value(QStringLiteral("name")).toString();
    INFO("offered: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.contains(QStringLiteral("notes.txt")));
    CHECK_FALSE(names.contains(QStringLiteral("README")));
}

TEST_CASE("Every mounted volume is looked at", "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir media;
    REQUIRE(media.isValid());
    layOutMedia(media.path(), {
        {QStringLiteral("STICK"), {QStringLiteral("a.img")}},
        {QStringLiteral("CARD"),  {QStringLiteral("b.img")}},
    });

    MediaImageWriter w;
    w.mediaRoot = media.path();

    const auto entries = mediaEntries(w.getUsbSourceOSlist());
    CHECK(entries.size() == 2);
}

TEST_CASE("The volume an image came from is named", "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // Two sticks can hold a file of the same name. The description is the
    // only thing telling the user which one they are picking.
    QTemporaryDir media;
    REQUIRE(media.isValid());
    layOutMedia(media.path(), {
        {QStringLiteral("STICK_A"), {QStringLiteral("os.img")}},
        {QStringLiteral("STICK_B"), {QStringLiteral("os.img")}},
    });

    MediaImageWriter w;
    w.mediaRoot = media.path();

    QStringList descriptions;
    for (const auto &e : mediaEntries(w.getUsbSourceOSlist()))
        descriptions << e.toObject().value(QStringLiteral("description")).toString();

    CHECK(descriptions.contains(QStringLiteral("STICK_A/os.img")));
    CHECK(descriptions.contains(QStringLiteral("STICK_B/os.img")));
}

TEST_CASE("Nothing mounted offers nothing", "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir media;
    REQUIRE(media.isValid());

    MediaImageWriter w;
    w.mediaRoot = media.path();

    CHECK(mediaEntries(w.getUsbSourceOSlist()).isEmpty());
}

TEST_CASE("A media root that is not there is not an error",
          "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // /media does not exist on every system, and a machine without it should
    // simply offer no local images rather than failing to build a chooser.
    MediaImageWriter w;
    w.mediaRoot = QStringLiteral("/nonexistent-media-root-for-tests");

    QByteArray json;
    REQUIRE_NOTHROW(json = w.getUsbSourceOSlist());
    CHECK(mediaEntries(json).isEmpty());
}

TEST_CASE("A volume with no images contributes nothing",
          "[imagewriter][usbsource]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir media;
    REQUIRE(media.isValid());
    layOutMedia(media.path(), {
        {QStringLiteral("EMPTY"), {}},
        {QStringLiteral("DOCS"),  {QStringLiteral("a.txt")}},
        {QStringLiteral("STICK"), {QStringLiteral("os.img")}},
    });

    MediaImageWriter w;
    w.mediaRoot = media.path();

    const auto entries = mediaEntries(w.getUsbSourceOSlist());
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].toObject().value(QStringLiteral("description")).toString()
          == QStringLiteral("STICK/os.img"));
}


// ══════════════════════════════════════════════════════════════
// Which block devices get mounted to look for images
//
// This runs as root on an embedded build and mounts what it finds. Two
// things must never be reached for: the card the machine is running from,
// and anything that is not real storage.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A removable disk is mounted to look for images",
          "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    layOutBlockDevices(blocks, {QStringLiteral("sda")});

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = tmp.filePath(QStringLiteral("media"));

    CHECK(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts == QStringList{QStringLiteral("/dev/sda")});
}

TEST_CASE("The card the machine is running from is never mounted",
          "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // mmcblk0 is the boot card on a Pi. Mounting it here and offering its
    // contents as images to write is reaching for the disk under your feet.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    layOutBlockDevices(blocks, {
        QStringLiteral("mmcblk0"),
        QStringLiteral("mmcblk0p1"),
        QStringLiteral("mmcblk0p2"),
    });

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = tmp.filePath(QStringLiteral("media"));

    CHECK_FALSE(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts.isEmpty());
}

TEST_CASE("Another card is still eligible", "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // The guard is on mmcblk0 specifically, not on cards in general -- a
    // second card reader is a perfectly good place to keep an image.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    layOutBlockDevices(blocks, {
        QStringLiteral("mmcblk0"),
        QStringLiteral("mmcblk1"),
    });

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = tmp.filePath(QStringLiteral("media"));

    CHECK(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts == QStringList{QStringLiteral("/dev/mmcblk1")});
}

TEST_CASE("Devices that are not real storage are skipped",
          "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // loop and ram devices live under devices/virtual. Mounting them finds
    // nothing and clutters /media with mount points.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    layOutBlockDevices(blocks,
                       {QStringLiteral("sda"), QStringLiteral("loop0"),
                        QStringLiteral("ram0")},
                       {QStringLiteral("loop0"), QStringLiteral("ram0")});

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = tmp.filePath(QStringLiteral("media"));

    CHECK(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts == QStringList{QStringLiteral("/dev/sda")});
}

TEST_CASE("A mount that fails leaves no empty mount point behind",
          "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    // An unformatted or unreadable disk. The directory made for it has to go
    // again, or /media fills with empty folders that later scans walk.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    const QString media = tmp.filePath(QStringLiteral("media"));
    layOutBlockDevices(blocks, {QStringLiteral("sda")});

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = media;
    w.mountResult = 1;                       // mount refused it

    CHECK_FALSE(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts.size() == 1);
    CHECK_FALSE(QDir(media + "/sda").exists());
}

TEST_CASE("An already-mounted disk is counted, not mounted again",
          "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    const QString media = tmp.filePath(QStringLiteral("media"));
    layOutBlockDevices(blocks, {QStringLiteral("sda")});
    REQUIRE(QDir().mkpath(media + "/sda"));

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = media;

    CHECK(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts.isEmpty());
}

TEST_CASE("No block devices at all mounts nothing", "[imagewriter][usbmount]")
{
    SKIP_WITHOUT_LINUX_MEDIA();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString blocks = tmp.filePath(QStringLiteral("sys/class/block"));
    REQUIRE(QDir().mkpath(blocks));

    MediaImageWriter w;
    w.blockRoot = blocks;
    w.mediaRoot = tmp.filePath(QStringLiteral("media"));

    CHECK_FALSE(w.mountUsbSourceMedia());
    CHECK(w.mountAttempts.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// The SSH key offered for public-key authentication
//
// The customisation step can put the user's own public key on the card so
// they can log in without a password. It reads that key from ~/.ssh, and
// will generate one if there is none -- which means the guard against
// writing over a key that already exists is protecting a file that is not
// replaceable. Somebody's private key is not ours to overwrite.
// ══════════════════════════════════════════════════════════════

namespace {

class KeyedImageWriter : public ImageWriter
{
public:
    KeyedImageWriter() : ImageWriter(nullptr) {}
    QString keyDir;
protected:
    QString _sshKeyDir() override { return keyDir; }
};

void writeFile(const QString &path, const QByteArray &contents)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(contents);
    f.close();
}

} // namespace

TEST_CASE("With no key there is none to offer", "[imagewriter][sshkey]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    KeyedImageWriter w;
    w.keyDir = dir.filePath(QStringLiteral(".ssh"));

    CHECK_FALSE(w.hasPubKey());
    CHECK(w.getDefaultPubKey().isEmpty());
}

TEST_CASE("An existing public key is offered", "[imagewriter][sshkey]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ssh = dir.filePath(QStringLiteral(".ssh"));
    writeFile(ssh + "/id_rsa.pub", "ssh-rsa AAAAB3NzaC1yc2E user@host\n");

    KeyedImageWriter w;
    w.keyDir = ssh;

    CHECK(w.hasPubKey());
    CHECK(w.getDefaultPubKey()
          == QStringLiteral("ssh-rsa AAAAB3NzaC1yc2E user@host"));
}

TEST_CASE("The offered key has no trailing newline", "[imagewriter][sshkey]")
{
    // It goes into authorized_keys on the card. A stray newline there makes
    // a second, empty entry -- harmless, but the file is one the user may
    // later read and wonder about.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ssh = dir.filePath(QStringLiteral(".ssh"));
    writeFile(ssh + "/id_rsa.pub", "ssh-rsa AAAA user@host\n\n");

    KeyedImageWriter w;
    w.keyDir = ssh;

    const QString key = w.getDefaultPubKey();
    CHECK_FALSE(key.endsWith(QChar('\n')));
    CHECK(key == QStringLiteral("ssh-rsa AAAA user@host"));
}

TEST_CASE("An existing key is never generated over", "[imagewriter][sshkey]")
{
    // The one that matters. A private key is not replaceable: overwriting it
    // locks the user out of every machine that trusts the old one.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ssh = dir.filePath(QStringLiteral(".ssh"));
    writeFile(ssh + "/id_rsa.pub", "ssh-rsa ORIGINAL user@host\n");
    writeFile(ssh + "/id_rsa", "PRIVATE KEY MATERIAL\n");

    KeyedImageWriter w;
    w.keyDir = ssh;

    w.generatePubKey();

    QFile priv(ssh + "/id_rsa");
    REQUIRE(priv.open(QIODevice::ReadOnly));
    CHECK(priv.readAll() == QByteArray("PRIVATE KEY MATERIAL\n"));
    CHECK(w.getDefaultPubKey() == QStringLiteral("ssh-rsa ORIGINAL user@host"));
}

TEST_CASE("A private key with no public key is left alone too",
          "[imagewriter][sshkey]")
{
    // hasPubKey() is false here, so only the second half of the guard stops
    // this -- and it has to, because the private key is the irreplaceable
    // half. The public one can always be derived again.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ssh = dir.filePath(QStringLiteral(".ssh"));
    writeFile(ssh + "/id_rsa", "PRIVATE KEY MATERIAL\n");

    KeyedImageWriter w;
    w.keyDir = ssh;

    CHECK_FALSE(w.hasPubKey());
    w.generatePubKey();

    QFile priv(ssh + "/id_rsa");
    REQUIRE(priv.open(QIODevice::ReadOnly));
    CHECK(priv.readAll() == QByteArray("PRIVATE KEY MATERIAL\n"));
}

TEST_CASE("A key directory that is not there yields no key",
          "[imagewriter][sshkey]")
{
    KeyedImageWriter w;
    w.keyDir = QStringLiteral("/nonexistent-ssh-dir-for-tests");

    CHECK_FALSE(w.hasPubKey());
    CHECK(w.getDefaultPubKey().isEmpty());
}

// ══════════════════════════════════════════════════════════════
// What the command line accepts, and how it refuses
//
// The CLI is how imaging gets scripted and how it runs on a machine with no
// display. When it refuses, the message on stderr is the whole of what the
// operator gets -- there is no dialog to read and no list to look at.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A cache file with no hash to check it against is refused",
          "[imagewriter][cli][cache]")
{
    // --cache-file's own help text has always said it requires --sha256.
    // Nothing enforced it, and the failure was silent: both hashes are empty,
    // the cache lookup compares them and matches, and the file named by
    // --cache-file is written in place of the image the script asked for.
    // An automation run would report success having written the wrong image.
    const QString problem =
        Cli::validateCacheOptions(QStringLiteral("/tmp/cached.img"), QString());
    INFO("reported: " << problem.toStdString());
    REQUIRE_FALSE(problem.isEmpty());
    // Names both options, since the fix is to add one of them.
    CHECK_THAT(problem.toStdString(), ContainsSubstring("--cache-file"));
    CHECK_THAT(problem.toStdString(), ContainsSubstring("--sha256"));
}

TEST_CASE("A cache file with a hash is accepted", "[imagewriter][cli][cache]")
{
    CHECK(Cli::validateCacheOptions(QStringLiteral("/tmp/cached.img"),
                                    QStringLiteral("abc123")).isEmpty());
}

TEST_CASE("No cache file means there is nothing to require", "[imagewriter][cli][cache]")
{
    // --sha256 on its own is ordinary: it is how a script pins the image it
    // expects, with or without a cache.
    CHECK(Cli::validateCacheOptions(QString(), QString()).isEmpty());
    CHECK(Cli::validateCacheOptions(QString(), QStringLiteral("abc123")).isEmpty());
}

TEST_CASE("An http source is fetched rather than looked for on disk",
          "[cli][source]")
{
    CHECK(Cli::classifySource(QStringLiteral("http://example.invalid/os.img.xz"))
          == Cli::SourceKind::Remote);
    CHECK(Cli::classifySource(QStringLiteral("https://example.invalid/os.img.xz"))
          == Cli::SourceKind::Remote);
}

TEST_CASE("The scheme is recognised whatever its case", "[cli][source]")
{
    // A URL pasted out of a browser or a spreadsheet can arrive shouting.
    CHECK(Cli::classifySource(QStringLiteral("HTTP://example.invalid/os.img"))
          == Cli::SourceKind::Remote);
    CHECK(Cli::classifySource(QStringLiteral("HttpS://example.invalid/os.img"))
          == Cli::SourceKind::Remote);
}

TEST_CASE("Another scheme is not treated as a download", "[cli][source]")
{
    // ftp:// and file:// are not fetched. They fall through to the
    // filesystem, where they will not be found -- which is the honest
    // answer, rather than handing libcurl something it will fail on later.
    CHECK(Cli::classifySource(QStringLiteral("ftp://example.invalid/os.img"))
          == Cli::SourceKind::Missing);
}

TEST_CASE("A path that merely contains http is still a path", "[cli][source]")
{
    // The check is on the start of the string, so a directory called
    // "http-images" is not mistaken for a URL.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("http-images/os.img"));
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    CHECK(Cli::classifySource(path) == Cli::SourceKind::LocalFile);
}

TEST_CASE("A real file on disk is used as-is", "[cli][source]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("os.img"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(2048, 'x'));
    f.close();

    CHECK(Cli::classifySource(path) == Cli::SourceKind::LocalFile);
}

TEST_CASE("A source that is not there is told apart from one that is not a file",
          "[cli][source]")
{
    // Two different mistakes deserving two different messages: a typo in a
    // path, versus pointing at a directory.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    CHECK(Cli::classifySource(dir.filePath(QStringLiteral("typo.img")))
          == Cli::SourceKind::Missing);
    CHECK(Cli::classifySource(dir.path()) == Cli::SourceKind::NotRegular);
}

TEST_CASE("An empty source is not there", "[cli][source]")
{
    CHECK(Cli::classifySource(QString()) == Cli::SourceKind::Missing);
}

// -- The secure boot signing key ---------------------------------------

TEST_CASE("A usable signing key passes", "[cli][secureboot-key]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("key.pem"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("-----BEGIN RSA PRIVATE KEY-----\n");
    f.close();

    CHECK(Cli::validateSecureBootKey(path).isEmpty());
}

TEST_CASE("A missing signing key is refused, and named", "[cli][secureboot-key]")
{
    // Signing is not something to fall back from silently: an unsigned image
    // will not boot on a fused board, and the operator needs to know it was
    // the path that was wrong.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("absent.pem"));

    const QString err = Cli::validateSecureBootKey(path);
    REQUIRE_FALSE(err.isEmpty());
    CHECK(err.contains(QStringLiteral("does not exist")));
    CHECK(err.contains(path));
}

TEST_CASE("A signing key that is a directory is refused separately",
          "[cli][secureboot-key]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString err = Cli::validateSecureBootKey(dir.path());
    REQUIRE_FALSE(err.isEmpty());
    CHECK(err.contains(QStringLiteral("not a regular file")));
    CHECK(err.contains(dir.path()));
}

// -- Where the command line will let a script write --------------------
//
// Unless --enable-writing-system-drives is given, the destination must be
// one of the removable volumes the system reports. This is the CLI's
// version of the storage picker greying out the machine's own disk, and
// there is no dialog behind it: a script that names the wrong device is
// refused here or not at all.

namespace {

Drivelist::DeviceDescriptor removable(const std::string &device,
                                      const std::string &description)
{
    Drivelist::DeviceDescriptor d;
    d.device = device;
    d.description = description;
    d.size = 32ull * 1024 * 1024 * 1024;
    d.isRemovable = true;
    d.isUSB = true;
    d.isSystem = false;
    return d;
}

} // namespace

// ══════════════════════════════════════════════════════════════
// What the drive list reports about a drive
//
// Two things read out of it decide what happens to hardware.
//
// The child devices are the partitions unmounted before a write. Looked up
// against the wrong drive they are the wrong partitions -- either the user's
// own filesystems get unmounted, or the ones on the card do not and the
// write fails on a busy device.
//
// The rpiboot chip list is what marks a board as attached in the chooser.
// Reported on every poll it makes the chooser churn; not de-duplicated, one
// board attached twice looks like two.

namespace {

Drivelist::DeviceDescriptor removableWithChildren(
    const std::string &device, const std::vector<std::string> &children)
{
    Drivelist::DeviceDescriptor d = removable(device, "A card reader");
    d.childDevices = children;
    return d;
}

Drivelist::DeviceDescriptor rpibootDevice(const std::string &device,
                                          const std::string &chip)
{
    Drivelist::DeviceDescriptor d = removable(device, "A board in USB boot");
    d.isRpiboot = true;
    d.rpibootChipName = chip;
    return d;
}

} // namespace

TEST_CASE("A drive's partitions are the ones reported for it",
          "[drivelist]")
{
    DriveListModel drives;
    drives.processDriveList({
        removableWithChildren("/dev/sdb", {"/dev/sdb1", "/dev/sdb2"}),
        removableWithChildren("/dev/sdc", {"/dev/sdc1"}),
    });

    CHECK(drives.getChildDevices(QStringLiteral("/dev/sdb"))
          == QStringList{QStringLiteral("/dev/sdb1"), QStringLiteral("/dev/sdb2")});
    CHECK(drives.getChildDevices(QStringLiteral("/dev/sdc"))
          == QStringList{QStringLiteral("/dev/sdc1")});
}

TEST_CASE("A drive that is not in the list has no partitions", "[drivelist]")
{
    // Rather than the first drive's, or whatever the loop happened to leave
    // behind. The caller unmounts what comes back from here.
    DriveListModel drives;
    drives.processDriveList({
        removableWithChildren("/dev/sdb", {"/dev/sdb1"}),
    });

    CHECK(drives.getChildDevices(QStringLiteral("/dev/sdz")).isEmpty());
    CHECK(drives.getChildDevices(QString()).isEmpty());
    CHECK(drives.getChildDevices(QStringLiteral("/dev/sdb1")).isEmpty());
}

TEST_CASE("A drive with no partitions reports none", "[drivelist]")
{
    // A blank card. Not an error, and not somebody else's partitions.
    DriveListModel drives;
    drives.processDriveList({ removableWithChildren("/dev/sdb", {}) });

    CHECK(drives.getChildDevices(QStringLiteral("/dev/sdb")).isEmpty());
}

TEST_CASE("An attached board's chip is reported once", "[drivelist]")
{
    DriveListModel drives;
    rpi_test::SignalLog chips(&drives,
                              &DriveListModel::connectedRpibootChipsChanged);

    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712") });

    REQUIRE(chips.count() == 1);
    CHECK(chips.at(0).at(0).toStringList()
          == QStringList{QStringLiteral("BCM2712")});
}

TEST_CASE("The same board seen twice is still one chip", "[drivelist]")
{
    // A board can appear under more than one node. Two entries for one chip
    // would mark two boards as attached in the chooser.
    DriveListModel drives;
    rpi_test::SignalLog chips(&drives,
                              &DriveListModel::connectedRpibootChipsChanged);

    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712"),
                              rpibootDevice("/dev/sdc", "BCM2712") });

    REQUIRE(chips.count() == 1);
    CHECK(chips.at(0).at(0).toStringList().size() == 1);
}

TEST_CASE("A poll that changes nothing reports nothing", "[drivelist]")
{
    // The poller runs on a timer. Reporting on every pass would have the
    // chooser rebuild its attached markers several times a second.
    DriveListModel drives;
    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712") });

    rpi_test::SignalLog chips(&drives,
                              &DriveListModel::connectedRpibootChipsChanged);

    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712") });
    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712") });

    CHECK(chips.count() == 0);
}

TEST_CASE("A board being unplugged is reported", "[drivelist]")
{
    DriveListModel drives;
    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2712") });

    rpi_test::SignalLog chips(&drives,
                              &DriveListModel::connectedRpibootChipsChanged);

    drives.processDriveList({ removable("/dev/sdb", "An ordinary card") });

    REQUIRE(chips.count() == 1);
    CHECK(chips.at(0).at(0).toStringList().isEmpty());
}

TEST_CASE("Two different boards are both reported, in a settled order",
          "[drivelist]")
{
    // Sorted, so the same two boards do not look like a change depending on
    // the order the poller happened to enumerate them.
    DriveListModel drives;
    rpi_test::SignalLog chips(&drives,
                              &DriveListModel::connectedRpibootChipsChanged);

    drives.processDriveList({ rpibootDevice("/dev/sdc", "BCM2712"),
                              rpibootDevice("/dev/sdb", "BCM2711") });

    REQUIRE(chips.count() == 1);
    CHECK(chips.at(0).at(0).toStringList()
          == QStringList{QStringLiteral("BCM2711"), QStringLiteral("BCM2712")});

    // The same pair the other way round is not a change.
    drives.processDriveList({ rpibootDevice("/dev/sdb", "BCM2711"),
                              rpibootDevice("/dev/sdc", "BCM2712") });
    CHECK(chips.count() == 1);
}

TEST_CASE("A role the view does not know is answered with nothing",
          "[drivelist]")
{
    // Defended twice: the role-name lookup returns nothing and is checked,
    // and asking a QObject for a property with an empty name returns an
    // invalid value anyway. Removing the check fails nothing, so what is
    // pinned here is the outcome.
    DriveListModel drives;
    drives.processDriveList({ removable("/dev/sdb", "A card reader") });
    QAbstractItemModel *view = &drives;

    CHECK_FALSE(view->data(view->index(0, 0), Qt::UserRole + 999).isValid());
    CHECK_FALSE(view->data(view->index(-1, 0), Qt::UserRole + 1).isValid());
    CHECK_FALSE(view->data(view->index(5, 0), Qt::UserRole + 1).isValid());
}

TEST_CASE("A destination among the removable volumes is allowed",
          "[cli][destination]")
{
    DriveListModel drives;
    drives.processDriveList({ removable("/dev/sdb", "Generic Card Reader") });

    CHECK(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdb")));
}

TEST_CASE("A destination that is not listed is refused", "[cli][destination]")
{
    // The system disk is not in the removable list, so naming it here is
    // refused. A script with a typo does not get to write over root.
    DriveListModel drives;
    drives.processDriveList({ removable("/dev/sdb", "Generic Card Reader") });

    CHECK_FALSE(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sda")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/nvme0n1")));
}

TEST_CASE("The match is exact", "[cli][destination]")
{
    // /dev/sdb1 is a partition of the listed disk, not the disk. Writing an
    // image to a partition of a card produces something that will not boot.
    DriveListModel drives;
    drives.processDriveList({ removable("/dev/sdb", "Generic Card Reader") });

    CHECK_FALSE(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdb1")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sd")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives, QString()));
}

TEST_CASE("Nothing plugged in allows nothing", "[cli][destination]")
{
    DriveListModel drives;
    drives.processDriveList({});

    CHECK_FALSE(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdb")));
    CHECK(Cli::removableDestinations(drives).isEmpty());
}

TEST_CASE("The refusal lists what could have been written instead",
          "[cli][destination]")
{
    // With no display and no picker, this list is the operator's only way of
    // finding out what the right answer was.
    DriveListModel drives;
    drives.processDriveList({
        removable("/dev/sdb", "Generic Card Reader"),
        removable("/dev/sdc", "SanDisk Extreme"),
    });

    const QStringList choices = Cli::removableDestinations(drives);
    REQUIRE(choices.size() == 2);
    CHECK(choices.contains(QStringLiteral("/dev/sdb (Generic Card Reader)")));
    CHECK(choices.contains(QStringLiteral("/dev/sdc (SanDisk Extreme)")));
}

TEST_CASE("Every removable volume can be chosen", "[cli][destination]")
{
    DriveListModel drives;
    drives.processDriveList({
        removable("/dev/sdb", "First"),
        removable("/dev/sdc", "Second"),
        removable("/dev/sdd", "Third"),
    });

    CHECK(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdb")));
    CHECK(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdc")));
    CHECK(Cli::destinationIsRemovable(drives, QStringLiteral("/dev/sdd")));
}

// -- Customisation files named on the command line ---------------------
//
// Three of these can be given: cloud-init user-data, cloud-init
// network-config, and a first-run script. All three were read by the same
// twenty lines written out three times; they now share one, and what it
// refuses on is what a script author sees when they get a path wrong.

TEST_CASE("A customisation file is read whole", "[cli][customisation-file]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("user-data"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("#cloud-config\nhostname: pi\n");
    f.close();

    QByteArray contents;
    QString error;
    REQUIRE(Cli::readCustomisationFile(path, QStringLiteral("user-data file"),
                                       contents, error));

    CHECK(error.isEmpty());
    CHECK(contents == QByteArray("#cloud-config\nhostname: pi\n"));
}

TEST_CASE("An empty customisation file is read, not refused",
          "[cli][customisation-file]")
{
    // An empty user-data is a legitimate thing to hand cloud-init.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("empty"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.close();

    QByteArray contents("not cleared");
    QString error;
    REQUIRE(Cli::readCustomisationFile(path, QStringLiteral("user-data file"),
                                       contents, error));
    CHECK(contents.isEmpty());
}

TEST_CASE("A customisation file that is not there names what was wanted",
          "[cli][customisation-file]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QByteArray contents;
    QString error;
    CHECK_FALSE(Cli::readCustomisationFile(dir.filePath(QStringLiteral("absent")),
                                           QStringLiteral("network-config file"),
                                           contents, error));

    CHECK(error.contains(QStringLiteral("network-config file")));
    CHECK(error.contains(QStringLiteral("does not exist")));
    // Named, so a mistyped path can be compared against what was meant.
    CHECK(error.contains(QStringLiteral("absent")));
}

TEST_CASE("Each of the three files is described by its own name",
          "[cli][customisation-file]")
{
    // The operator gave up to three paths. A message that did not say which
    // one was wrong would leave them checking all of them.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString absent = dir.filePath(QStringLiteral("nope"));

    QByteArray contents;
    QString e1, e2, e3;
    Cli::readCustomisationFile(absent, QStringLiteral("user-data file"), contents, e1);
    Cli::readCustomisationFile(absent, QStringLiteral("network-config file"), contents, e2);
    Cli::readCustomisationFile(absent, QStringLiteral("firstrun script"), contents, e3);

    CHECK(e1.contains(QStringLiteral("user-data")));
    CHECK(e2.contains(QStringLiteral("network-config")));
    CHECK(e3.contains(QStringLiteral("firstrun script")));
    CHECK(e1 != e2);
    CHECK(e2 != e3);
}

TEST_CASE("A file that cannot be opened is told apart from one that is absent",
          "[cli][customisation-file]")
{
    // A permissions problem and a typo need different fixes, and with no
    // dialog to interrogate the wording is all the operator has.
    if (::geteuid() == 0)
        SKIP("running as root, which can read a file with no permissions");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("locked"));
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("secret");
    f.close();
    REQUIRE(QFile::setPermissions(path, QFileDevice::Permissions()));

    QByteArray contents;
    QString error;
    CHECK_FALSE(Cli::readCustomisationFile(path, QStringLiteral("user-data file"),
                                           contents, error));
    CHECK(error.contains(QStringLiteral("opening")));
    CHECK_FALSE(error.contains(QStringLiteral("does not exist")));

    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

TEST_CASE("A directory given where a file was wanted is refused",
          "[cli][customisation-file]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QByteArray contents;
    QString error;
    CHECK_FALSE(Cli::readCustomisationFile(dir.path(),
                                           QStringLiteral("firstrun script"),
                                           contents, error));
    CHECK_FALSE(error.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Building the boot image the compute module is handed
//
// createBootImg() packs a set of files into a FAT32 image using mkfs.vfat
// and mtools. That image is what a CM4 or CM5 is booted from during a
// sideload, so a file landing at the wrong path -- or not landing at all --
// is a board that does not come up, with nothing on screen to say why.
//
// mtools is used here as an independent reader: the image is inspected with
// the same tools a person would use, rather than by the code that wrote it.
// ══════════════════════════════════════════════════════════════

namespace {

bool haveMtools()
{
    return rpi_test::haveTool(QStringLiteral("mkfs.vfat"))
        && rpi_test::haveTool(QStringLiteral("mcopy"));
}

// Read a file back out of the image with mtools.
QByteArray readFromImage(const QString &image, const QString &path)
{
    QProcess p;
    p.start(QStringLiteral("mcopy"),
            {QStringLiteral("-i"), image, QStringLiteral("::") + path,
             QStringLiteral("-")});
    if (!p.waitForFinished(10000))
        return {};
    return p.readAllStandardOutput();
}

// List the image the way somebody checking it by hand would.
QString listImage(const QString &image, const QString &dir = QString())
{
    QProcess p;
    p.start(QStringLiteral("mdir"),
            {QStringLiteral("-i"), image, QStringLiteral("::") + dir});
    if (!p.waitForFinished(10000))
        return {};
    return QString::fromUtf8(p.readAllStandardOutput());
}

constexpr qint64 kBootImgSize = 8 * 1024 * 1024;

} // namespace

TEST_CASE("A file put in the boot image can be read back out",
          "[bootimg]")
{
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["config.txt"] = "arm_64bit=1\n";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));
    REQUIRE(QFileInfo::exists(img));

    CHECK(readFromImage(img, QStringLiteral("config.txt"))
          == QByteArray("arm_64bit=1\n"));
}

TEST_CASE("A file in a subdirectory lands at that path", "[bootimg]")
{
    // The firmware tree is nested. A file flattened into the root is a file
    // the bootloader will not find.
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["overlays/vc4-kms-v3d.dtbo"] = "OVERLAY";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));

    CHECK(readFromImage(img, QStringLiteral("overlays/vc4-kms-v3d.dtbo"))
          == QByteArray("OVERLAY"));
}

TEST_CASE("Directories several deep are all created", "[bootimg]")
{
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["a/b/c/deep.bin"] = "DEEP";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));

    CHECK(readFromImage(img, QStringLiteral("a/b/c/deep.bin"))
          == QByteArray("DEEP"));
}

TEST_CASE("Several files sharing a directory all arrive", "[bootimg]")
{
    // The directory is created once for the first file; the rest have to
    // land in it rather than being lost to an "already exists" failure.
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["overlays/one.dtbo"] = "ONE";
    files["overlays/two.dtbo"] = "TWO";
    files["overlays/three.dtbo"] = "THREE";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));

    CHECK(readFromImage(img, QStringLiteral("overlays/one.dtbo")) == QByteArray("ONE"));
    CHECK(readFromImage(img, QStringLiteral("overlays/two.dtbo")) == QByteArray("TWO"));
    CHECK(readFromImage(img, QStringLiteral("overlays/three.dtbo")) == QByteArray("THREE"));
}

TEST_CASE("Root files and nested files coexist", "[bootimg]")
{
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["config.txt"] = "arm_64bit=1\n";
    files["cmdline.txt"] = "console=serial0\n";
    files["overlays/x.dtbo"] = "X";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));

    CHECK(readFromImage(img, QStringLiteral("config.txt")) == QByteArray("arm_64bit=1\n"));
    CHECK(readFromImage(img, QStringLiteral("cmdline.txt")) == QByteArray("console=serial0\n"));
    CHECK(readFromImage(img, QStringLiteral("overlays/x.dtbo")) == QByteArray("X"));
}

TEST_CASE("Binary content survives unchanged", "[bootimg]")
{
    // The bootcode and firmware blobs are binary. A text-mode copy would
    // mangle them in ways that do not show up until the board fails to boot.
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QByteArray blob;
    for (int i = 0; i < 4096; ++i)
        blob.append(static_cast<char>(i & 0xFF));

    QMap<QString, QByteArray> files;
    files["start4.elf"] = blob;

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));
    CHECK(readFromImage(img, QStringLiteral("start4.elf")) == blob);
}

TEST_CASE("An empty set of files makes no image", "[bootimg]")
{
    // Nothing to boot from. Better to refuse than to hand a compute module
    // a formatted but empty volume.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    CHECK_FALSE(BootImgCreator::createBootImg({}, img, kBootImgSize));
    CHECK_FALSE(QFileInfo::exists(img));
}

TEST_CASE("The output directory is created if it is not there", "[bootimg]")
{
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("nested/deeper/boot.img"));

    QMap<QString, QByteArray> files;
    files["config.txt"] = "x\n";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));
    CHECK(QFileInfo::exists(img));
}

TEST_CASE("The image is a filesystem, not just a sized file", "[bootimg]")
{
    // Read back with mdir, which is a different tool from the one that
    // wrote it -- so this checks the image really is mountable FAT32 rather
    // than that our own writer agrees with itself.
    if (!haveMtools())
        SKIP("mtools, and a FAT formatter, are needed to build a boot image");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString img = dir.filePath(QStringLiteral("boot.img"));

    QMap<QString, QByteArray> files;
    files["config.txt"] = "arm_64bit=1\n";

    REQUIRE(BootImgCreator::createBootImg(files, img, kBootImgSize));

    const QString listing = listImage(img);
    INFO("mdir said: " << listing.toStdString());
    CHECK(listing.contains(QStringLiteral("config")));
    CHECK(QFileInfo(img).size() == kBootImgSize);
}

// ══════════════════════════════════════════════════════════════
// The check that customisation actually landed
//
// DownloadThread records a digest of every customisation file it writes and
// reads them back afterwards, because a customisation that fails to land
// fails silently: the card boots, and the hostname, the user and the Wi-Fi
// are simply not the ones that were asked for.
//
// The happy path of that check runs in the write tests. The branches that
// detect a problem did not run anywhere -- which is the half that matters,
// since a verifier that cannot fail is the same as no verifier at all.
// ══════════════════════════════════════════════════════════════

namespace {

// Drives _verifyCustomisation() against a FAT image the test controls.
class VerifiableDownloadThread : public DownloadThread
{
public:
    explicit VerifiableDownloadThread(const QString &imagePath)
        : DownloadThread(QByteArray("file:///dev/null"))
    {
        _file = rpi_imager::FileOperations::Create();
        REQUIRE(_file);
        REQUIRE(_file->OpenDevice(imagePath.toStdString())
                == rpi_imager::FileError::kSuccess);
    }

    // Claim a file was written with these contents, whether or not it was.
    void claimWritten(const QString &name, const QByteArray &contents)
    {
        _recordCustomisationWrite(name, contents);
    }

    void setBytesWrittenForTest(quint64 n) { _bytesWritten.store(n); }

    using DownloadThread::_verifyCustomisation;
};

// BootPartitionFixture's target starts blank -- the image only lands on it
// when a write runs. _verifyCustomisation() needs a real FAT partition to
// read, and without one it takes its "could not check" path and returns true,
// which is not the branch under test here.
void layDownImage(const BootPartitionFixture &fx)
{
    QProcess xz;
    xz.start(QStringLiteral("xz"),
             {QStringLiteral("-dc"), fx.sourceUrl().toLocalFile()});
    REQUIRE(xz.waitForFinished(rpi_test::kFixtureProcessTimeoutMs));
    const QByteArray image = xz.readAllStandardOutput();
    REQUIRE(image.size() > 0);

    QFile t(fx.target());
    REQUIRE(t.open(QIODevice::WriteOnly));
    REQUIRE(t.write(image) == image.size());
    t.close();
}

// Put a file into the FAT partition of a prepared image.
void putFileOnCard(const QString &imagePath, const QString &name,
                   const QByteArray &contents)
{
    QProcess p;
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString local = tmp.filePath(name);
    QFile f(local);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(contents);
    f.close();

    p.start(QStringLiteral("mcopy"),
            {QStringLiteral("-i"), imagePath + QStringLiteral("@@1M"),
             QStringLiteral("-o"), local, QStringLiteral("::") + name});
    p.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
}

} // namespace

TEST_CASE("Customisation verification passes when the card matches",
          "[imagewriter][customisation-verify]")
{
    if (QStandardPaths::findExecutable(QStringLiteral("mcopy")).isEmpty())
        SKIP("mtools is needed to place a file on the card");

    BootPartitionFixture fx;
    layDownImage(fx);
    const QByteArray contents = "#!/bin/bash\nexit 0\n";
    putFileOnCard(fx.target(), QStringLiteral("firstrun.sh"), contents);

    VerifiableDownloadThread t(fx.target());
    t.setBytesWrittenForTest(BootPartitionFixture::kImageSize);
    t.claimWritten(QStringLiteral("firstrun.sh"), contents);

    CHECK(t._verifyCustomisation());
}

TEST_CASE("Customisation verification fails when the file never arrived",
          "[imagewriter][customisation-verify]")
{
    // The failure the check exists for. Nothing was written, but the writer
    // believes it was -- without this branch the card would be handed over
    // as customised.
    BootPartitionFixture fx;
    layDownImage(fx);

    VerifiableDownloadThread t(fx.target());
    t.setBytesWrittenForTest(BootPartitionFixture::kImageSize);
    t.claimWritten(QStringLiteral("firstrun.sh"), "#!/bin/bash\nexit 0\n");

    CHECK_FALSE(t._verifyCustomisation());
}

TEST_CASE("Customisation verification fails when the contents differ",
          "[imagewriter][customisation-verify]")
{
    // Same name, same length, different bytes: a digest check catches this
    // and a size check does not.
    if (QStandardPaths::findExecutable(QStringLiteral("mcopy")).isEmpty())
        SKIP("mtools is needed to place a file on the card");

    BootPartitionFixture fx;
    layDownImage(fx);
    putFileOnCard(fx.target(), QStringLiteral("firstrun.sh"), "AAAAAAAAAA");

    VerifiableDownloadThread t(fx.target());
    t.setBytesWrittenForTest(BootPartitionFixture::kImageSize);
    t.claimWritten(QStringLiteral("firstrun.sh"), "BBBBBBBBBB");

    CHECK_FALSE(t._verifyCustomisation());
}

TEST_CASE("Customisation verification fails when the length differs",
          "[imagewriter][customisation-verify]")
{
    if (QStandardPaths::findExecutable(QStringLiteral("mcopy")).isEmpty())
        SKIP("mtools is needed to place a file on the card");

    BootPartitionFixture fx;
    layDownImage(fx);
    putFileOnCard(fx.target(), QStringLiteral("config.txt"), "arm_64bit=1\n");

    VerifiableDownloadThread t(fx.target());
    t.setBytesWrittenForTest(BootPartitionFixture::kImageSize);
    t.claimWritten(QStringLiteral("config.txt"), "arm_64bit=1\ndtparam=audio=on\n");

    CHECK_FALSE(t._verifyCustomisation());
}

TEST_CASE("Verification of nothing is not a failure",
          "[imagewriter][customisation-verify]")
{
    // A write with no customisation has nothing to check, and must not be
    // reported as a verification failure.
    BootPartitionFixture fx;
    layDownImage(fx);
    VerifiableDownloadThread t(fx.target());
    t.setBytesWrittenForTest(BootPartitionFixture::kImageSize);

    CHECK(t._verifyCustomisation());
}

// ══════════════════════════════════════════════════════════════
// Reading a file the user picked
//
// readFileContents() is how a file chosen in the UI -- an SSH public key,
// most often -- becomes a string. It answers every failure the same way, with
// an empty string, so the caller cannot tell "you picked an empty file" from
// "I could not open it". That is worth pinning rather than discovering later.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Reading a file the user picked", "[imagewriter][files]")
{
    ImageWriter w(nullptr);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("no path at all") {
        CHECK(w.readFileContents(QString()).isEmpty());
    }

    SECTION("a path that is not there") {
        // Not a crash and not a throw: the picker can hand over a file that
        // has since been moved.
        CHECK(w.readFileContents(QDir(dir.path()).filePath(QStringLiteral("absent")))
                  .isEmpty());
    }

    SECTION("a directory rather than a file") {
        CHECK(w.readFileContents(dir.path()).isEmpty());
    }

    SECTION("the contents come back") {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("id_ed25519.pub"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5 user@host");
        f.close();
        CHECK(w.readFileContents(path)
              == QStringLiteral("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5 user@host"));
    }

    SECTION("trailing whitespace is trimmed") {
        // Keys arrive from editors that add a newline. A key with a trailing
        // newline pasted into authorized_keys is a key that does not work.
        const QString path = QDir(dir.path()).filePath(QStringLiteral("padded.pub"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("  ssh-rsa AAAAB3 user@host \n\n");
        f.close();
        CHECK(w.readFileContents(path) == QStringLiteral("ssh-rsa AAAAB3 user@host"));
    }

    SECTION("an empty file is empty, not an error") {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("empty.pub"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.close();
        CHECK(w.readFileContents(path).isEmpty());
    }
}

// ══════════════════════════════════════════════════════════════
// The diagnostics file
//
// exportPerformanceData() produces what somebody attaches to a bug report
// about a slow or failing write. If it writes nothing, or writes something
// that will not parse, the report arrives useless and nobody notices until
// they try to read it.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Exporting performance data before there is any", "[imagewriter][files]")
{
    ImageWriter w(nullptr);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("nothing.json"));

    // Nothing has been written yet, so there is nothing to export. Saying so
    // beats leaving an empty file that looks like a report.
    CHECK_FALSE(w.exportPerformanceDataToFile(path));
    CHECK_FALSE(QFileInfo::exists(path));
}

TEST_CASE("A finished write exports a report that parses", "[imagewriter][files]")
{
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);

    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // Given a name without an extension, as somebody typing into a save
    // dialog would: the file that appears has to be the one named.
    const QString typed = QDir(dir.path()).filePath(QStringLiteral("report"));
    REQUIRE(w.exportPerformanceDataToFile(typed));

    const QString expected = typed + QStringLiteral(".json");
    REQUIRE(QFileInfo::exists(expected));
    CHECK_FALSE(QFileInfo::exists(typed));          // not the extensionless one

    QFile f(expected);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray body = f.readAll();
    CHECK(body.size() > 0);

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    INFO("parse error: " << err.errorString().toStdString());
    CHECK(err.error == QJsonParseError::NoError);
    CHECK_FALSE(doc.isNull());
}

TEST_CASE("An exported report keeps the extension it was given",
          "[imagewriter][files]")
{
    WriteFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    w.setSrc(fx.sourceUrl(), 0, WriteFixture::kSize);
    w.setDst(fx.target(), WriteFixture::kSize);
    REQUIRE(runWrite(w).succeeded);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString named = QDir(dir.path()).filePath(QStringLiteral("run.json"));

    REQUIRE(w.exportPerformanceDataToFile(named));
    CHECK(QFileInfo::exists(named));
    // Not run.json.json.
    CHECK_FALSE(QFileInfo::exists(named + QStringLiteral(".json")));
}

// ══════════════════════════════════════════════════════════════
// Why a local source is refused
//
// One definition, two callers: startWrite() and the continuation that
// resumes after cache verification. They had a copy each and the copies had
// already drifted -- the empty-file case existed on one path only. Testing
// the shared helper is what keeps the next addition from landing on one of
// them again.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A local source is judged the same way whichever path asks",
          "[imagewriter][write]")
{
    ImageWriter w(nullptr);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("a good file is accepted") {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("ok.img"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.close();
        CHECK(w._localSourceError(path).isEmpty());
    }

    SECTION("a file that is not there") {
        const QString msg =
            w._localSourceError(QDir(dir.path()).filePath(QStringLiteral("gone.img")));
        INFO(msg.toStdString());
        CHECK_FALSE(msg.isEmpty());
        CHECK(msg.contains(QStringLiteral("not found")));
    }

    SECTION("a directory is not a source") {
        const QString msg = w._localSourceError(dir.path());
        INFO(msg.toStdString());
        CHECK_FALSE(msg.isEmpty());
        CHECK(msg.contains(QStringLiteral("regular file")));
    }

    SECTION("an empty file is refused on both paths") {
        // The case that had drifted. Zero bytes clears the capacity check,
        // extracts to nothing, and the write used to report success.
        const QString path = QDir(dir.path()).filePath(QStringLiteral("empty.img"));
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.close();
        const QString msg = w._localSourceError(path);
        INFO(msg.toStdString());
        CHECK_FALSE(msg.isEmpty());
        CHECK(msg.contains(QStringLiteral("empty")));
    }

    SECTION("every refusal names the file") {
        // The message goes straight to a dialog; a user with several images
        // needs to know which one was rejected.
        const QString missing = QDir(dir.path()).filePath(QStringLiteral("which-one.img"));
        CHECK(w._localSourceError(missing).contains(QStringLiteral("which-one.img")));
        CHECK(w._localSourceError(dir.path()).contains(dir.path()));
    }
}

// ══════════════════════════════════════════════════════════════
// Which kind of write this is going to be
//
// startWrite() decides between four routes by asking three questions in a
// fixed order, and the order is the interesting part: it is what happens when
// more than one is true at once. That was implied by the arrangement of three
// ifs at the top of an eight-hundred-line function and tested nowhere.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An ordinary image takes the ordinary path", "[imagewriter][write]")
{
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")), 0, 0);
    CHECK(w.choosePath() == ImageWriter::WritePath::Normal);
}

TEST_CASE("The erase sentinel takes the erase path", "[imagewriter][write]")
{
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("internal://format")), 0, 0);
    CHECK(w.choosePath() == ImageWriter::WritePath::Erase);
}

TEST_CASE("A near miss for the erase sentinel is an ordinary image",
          "[imagewriter][write]")
{
    // The single gate between writing an image and destroying a card, so
    // anything that is not the sentinel has to miss it. Note the trailing
    // slash counts as a miss: QUrl keeps the path, so it does not normalise
    // away.
    ImageWriter w(nullptr);
    for (const char *almost : {"internal://formatt", "internal://forma",
                               "internal://format/", "internal://reformat",
                               "https://example.invalid/internal://format"}) {
        w.setSrc(QUrl(QString::fromLatin1(almost)), 0, 0);
        INFO(almost);
        CHECK(w.choosePath() == ImageWriter::WritePath::Normal);
    }
}

TEST_CASE("The erase sentinel is matched however it is capitalised",
          "[imagewriter][write]")
{
    // Not leniency in the comparison -- QUrl normalises a scheme and host to
    // lower case, as RFC 3986 says they are case-insensitive, so all of these
    // are literally the same URL by the time anything compares them. Measured
    // rather than assumed: QUrl("INTERNAL://FORMAT").toString() really is
    // "internal://format".
    ImageWriter w(nullptr);
    for (const char *spelling : {"internal://format", "INTERNAL://FORMAT",
                                 "Internal://Format"}) {
        w.setSrc(QUrl(QString::fromLatin1(spelling)), 0, 0);
        INFO(spelling);
        CHECK(w.choosePath() == ImageWriter::WritePath::Erase);
    }
}

TEST_CASE("A device in fastboot mode takes the fastboot path",
          "[imagewriter][write]")
{
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")), 0, 0);
    w.setFastbootDevice(QStringLiteral("0001-fastboot"), 8ull * 1024 * 1024 * 1024);
    CHECK(w.choosePath() == ImageWriter::WritePath::FastbootDevice);
}

TEST_CASE("A device needing sideloading takes the rpiboot path",
          "[imagewriter][write]")
{
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")), 0, 0);
    w.setRpibootDevice(QStringLiteral("0001-rpiboot"), QStringLiteral("mmcblk0"));
    CHECK(w.choosePath() == ImageWriter::WritePath::RpibootDevice);
}

TEST_CASE("Fastboot beats rpiboot when both are somehow set",
          "[imagewriter][write]")
{
    // setFastbootDevice() clears the rpiboot flag, which is the real
    // protection; this pins the decision as well, so a future caller that
    // sets them separately still lands somewhere defined.
    ImageWriter w(nullptr);
    w.setSrc(QUrl(QStringLiteral("https://example.invalid/os.img.xz")), 0, 0);
    w.setRpibootDevice(QStringLiteral("0001-rpiboot"), QStringLiteral("mmcblk0"));
    w.setFastbootDevice(QStringLiteral("0001-fastboot"), 8ull * 1024 * 1024 * 1024);
    CHECK(w.choosePath() == ImageWriter::WritePath::FastbootDevice);
}

TEST_CASE("Erasing a Compute Module goes over USB, not down the card path",
          "[imagewriter][write]")
{
    // Both true at once: the erase sentinel selected against a device already
    // in fastboot mode. The fastboot path wins, and it has to -- there is no
    // block device here to write a partition table to, so taking the erase
    // path would be reaching for a card that is not there.
    ImageWriter w(nullptr);
    w.setFastbootDevice(QStringLiteral("0001-fastboot"), 8ull * 1024 * 1024 * 1024);
    w.setSrc(QUrl(QStringLiteral("internal://format")), 0, 0);
    CHECK(w.choosePath() == ImageWriter::WritePath::FastbootDevice);
}

// ══════════════════════════════════════════════════════════════
// Pulling the drive out
//
// onSelectedDeviceRemoved() branches five ways on the state the write is in,
// and three of those branches exist to say nothing. That is the part worth
// pinning down: a card ejected after a write finished, or pulled after a
// write already failed, must not raise a second complaint about a problem
// the user has either already seen or does not have. Nothing here was
// tested, so the silent branches and the speaking one were indistinguishable.
// ══════════════════════════════════════════════════════════════

namespace {

// The removal handling is a set of protected slots the drive-list poller
// calls. Exposing them is enough to drive it; none of them touch a device.
class RemovableDriveWriter : public ImageWriter
{
public:
    RemovableDriveWriter() : ImageWriter(nullptr) {}

    using ImageWriter::onSelectedDeviceRemoved;
    using ImageWriter::onSuccess;
    using ImageWriter::onError;
    using ImageWriter::onCancelled;

    int state() const { return property("writeState").toInt(); }
};

// A writer with an image and a drive chosen, ready to go.
std::unique_ptr<RemovableDriveWriter> writerWithDriveChosen(const QString &drive)
{
    auto w = std::make_unique<RemovableDriveWriter>();
    w->setSrc(QUrl(QStringLiteral("file:///tmp/whatever.img")));
    w->setDst(drive, 1024 * 1024);
    return w;
}

} // namespace

TEST_CASE("Some other drive disappearing leaves the chosen one alone", "[imagewriter][removal]")
{
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));
    REQUIRE(w->readyToWrite());

    rpi_test::SignalLog removed(w.get(), &ImageWriter::selectedDeviceRemoved);

    w->onSelectedDeviceRemoved(QStringLiteral("/dev/sdz"));

    CHECK(removed.count() == 0);
    CHECK(w->readyToWrite());
}

TEST_CASE("Losing the chosen drive takes the Write button with it", "[imagewriter][removal]")
{
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));
    REQUIRE(w->readyToWrite());

    rpi_test::SignalLog removed(w.get(), &ImageWriter::selectedDeviceRemoved);

    w->onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(removed.count() == 1);
    CHECK_FALSE(w->readyToWrite());
}

TEST_CASE("A card ejected after the write finished is not reported as a fault", "[imagewriter][removal]")
{
    // Taking the card out is the next thing anyone does after a successful
    // write. Announcing it as a removal would turn the last thing the user
    // sees from "done" into a warning.
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));
    w->onSuccess();
    REQUIRE(w->state() == static_cast<int>(ImageWriter::WriteState::Succeeded));

    rpi_test::SignalLog removed(w.get(), &ImageWriter::selectedDeviceRemoved);

    w->onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(removed.count() == 0);
}

TEST_CASE("A drive pulled after a failure does not raise a second complaint", "[imagewriter][removal]")
{
    // The error is already on screen. Pulling the card that caused it should
    // not add a second, less specific message on top.
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));
    w->onError(QStringLiteral("write failed"));
    REQUIRE(w->state() == static_cast<int>(ImageWriter::WriteState::Failed));

    rpi_test::SignalLog removed(w.get(), &ImageWriter::selectedDeviceRemoved);

    w->onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(removed.count() == 0);
}

TEST_CASE("A drive pulled after cancelling does not raise a second complaint", "[imagewriter][removal]")
{
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));
    w->onCancelled();
    REQUIRE(w->state() == static_cast<int>(ImageWriter::WriteState::Cancelled));

    rpi_test::SignalLog removed(w.get(), &ImageWriter::selectedDeviceRemoved);

    w->onSelectedDeviceRemoved(QStringLiteral("/dev/null"));

    CHECK(removed.count() == 0);
}

TEST_CASE("Cancelling on purpose is reported as an ordinary cancellation", "[imagewriter][removal]")
{
    // onCancelled() chooses between two signals, and the UI says something
    // different for each. With nothing having removed a device, it is the
    // plain one.
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));

    rpi_test::SignalLog cancelled(w.get(), &ImageWriter::cancelled);
    rpi_test::SignalLog removalCancelled(w.get(), &ImageWriter::writeCancelledDueToDeviceRemoval);

    w->onCancelled();

    CHECK(cancelled.count() == 1);
    CHECK(removalCancelled.count() == 0);
}

// ══════════════════════════════════════════════════════════════
// Not telling the user two different things about one write
// ══════════════════════════════════════════════════════════════

TEST_CASE("A success arriving after a failure is ignored", "[imagewriter][removal]")
{
    // The guard exists because FastbootFlashThread can fall through to its
    // success path after a flash has already failed. Were it to get through,
    // the user would be shown a completed write over the top of the error
    // explaining why it did not complete.
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));

    rpi_test::SignalLog succeeded(w.get(), &ImageWriter::success);

    w->onError(QStringLiteral("flash failed"));
    w->onSuccess();

    CHECK(succeeded.count() == 0);
    CHECK(w->state() == static_cast<int>(ImageWriter::WriteState::Failed));
}

TEST_CASE("A success arriving after a cancellation is ignored", "[imagewriter][removal]")
{
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));

    rpi_test::SignalLog succeeded(w.get(), &ImageWriter::success);

    w->onCancelled();
    w->onSuccess();

    CHECK(succeeded.count() == 0);
    CHECK(w->state() == static_cast<int>(ImageWriter::WriteState::Cancelled));
}

TEST_CASE("A second error does not reach the user twice", "[imagewriter][removal]")
{
    auto w = writerWithDriveChosen(QStringLiteral("/dev/null"));

    rpi_test::SignalLog failed(w.get(), &ImageWriter::error);

    w->onError(QStringLiteral("the real problem"));
    w->onError(QStringLiteral("a later consequence of it"));

    REQUIRE(failed.count() == 1);
    CHECK(failed.at(0).at(0).toString() == QStringLiteral("the real problem"));
}

// ══════════════════════════════════════════════════════════════
// What the custom repository field shows the user
//
// customRepoHost() is the string someone reads before deciding whether to
// trust an OS list from somewhere other than Raspberry Pi. It is the only
// place the origin of that list is shown, which makes it a security control
// rather than a label: it deliberately displays the punycode form, so a
// hostname built from lookalike Unicode cannot render as the name it is
// imitating. None of that was tested.
// ══════════════════════════════════════════════════════════════

TEST_CASE("The default repository is not presented as a custom one", "[imagewriter][repo]")
{
    ImageWriter w(nullptr);
    CHECK_FALSE(w.customRepo());
    CHECK(w.customRepoHost().isEmpty());
}

TEST_CASE("A custom repository shows the host it will fetch from", "[imagewriter][repo]")
{
    ImageWriter w(nullptr);
    w.setCustomRepo(QUrl(QStringLiteral("https://mirror.example.com/os_list.json")));

    REQUIRE(w.customRepo());
    CHECK(w.customRepoHost() == QStringLiteral("mirror.example.com"));
}

TEST_CASE("A lookalike hostname cannot render as the name it imitates", "[imagewriter][repo]")
{
    // U+0430 CYRILLIC SMALL LETTER A is indistinguishable from Latin 'a' in
    // most fonts, so this URL reads as raspberrypi.com to a human being while
    // resolving somewhere else entirely. Displaying the decoded form would
    // hand an attacker the appearance of the real thing.
    ImageWriter w(nullptr);
    const QString deceptive =
        QString::fromUtf8("https://r\xD0\xB0spberrypi.com/os_list.json");
    w.setCustomRepo(QUrl(deceptive));

    REQUIRE(w.customRepo());
    const QString shown = w.customRepoHost();
    INFO("shown as: " << shown.toStdString());

    // The whole point: what is displayed is not the name being imitated.
    CHECK(shown != QStringLiteral("raspberrypi.com"));
    CHECK_FALSE(shown.contains(QString::fromUtf8("\xD0\xB0")));

    // Punycode makes the substitution visible rather than merely absent.
    CHECK(shown.contains(QStringLiteral("xn--")));
}

TEST_CASE("An ordinary hostname is shown without a warning", "[imagewriter][repo]")
{
    // The counterpart to the test above: the marker has to mean something,
    // so it must not appear on a hostname that is exactly what it looks like.
    ImageWriter w(nullptr);
    w.setCustomRepo(QUrl(QStringLiteral("https://raspberrypi.com/os_list.json")));

    const QString shown = w.customRepoHost();
    CHECK(shown == QStringLiteral("raspberrypi.com"));
    CHECK_FALSE(shown.contains(QStringLiteral("xn--")));
}

TEST_CASE("A repository read from a file shows the file, not a host", "[imagewriter][repo]")
{
    // A file:// URL has no host at all, so without this the field would go
    // blank and the user would be told nothing about where the list came from.
    ImageWriter w(nullptr);
    w.setCustomRepo(QUrl(QStringLiteral("file:///home/someone/my_os_list.json")));

    REQUIRE(w.customRepo());
    CHECK(w.customRepoHost() == QStringLiteral("my_os_list.json"));
}

TEST_CASE("A hostname too long for the field is truncated visibly", "[imagewriter][repo]")
{
    // Built from short labels: a single label over 63 characters is not a
    // legal hostname and QUrl discards it, which would test nothing.
    QString host;
    for (int i = 0; i < 8; ++i)
        host += QStringLiteral("segment%1.").arg(i);
    host += QStringLiteral("example.com");
    REQUIRE(host.length() > 50);

    ImageWriter w(nullptr);
    w.setCustomRepo(QUrl(QStringLiteral("https://") + host + QStringLiteral("/os_list.json")));

    const QString shown = w.customRepoHost();
    CHECK(shown.length() == 50);
    CHECK(shown.endsWith(QStringLiteral("...")));
    CHECK(shown.startsWith(QStringLiteral("segment0.")));
}

// ══════════════════════════════════════════════════════════════
// Choosing a local image from the file dialog
//
// onFileSelected() is the slot the native file dialog hands its answer to.
// It filters that answer -- anything that is not a regular file is dropped
// on the floor with a qDebug() line and no signal -- and it remembers the
// directory so the next dialog opens where the last one did.
// ══════════════════════════════════════════════════════════════

namespace {

class FileChoosingWriter : public ImageWriter
{
public:
    FileChoosingWriter() : ImageWriter(nullptr) {}
    using ImageWriter::onFileSelected;
};

} // namespace

TEST_CASE("Choosing a file reports it as a local URL", "[imagewriter][filedialog]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("chosen.img"));
    {
        QFile f(imagePath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("not really an image");
    }

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.onFileSelected(imagePath);

    REQUIRE(chosen.count() == 1);
    CHECK(chosen.at(0).at(0).toUrl() == QUrl::fromLocalFile(imagePath));
}

TEST_CASE("Choosing a directory reports nothing at all", "[imagewriter][filedialog]")
{
    // A dialog can hand back a directory, and the rest of the write path
    // assumes a file it can open. Passing one through would fail later and
    // further away than here.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.onFileSelected(dir.path());

    CHECK(chosen.count() == 0);
}

TEST_CASE("Choosing a file that is not there reports nothing", "[imagewriter][filedialog]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.onFileSelected(dir.filePath(QStringLiteral("never_created.img")));

    CHECK(chosen.count() == 0);
}

TEST_CASE("Choosing a file remembers the directory for next time", "[imagewriter][filedialog]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("remembered.img"));
    {
        QFile f(imagePath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    QSettings settings;
    settings.setValue(QStringLiteral("lastpath"), QStringLiteral("/somewhere/else"));
    settings.sync();

    FileChoosingWriter w;
    w.onFileSelected(imagePath);

    QSettings after;
    CHECK(after.value(QStringLiteral("lastpath")).toString() == QFileInfo(imagePath).path());
}

TEST_CASE("A directory that was not chosen does not overwrite the remembered one",
          "[imagewriter][filedialog]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    QSettings settings;
    settings.setValue(QStringLiteral("lastpath"), QStringLiteral("/the/previous/one"));
    settings.sync();

    FileChoosingWriter w;
    w.onFileSelected(dir.path());

    QSettings after;
    CHECK(after.value(QStringLiteral("lastpath")).toString()
          == QStringLiteral("/the/previous/one"));
}

TEST_CASE("A chosen file says what it was chosen for", "[imagewriter][filedialog]")
{
    // There is one file dialog and one signal reporting its result, and more
    // than one part of the interface asks. Without saying which request a
    // selection is answering, a repository file chosen from the options
    // dialog was also taken as a custom image: the selected OS became the
    // json file and the user's staged customisation went with it. See
    // tst_file_choice_routing.qml for that, measured.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString repoPath = dir.filePath(QStringLiteral("repo.json"));
    {
        QFile f(repoPath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("{}");
    }

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.onFileSelected(repoPath, QStringLiteral("repository"));

    REQUIRE(chosen.count() == 1);
    CHECK(chosen.at(0).at(0).toUrl() == QUrl::fromLocalFile(repoPath));
    CHECK(chosen.at(0).at(1).toString() == QStringLiteral("repository"));
}

TEST_CASE("A choice that does not say is a custom image", "[imagewriter][filedialog]")
{
    // What an untagged selection has always meant, kept so a caller that
    // has not been told about purposes still behaves as it did.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("untagged.img"));
    {
        QFile f(imagePath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.onFileSelected(imagePath);

    REQUIRE(chosen.count() == 1);
    CHECK(chosen.at(0).at(1).toString() == QStringLiteral("customImage"));
}

TEST_CASE("The fallback image picker reports a custom image", "[imagewriter][filedialog]")
{
    // The styled dialog used where no native one is available goes through
    // its own entry point, which has to tag the selection the same way --
    // otherwise choosing a custom image works on one platform and silently
    // does nothing on another.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString imagePath = dir.filePath(QStringLiteral("fallback.img"));
    {
        QFile f(imagePath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    FileChoosingWriter w;
    rpi_test::SignalLog chosen(&w, &ImageWriter::fileSelected);

    w.acceptCustomImageFromQml(QUrl::fromLocalFile(imagePath));

    REQUIRE(chosen.count() == 1);
    CHECK(chosen.at(0).at(0).toUrl() == QUrl::fromLocalFile(imagePath));
    CHECK(chosen.at(0).at(1).toString() == QStringLiteral("customImage"));
}

// ══════════════════════════════════════════════════════════════
// Asking for an organisation key when there is none
//
// The Pi Connect step can mint a single-use auth key from an
// organisation's API key, so a fleet of boards each join the
// organisation on first boot without anyone pasting a per-device token.
//
// The first thing that can go wrong is having no organisation key to mint
// from -- the feature turned on without one configured, or the key cleared
// between sessions. The refusal for that was uncovered, and it is the one
// the user sees: the step puts the message straight on screen, so a refusal
// with no message is a button that appears to do nothing.
//
// The paths past it are deliberately not driven here. They build a
// ConnectDeviceRegistrar against the real Connect API, and there is no seam
// to point it elsewhere from this function -- the registrar's own tests
// cover the request and its failures against a local server, with a base
// URL it takes as an argument.

TEST_CASE("Minting an organisation key without one configured says so",
          "[imagewriter][connect]")
{
    WriterWithRemoval writer;
    writer.clearConnectOrgRegistration();
    REQUIRE_FALSE(writer.hasConnectOrgRegistration());

    const QVariantMap result =
        writer.requestOrgAuthKey(QStringLiteral("a device"), 7);

    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    CHECK_FALSE(result.value(QStringLiteral("error")).toString().isEmpty());
    CHECK_THAT(result.value(QStringLiteral("error")).toString().toStdString(),
               ContainsSubstring("organisation"));
}

TEST_CASE("A refusal to mint reports no key of its own", "[imagewriter][connect]")
{
    // Whatever comes back must not look like a token. The step writes what
    // it is given into the image, and "ok" being false is the only thing
    // between a failed mint and a board configured with a rejected key.
    WriterWithRemoval writer;
    writer.clearConnectOrgRegistration();

    const QVariantMap result =
        writer.requestOrgAuthKey(QStringLiteral("a device"), 7);

    CHECK_FALSE(result.contains(QStringLiteral("secret")));
    CHECK_FALSE(result.contains(QStringLiteral("id")));
}

// ══════════════════════════════════════════════════════════════
// Whose Wi-Fi passphrase gets written into the image
//
// The wireless step offers to reuse the network this computer is on, which
// means reading this machine's own passphrase out of NetworkManager and
// writing it into the image. That is a credential the user never typed into
// Imager, so which requests it answers matters: a request naming a
// different network must not be handed the passphrase for this one.
//
// Getting that wrong is quiet in both directions. Written into an image
// configured for another network the Pi simply fails to associate, and the
// user has this machine's home passphrase sitting in plain text in a
// firstrun script on a card they may pass on. NetworkManagerApi was at 0%:
// nothing had ever called it.
//
// getPSKForSSID() is what decides, and it is not virtual, so it runs here
// exactly as it ships. What the fake replaces is the two calls that reach
// the host -- which network this computer is on, and its passphrase --
// because those are the machine's, not the test's.

#if defined(__linux__) && !defined(CLI_ONLY_BUILD)

#include "linux/networkmanagerapi.h"

namespace {

class FakeHostNetwork : public NetworkManagerApi
{
public:
    QByteArray ssid;
    QByteArray psk;
    int psk_reads = 0;

    QByteArray getSSID() override { return ssid; }
    QByteArray getPSK() override { ++psk_reads; return psk; }

    using NetworkManagerApi::_getSSIDofInterface;
};

} // namespace

TEST_CASE("The passphrase is offered for the network it belongs to", "[wlan]")
{
    FakeHostNetwork net;
    net.ssid = "Pi Towers";
    net.psk = "correct horse battery staple";

    CHECK(net.getPSKForSSID("Pi Towers") == QByteArray("correct horse battery staple"));
}

TEST_CASE("The passphrase is not offered for any other network", "[wlan]")
{
    // The one that matters. Answering this with the local passphrase writes
    // it into an image meant for somewhere else.
    FakeHostNetwork net;
    net.ssid = "Pi Towers";
    net.psk = "correct horse battery staple";

    CHECK(net.getPSKForSSID("Next Door") == QByteArray());
    CHECK(net.psk_reads == 0);  // not even read, let alone returned
}

TEST_CASE("A network name has to match exactly to be answered", "[wlan]")
{
    // SSIDs are bytes, and two that differ by case or by a space are two
    // different networks as far as any access point is concerned.
    FakeHostNetwork net;
    net.ssid = "Pi Towers";
    net.psk = "correct horse battery staple";

    CHECK(net.getPSKForSSID("pi towers") == QByteArray());
    CHECK(net.getPSKForSSID("PI TOWERS") == QByteArray());
    CHECK(net.getPSKForSSID("Pi Towers ") == QByteArray());
    CHECK(net.getPSKForSSID(" Pi Towers") == QByteArray());
    CHECK(net.getPSKForSSID("Pi") == QByteArray());
    CHECK(net.getPSKForSSID("Pi Towers 5GHz") == QByteArray());
    CHECK(net.psk_reads == 0);
}

TEST_CASE("A machine on no network answers nothing", "[wlan]")
{
    // Without the "is there a network at all" half of the condition, an
    // empty request matches an empty current network and the passphrase
    // goes out to a caller that named nothing.
    FakeHostNetwork net;
    net.ssid = "";
    net.psk = "correct horse battery staple";

    CHECK(net.getPSKForSSID("") == QByteArray());
    CHECK(net.getPSKForSSID("Pi Towers") == QByteArray());
    CHECK(net.psk_reads == 0);
}

TEST_CASE("A request naming nothing is answered with nothing", "[wlan]")
{
    // Defended twice: ImageWriter::getPSKForSSID() refuses an empty name
    // before this is reached. Pinned at this level because it is the layer
    // that holds the credential, and because the caller's guard is the one
    // more likely to be moved.
    FakeHostNetwork net;
    net.ssid = "Pi Towers";
    net.psk = "correct horse battery staple";

    CHECK(net.getPSKForSSID("") == QByteArray());
    CHECK(net.psk_reads == 0);
}

TEST_CASE("An interface that is not there has no network name", "[wlan]")
{
    // The wireless-extensions ioctl against a name no interface has. It
    // fails, and the buffer it would have filled has to be treated as
    // empty rather than read back as whatever was on the stack.
    FakeHostNetwork net;

    CHECK(net._getSSIDofInterface("nosuchif0") == QByteArray());
    CHECK(net._getSSIDofInterface("") == QByteArray());
}

TEST_CASE("There is one holder of the local credentials", "[wlan]")
{
    // Everything reaches this through WlanCredentials::instance(). A second
    // instance would mean a second trip to the keychain or the system bus,
    // which on macOS is a second password prompt for the user.
    WlanCredentials *first = WlanCredentials::instance();
    REQUIRE(first != nullptr);
    CHECK(WlanCredentials::instance() == first);
}

#endif  // __linux__ && !CLI_ONLY_BUILD

// ══════════════════════════════════════════════════════════════
// Which repository URLs are allowed to become the OS list
//
// isValidRepoUrl() decides whether an address the user typed, or one that
// arrived in a deep link, is allowed to replace the source of every image
// the application offers. It had no test, including the part that exists
// because of a bug that already reached users.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A repository URL must be http and must name a list", "[imagewriter][repo]")
{
    ImageWriter w(nullptr);

    SECTION("an ordinary json list is accepted") {
        CHECK(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.json")));
        CHECK(w.isValidRepoUrl(QStringLiteral("http://example.com/os_list.json")));
    }

    SECTION("the manifest extension is accepted too") {
        CHECK(w.isValidRepoUrl(
            QStringLiteral("https://example.com/repo." MANIFEST_EXTENSION)));
    }

    SECTION("a pre-signed URL keeps its query string") {
        // Cloud blob storage hands out the list with a SAS token attached.
        // Rejecting those would make a perfectly ordinary hosting
        // arrangement unusable.
        CHECK(w.isValidRepoUrl(QStringLiteral(
            "https://acct.blob.core.windows.net/c/manifest.json?sv=2021&sig=abc%3D")));
        CHECK(w.isValidRepoUrl(QStringLiteral("https://example.com/os.json#section")));
    }

    SECTION("the extension has to come before the query, not merely appear") {
        // Otherwise ".../evil?x=.json" passes by putting the extension
        // somewhere the server never sees as a path.
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/evil?x=.json")));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/evil#.json")));
    }

    SECTION("a scheme that is not http is refused") {
        // file:// would read the local disk, and the others are not fetches
        // at all.
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("file:///etc/passwd.json")));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("ftp://example.com/os.json")));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("javascript:alert(1)//.json")));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("data:application/json,[]")));
    }

    SECTION("something that is not a list is refused") {
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.xml")));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/")));
        CHECK_FALSE(w.isValidRepoUrl(QString()));
        CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("not a url at all")));
    }
}

TEST_CASE("A repository URL copied out of a browser is not accepted with its newline",
          "[imagewriter][repo]")
{
    // Issue #1687. PCRE2 lets '$' match immediately before a trailing
    // newline, so an address copied from a browser -- which is exactly how
    // someone gets a URL into this field -- validated, and then reached the
    // fetch as a %0A-suffixed URL that could not resolve. The pattern is
    // anchored \A..\z for this reason, and nothing checked it.
    ImageWriter w(nullptr);

    CHECK(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.json")));

    CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.json\n")));
    CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.json\r\n")));
    CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/os_list.json ")));
    CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("\nhttps://example.com/os_list.json")));

    // Trailing whitespace after a query string is the same hazard.
    CHECK_FALSE(w.isValidRepoUrl(QStringLiteral("https://example.com/os.json?sig=a\n")));
}

// ══════════════════════════════════════════════════════════════
// What may go into an Authorization header
//
// The organisation API key is pasted by the user and then concatenated
// into "Authorization: Bearer <key>" before it reaches curl. A pasted
// value carrying a carriage return or newline would end the header early
// and let whatever followed be read as further headers -- request
// splitting, from a field whose whole purpose is to accept an opaque
// string from the clipboard. The guard refuses control characters up
// front rather than trusting the receiver, and nothing tested it.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An organisation key carrying control characters is refused",
          "[imagewriter][connect]")
{
    auto key = GENERATE(
        QStringLiteral("abc\rdef"),          // CR alone ends a header line
        QStringLiteral("abc\ndef"),          // LF alone does too
        QStringLiteral("abc\r\nX-Evil: 1"),  // the full injection
        QStringLiteral("abc\tdef"),          // any C0, not just the useful ones
        QStringLiteral("abc\x01" "def"),
        QStringLiteral("abc\x7F" "def"));    // DEL is not printable either

    ImageWriter w(nullptr);
    w.clearConnectOrgRegistration();
    REQUIRE_FALSE(w.hasConnectOrgRegistration());

    w.setConnectOrgRegistration(key, QStringLiteral("Some description"));

    INFO("key: " << key.toStdString());
    CHECK_FALSE(w.hasConnectOrgRegistration());

    w.clearConnectOrgRegistration();
}

TEST_CASE("A refused organisation key leaves the previous one alone",
          "[imagewriter][connect]")
{
    // Rejecting has to mean "nothing happened", not "the good key is gone
    // and the bad one was not stored" -- which would silently unenrol the
    // user from an organisation because of a bad paste.
    ImageWriter w(nullptr);
    w.setConnectOrgRegistration(QStringLiteral("good-key"), QStringLiteral("Mine"));
    REQUIRE(w.hasConnectOrgRegistration());

    w.setConnectOrgRegistration(QStringLiteral("bad\r\nkey"), QStringLiteral("Replaced"));

    CHECK(w.hasConnectOrgRegistration());
    CHECK(w.getConnectOrgDescription() == QStringLiteral("Mine"));

    w.clearConnectOrgRegistration();
}

TEST_CASE("An ordinary organisation key is accepted and trimmed",
          "[imagewriter][connect]")
{
    ImageWriter w(nullptr);
    w.clearConnectOrgRegistration();

    // Pasting from a browser brings whitespace with it; that is not a
    // reason to refuse the key.
    w.setConnectOrgRegistration(QStringLiteral("  rpi-org-key-123  "),
                                QStringLiteral("  Lab bench  "));

    CHECK(w.hasConnectOrgRegistration());
    CHECK(w.getConnectOrgDescription() == QStringLiteral("Lab bench"));

    w.clearConnectOrgRegistration();
}

// ══════════════════════════════════════════════════════════════
// Which Pi Connect tokens are accepted
//
// The token is pasted from the Connect website, or arrives in a deep link
// the browser hands back, and it is what enrols the device on first boot.
// Getting the check wrong goes wrong in both directions: too lax and a
// mistyped token ships in the image, so the board comes up and never
// appears in the user's Connect account with nothing to say why; too
// strict and a perfectly good token is refused at the last step before
// writing. Neither verifyAuthKey nor parseTokenFromUrl had a test.
// ══════════════════════════════════════════════════════════════

namespace {
// 24 Base58 characters, which is the length the current tokens carry.
const QString kPayload24 = QStringLiteral("abcdefghijkmnpqrstuvwxyz");
}

TEST_CASE("A well-formed Connect token is accepted", "[imagewriter][connect]")
{
    ImageWriter w(nullptr);

    // rpuak_ is a per-user key, rpoak_ an organisation one; both are real.
    CHECK(w.verifyAuthKey(QStringLiteral("rpuak_") + kPayload24, true));
    CHECK(w.verifyAuthKey(QStringLiteral("rpoak_") + kPayload24, true));
}

TEST_CASE("A token without a recognised prefix is refused", "[imagewriter][connect]")
{
    ImageWriter w(nullptr);

    CHECK_FALSE(w.verifyAuthKey(kPayload24, true));
    CHECK_FALSE(w.verifyAuthKey(QStringLiteral("rpxak_") + kPayload24, true));
    CHECK_FALSE(w.verifyAuthKey(QStringLiteral("RPUAK_") + kPayload24, true));
    CHECK_FALSE(w.verifyAuthKey(QStringLiteral("rpuak") + kPayload24, true));
    CHECK_FALSE(w.verifyAuthKey(QString(), true));
    CHECK_FALSE(w.verifyAuthKey(QStringLiteral("rpuak_"), true));
}

TEST_CASE("A token carrying a character Base58 leaves out is refused",
          "[imagewriter][connect]")
{
    // Base58 omits 0, O, I and l precisely because they are hard to tell
    // apart. A token containing one is a transcription error, which is
    // exactly the case worth catching before it reaches an image.
    auto ambiguous = GENERATE(QChar('0'), QChar('O'), QChar('I'), QChar('l'));

    ImageWriter w(nullptr);
    QString payload = kPayload24;
    payload[0] = ambiguous;

    INFO("payload begins with: " << QString(ambiguous).toStdString());
    CHECK_FALSE(w.verifyAuthKey(QStringLiteral("rpuak_") + payload, true));
}

TEST_CASE("A token of the wrong length is refused in strict mode",
          "[imagewriter][connect]")
{
    ImageWriter w(nullptr);
    const QString prefix = QStringLiteral("rpuak_");

    CHECK_FALSE(w.verifyAuthKey(prefix + kPayload24.left(23), true));
    CHECK_FALSE(w.verifyAuthKey(prefix + kPayload24 + QStringLiteral("z"), true));
    CHECK(w.verifyAuthKey(prefix + kPayload24, true));
}

TEST_CASE("A longer token is accepted when the length is not pinned",
          "[imagewriter][connect]")
{
    // The non-strict form exists so a future token that grows is not
    // rejected by a client shipped before it. Shorter than today's is still
    // refused either way.
    ImageWriter w(nullptr);
    const QString prefix = QStringLiteral("rpuak_");

    CHECK(w.verifyAuthKey(prefix + kPayload24 + QStringLiteral("abcdef"), false));
    CHECK(w.verifyAuthKey(prefix + kPayload24, false));
    CHECK_FALSE(w.verifyAuthKey(prefix + kPayload24.left(23), false));
}

TEST_CASE("A deep link's token is taken only when it is well formed",
          "[imagewriter][connect]")
{
    // The path with no user in front of it: the browser hands the
    // application a URL and whatever is in auth_key would otherwise go
    // straight into the image.
    ImageWriter w(nullptr);
    const QString good = QStringLiteral("rpuak_") + kPayload24;

    rpi_test::SignalLog received(&w, &ImageWriter::connectTokenReceived);

    SECTION("a good token is accepted and reported") {
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=") + good));

        REQUIRE(received.count() == 1);
        CHECK(received.at(0).at(0).toString() == good);
    }

    SECTION("other query parameters do not confuse it") {
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?state=x&auth_key=")
                                 + good + QStringLiteral("&next=y")));

        REQUIRE(received.count() == 1);
        CHECK(received.at(0).at(0).toString() == good);
    }

    SECTION("a malformed token is dropped rather than passed on") {
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=nonsense")));
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=rpuak_short")));
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=")));
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect")));

        CHECK(received.count() == 0);
    }
}

TEST_CASE("A second deep link does not silently replace the token in use",
          "[imagewriter][connect]")
{
    // Two links can arrive: a stale browser tab, or a second sign-in. The
    // token already held is what the user configured this image with, so a
    // different one is raised as a conflict for the UI to resolve rather
    // than swapped in underneath them.
    ImageWriter w(nullptr);
    const QString first = QStringLiteral("rpuak_") + kPayload24;
    const QString second = QStringLiteral("rpuak_") + QStringLiteral("zyxwvutsrqpnmkjihgfedcba");

    rpi_test::SignalLog received(&w, &ImageWriter::connectTokenReceived);
    rpi_test::SignalLog conflicts(&w, &ImageWriter::connectTokenConflictDetected);

    w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=") + first));
    REQUIRE(received.count() == 1);

    SECTION("a different token is reported as a conflict, not adopted") {
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=") + second));

        REQUIRE(conflicts.count() == 1);
        CHECK(conflicts.at(0).at(0).toString() == second);
        CHECK(received.count() == 1);
        // The stronger statement: what the image would be built with is
        // still the token the user set it up with.
        CHECK(w.getRuntimeConnectToken() == first);
    }

    SECTION("the same token again is not a conflict") {
        // A link opened twice is not a disagreement about anything.
        w.handleIncomingUrl(QUrl(QStringLiteral("rpi-imager://connect?auth_key=") + first));

        CHECK(conflicts.count() == 0);
        CHECK(received.count() == 1);
        CHECK(w.getRuntimeConnectToken() == first);
    }
}

TEST_CASE("A deep link's repository is taken only when it is well formed",
          "[imagewriter][connect]")
{
    // This is the path the anchored \\A..\\z pattern exists for. A URL
    // typed into the repository dialog is trimmed by the field before it is
    // checked, so a trailing newline never reaches the validator there --
    // here nothing trims, and issue #1687 was a %0A-suffixed URL reaching
    // the fetch.
    ImageWriter w(nullptr);
    rpi_test::SignalLog repos(&w, &ImageWriter::repositoryUrlReceived);

    SECTION("a good repository comes through") {
        w.handleIncomingUrl(QUrl(QStringLiteral(
            "rpi-imager://open?repo=https://example.com/os_list.json")));

        REQUIRE(repos.count() == 1);
        CHECK(repos.at(0).at(0).toString()
              == QStringLiteral("https://example.com/os_list.json"));
    }

    SECTION("a newline-suffixed repository is ignored") {
        w.handleIncomingUrl(QUrl(QStringLiteral(
            "rpi-imager://open?repo=https://example.com/os_list.json%0A")));

        CHECK(repos.count() == 0);
    }

    SECTION("a repository that is not a list is ignored") {
        w.handleIncomingUrl(QUrl(QStringLiteral(
            "rpi-imager://open?repo=file:///etc/passwd.json")));
        w.handleIncomingUrl(QUrl(QStringLiteral(
            "rpi-imager://open?repo=https://example.com/evil?x=.json")));

        CHECK(repos.count() == 0);
    }
}

// ══════════════════════════════════════════════════════════════
// The guard that stops a script erasing the machine it runs on
//
// In CLI mode the destination is whatever the operator typed, and there is no
// list to pick from and no confirmation to read. So before writing, the
// destination is looked up in the drive list and refused if it is not there --
// unless --enable-writing-system-drives is passed, which says the operator
// meant it.
//
// Get that comparison wrong and `rpi-imager --cli image.img /dev/sda` from a
// cron job writes over the system disk. Neither the check nor the list of
// alternatives it prints was covered; both are static and take the model by
// reference, so a model populated by hand reaches them without any hardware.
// ══════════════════════════════════════════════════════════════

namespace {

Drivelist::DeviceDescriptor removableDrive(const std::string &device,
                                           const std::string &description,
                                           uint64_t size = 32000000000ULL)
{
    Drivelist::DeviceDescriptor d;
    d.device = device;
    d.raw = device;
    d.description = description;
    d.size = size;
    d.isRemovable = true;
    d.isUSB = true;
    d.isSystem = false;
    d.isReadOnly = false;
    return d;
}

// The model as the CLI builds it: constructed, then handed a device list.
struct PopulatedDriveList
{
    DriveListModel model;

    explicit PopulatedDriveList(std::vector<Drivelist::DeviceDescriptor> drives)
    {
        model.processDriveList(std::move(drives));
    }
};

} // namespace

TEST_CASE("A destination in the drive list is accepted", "[cli][destination]")
{
    PopulatedDriveList drives({removableDrive("/dev/sdz", "Generic Mass-Storage")});

    CHECK(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sdz")));
}

TEST_CASE("A destination that is not in the drive list is refused",
          "[cli][destination]")
{
    // The whole point. /dev/sda is a plausible thing to type and a plausible
    // system disk, and nothing else stands between it and the write.
    PopulatedDriveList drives({removableDrive("/dev/sdz", "Generic Mass-Storage")});

    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sda")));
}

TEST_CASE("A partition of a listed drive is not itself a listed drive",
          "[cli][destination]")
{
    // The comparison is on the whole device path, so /dev/sdz1 is refused even
    // though /dev/sdz is offered. Writing an image to a partition rather than
    // the disk produces a card that will not boot, and a prefix match here
    // would let it through.
    PopulatedDriveList drives({removableDrive("/dev/sdz", "Generic Mass-Storage")});

    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sdz1")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sd")));
}

// ── The drives the flag is named after ────────────────────────────────
//
// The drive list drops what is mounted at "/" and nothing else, so a disk
// carrying /home, /boot, /usr or /var is on it, flagged isSystem. The storage
// picker demands such a drive's name be typed before it will touch one. The
// CLI has --enable-writing-system-drives, and its refusal already tells the
// operator to reach for it -- so without the flag those drives must not be
// accepted, or the flag means nothing and a script writes over somebody's
// home partition without being asked twice.

namespace {

Drivelist::DeviceDescriptor systemDrive(const std::string &device,
                                        const std::string &description)
{
    Drivelist::DeviceDescriptor d = removableDrive(device, description);
    d.isSystem = true;
    return d;
}

} // namespace

TEST_CASE("A drive carrying system files is not a destination on its own",
          "[cli][destination][system]")
{
    PopulatedDriveList drives({systemDrive("/dev/sda", "Disk holding /home")});
    REQUIRE(drives.model.rowCount(QModelIndex()) == 1);

    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sda")));
}

TEST_CASE("A system drive is not offered as somewhere to write instead",
          "[cli][destination][system]")
{
    // The refusal prints the alternatives. Listing a drive it would itself
    // refuse would send the operator straight back into the same wall -- or
    // worse, persuade them it was a safe choice.
    PopulatedDriveList drives({systemDrive("/dev/sda", "Disk holding /home"),
                               removableDrive("/dev/sdz", "SanDisk Cruzer")});

    const QStringList offered = Cli::removableDestinations(drives.model);
    INFO("offered: " << offered.join(QStringLiteral(" | ")).toStdString());
    CHECK(offered.size() == 1);
    CHECK(offered.first().startsWith(QStringLiteral("/dev/sdz")));
}

TEST_CASE("An ordinary card next to a system drive is still accepted",
          "[cli][destination][system]")
{
    // The refusal has to be about the one drive, not about there being a
    // system drive plugged in at all.
    PopulatedDriveList drives({systemDrive("/dev/sda", "Disk holding /home"),
                               removableDrive("/dev/sdz", "SanDisk Cruzer")});

    CHECK(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sdz")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sda")));
}

TEST_CASE("With no drives to write to, every destination is refused",
          "[cli][destination]")
{
    // An empty list is what a machine with nothing plugged in reports. The
    // answer has to be no rather than "nothing said no".
    PopulatedDriveList drives({});

    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QStringLiteral("/dev/sdz")));
    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QString()));
}

TEST_CASE("An empty destination is refused", "[cli][destination]")
{
    PopulatedDriveList drives({removableDrive("/dev/sdz", "Generic Mass-Storage")});

    CHECK_FALSE(Cli::destinationIsRemovable(drives.model, QString()));
}

TEST_CASE("The refusal names every drive that could be written to instead",
          "[cli][destination]")
{
    // This list is the operator's only way to find out what they should have
    // typed, so it has to carry both the path to type and enough description
    // to tell two cards apart.
    PopulatedDriveList drives({
        removableDrive("/dev/sdy", "SanDisk Ultra"),
        removableDrive("/dev/sdz", "Generic Mass-Storage")
    });

    const QStringList choices = Cli::removableDestinations(drives.model);

    REQUIRE(choices.size() == 2);
    const QString joined = choices.join(QStringLiteral("\n"));
    INFO("choices:\n" << joined.toStdString());
    CHECK_THAT(joined.toStdString(), Catch::Matchers::ContainsSubstring("/dev/sdy"));
    CHECK_THAT(joined.toStdString(), Catch::Matchers::ContainsSubstring("/dev/sdz"));
    CHECK_THAT(joined.toStdString(), Catch::Matchers::ContainsSubstring("SanDisk Ultra"));
    CHECK_THAT(joined.toStdString(),
               Catch::Matchers::ContainsSubstring("Generic Mass-Storage"));
}

TEST_CASE("With nothing plugged in the refusal offers nothing",
          "[cli][destination]")
{
    // Better an empty list than a stale one: the operator needs to know there
    // is nothing to write to, not be given a device that has gone.
    PopulatedDriveList drives({});

    CHECK(Cli::removableDestinations(drives.model).isEmpty());
}

// ══════════════════════════════════════════════════════════════
// What the drive list refuses to offer
//
// The storage list is where the user picks the thing that is about to be
// erased, and the model decides what appears in it. The QML side has a filter
// and a typed confirmation for system drives, both tested there -- but they can
// only act on what the model puts in front of them, and the model's own
// refusals were untested.
//
// The last line of defence is here: a disk mounted at / never reaches the list
// at all, whatever else is or is not set on it.
// ══════════════════════════════════════════════════════════════

namespace {

QStringList devicesOffered(DriveListModel &model)
{
    QStringList out;
    const int n = model.rowCount(QModelIndex());
    for (int i = 0; i < n; i++)
        out << model.index(i, 0).data(DriveListModel::deviceRole).toString();
    return out;
}

bool offeredAsSystemDrive(DriveListModel &model, const QString &device)
{
    const int n = model.rowCount(QModelIndex());
    for (int i = 0; i < n; i++) {
        const QModelIndex idx = model.index(i, 0);
        if (idx.data(DriveListModel::deviceRole).toString() == device)
            return idx.data(DriveListModel::isSystemRole).toBool();
    }
    return false;
}

} // namespace

TEST_CASE("The disk the system is running from is never offered",
          "[drivelist][safety]")
{
    // Checked on the mountpoint rather than on the isSystem flag, so it holds
    // even when whatever sets that flag has missed. Nothing downstream gets a
    // chance to offer it: not the filter, not the confirmation.
    Drivelist::DeviceDescriptor root = removableDrive("/dev/sda", "System disk");
    root.mountpoints = {"/"};
    root.isSystem = false;   // deliberately not set: the mountpoint decides

    DriveListModel model;
    model.processDriveList({root, removableDrive("/dev/sdz", "A card")});

    const QStringList offered = devicesOffered(model);
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(offered.contains(QStringLiteral("/dev/sda")));
    CHECK(offered.contains(QStringLiteral("/dev/sdz")));
}

TEST_CASE("A card reader with no card in it is not offered",
          "[drivelist][safety]")
{
    // An empty reader reports a size of zero. Offering it lets the user pick a
    // target that cannot be written, and the failure comes later and reads as
    // a broken card.
    Drivelist::DeviceDescriptor empty = removableDrive("/dev/sdy", "Card reader", 0);

    DriveListModel model;
    model.processDriveList({empty, removableDrive("/dev/sdz", "A card")});

    const QStringList offered = devicesOffered(model);
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(offered.contains(QStringLiteral("/dev/sdy")));
    CHECK(offered.contains(QStringLiteral("/dev/sdz")));
}

TEST_CASE("A read-only virtual device is not offered", "[drivelist][safety]")
{
    // A mounted ISO or a read-only loop device cannot be written and is not
    // something anyone means to image.
    Drivelist::DeviceDescriptor iso = removableDrive("/dev/loop9", "Mounted image");
    iso.isVirtual = true;
    iso.isReadOnly = true;

    DriveListModel model;
    model.processDriveList({iso, removableDrive("/dev/sdz", "A card")});

    CHECK_FALSE(devicesOffered(model).contains(QStringLiteral("/dev/loop9")));
}

TEST_CASE("A virtual device that is a system device is not offered",
          "[drivelist][safety]")
{
    Drivelist::DeviceDescriptor vol = removableDrive("/dev/loop8", "System volume");
    vol.isVirtual = true;
    vol.isSystem = true;

    DriveListModel model;
    model.processDriveList({vol, removableDrive("/dev/sdz", "A card")});

    CHECK_FALSE(devicesOffered(model).contains(QStringLiteral("/dev/loop8")));
}

TEST_CASE("A writable loop device is offered, but as a system drive",
          "[drivelist][safety]")
{
    // Loop devices are how an image gets written to a file, so they are kept
    // -- and the comment in the model says why they cannot be required to be
    // removable: losetup never marks them so, and requiring it would hide
    // them.
    //
    // They are flagged as system drives even when they are not, which is what
    // makes the QML side ask for the device's name to be typed before
    // selecting one. Being offered without that flag is the dangerous half of
    // this, so both are checked.
    Drivelist::DeviceDescriptor loop = removableDrive("/dev/loop7", "Disk image");
    loop.isVirtual = true;
    loop.isReadOnly = false;
    loop.isSystem = false;
    loop.isRemovable = false;   // as losetup reports it

    DriveListModel model;
    model.processDriveList({loop});

    const QStringList offered = devicesOffered(model);
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
#ifdef Q_OS_LINUX
    REQUIRE(offered.contains(QStringLiteral("/dev/loop7")));
    CHECK(offeredAsSystemDrive(model, QStringLiteral("/dev/loop7")));
#else
    // Off Linux the model requires a virtual device to be removable, because
    // isSystem alone misses APFS volumes on non-ejectable enclosures and
    // Storage Spaces pools. So this one is hidden rather than offered, which
    // is the safe half of the same decision.
    CHECK_FALSE(offered.contains(QStringLiteral("/dev/loop7")));
#endif
}

TEST_CASE("An ordinary card is not flagged as a system drive",
          "[drivelist][safety]")
{
    // The other side of the case above: if everything were flagged, the typed
    // confirmation would be asked for on every write and stop meaning
    // anything.
    DriveListModel model;
    model.processDriveList({removableDrive("/dev/sdz", "A card")});

    REQUIRE(devicesOffered(model).contains(QStringLiteral("/dev/sdz")));
    CHECK_FALSE(offeredAsSystemDrive(model, QStringLiteral("/dev/sdz")));
}

TEST_CASE("A drive reported as a system drive is offered and flagged",
          "[drivelist][safety]")
{
    // Not hidden -- the user may genuinely mean to write to an internal disk,
    // and the filter on the QML side is what hides these by default -- but it
    // has to arrive flagged, or the filter has nothing to act on.
    Drivelist::DeviceDescriptor internal = removableDrive("/dev/sdb", "Internal SSD");
    internal.isSystem = true;
    internal.isRemovable = false;

    DriveListModel model;
    model.processDriveList({internal});

    REQUIRE(devicesOffered(model).contains(QStringLiteral("/dev/sdb")));
    CHECK(offeredAsSystemDrive(model, QStringLiteral("/dev/sdb")));
}

// ══════════════════════════════════════════════════════════════
// Registering a written device with a Raspberry Pi Connect organisation.
//
// _configureAndStartFastbootFlash decides, from settings alone, whether the
// device about to be written should be enrolled with an organisation and
// under which credentials. The whole function starts a thread, so none of it
// had a test; the decision is now separable.
//
// The interesting case is the one nobody thinks about: turning the feature
// off in App Options does not erase the stored key.
// ══════════════════════════════════════════════════════════════

namespace {
class OrgRegistrationWriter : public ImageWriter
{
public:
    OrgRegistrationWriter() : ImageWriter(nullptr) {}
    using ImageWriter::_connectOrgRegistrationForWrite;
};
} // namespace

TEST_CASE("An organisation write carries the key and the description",
          "[imagewriter][connectorg]")
{
    OrgRegistrationWriter w;
    w.clearConnectOrgRegistration();
    w.setSetting(QStringLiteral("connect_org_enabled"), true);
    w.setConnectOrgRegistration(QStringLiteral("rpi-org-key-123"),
                                QStringLiteral("Bench 4"));
    REQUIRE(w.hasConnectOrgRegistration());

    QString key, desc;
    CHECK(w._connectOrgRegistrationForWrite(key, desc));
    CHECK(key == QStringLiteral("rpi-org-key-123"));
    CHECK(desc == QStringLiteral("Bench 4"));

    // The same key read the way the UI reads settings comes back empty: it is
    // a persisted secret getStringSetting refuses to hand out. The write path
    // has to go to the setting directly, and this is what says so -- routing
    // it through getStringSetting would send an empty key and every device
    // would fail to register.
    CHECK(w.getStringSetting(QStringLiteral("connect_org_api_key")).isEmpty());

    w.clearConnectOrgRegistration();
    w.setSetting(QStringLiteral("connect_org_enabled"), false);
}

TEST_CASE("Turning organisation mode off stops devices being enrolled",
          "[imagewriter][connectorg]")
{
    OrgRegistrationWriter w;
    w.clearConnectOrgRegistration();

    // Someone sets up organisation mode, writes some cards, then turns the
    // feature off in App Options. The key stays in settings -- switching the
    // checkbox off does not clear it, and clearing it is a separate button.
    w.setSetting(QStringLiteral("connect_org_enabled"), true);
    w.setConnectOrgRegistration(QStringLiteral("rpi-org-key-123"),
                                QStringLiteral("Bench 4"));
    REQUIRE(w.hasConnectOrgRegistration());

    w.setSetting(QStringLiteral("connect_org_enabled"), false);

    // The next card must not be enrolled with that organisation. Whoever is
    // writing it did not ask for it, may not be the same person, and would
    // have no way of knowing it had happened.
    QString key, desc;
    CHECK_FALSE(w._connectOrgRegistrationForWrite(key, desc));
    CHECK(key.isEmpty());
    CHECK(desc.isEmpty());

    // Still stored, so turning the feature back on works without re-entering
    // it -- the point is that it is not used meanwhile.
    CHECK(w.hasConnectOrgRegistration());

    w.clearConnectOrgRegistration();
}

TEST_CASE("Organisation mode with nothing to register under registers nothing",
          "[imagewriter][connectorg]")
{
    OrgRegistrationWriter w;
    w.clearConnectOrgRegistration();
    w.setSetting(QStringLiteral("connect_org_enabled"), true);
    REQUIRE_FALSE(w.hasConnectOrgRegistration());

    // The feature is on but no key was ever entered, or it was cleared. The
    // write goes ahead unregistered rather than handing the flash thread an
    // empty key to fail on partway through.
    QString key, desc;
    CHECK_FALSE(w._connectOrgRegistrationForWrite(key, desc));
    CHECK(key.isEmpty());

    w.setSetting(QStringLiteral("connect_org_enabled"), false);
}

TEST_CASE("A registration with no description prefix still goes ahead",
          "[imagewriter][connectorg]")
{
    OrgRegistrationWriter w;
    w.clearConnectOrgRegistration();
    w.setSetting(QStringLiteral("connect_org_enabled"), true);
    w.setConnectOrgRegistration(QStringLiteral("rpi-org-key-123"), QString());
    REQUIRE(w.hasConnectOrgRegistration());

    // The prefix names the device on the Connect dashboard and is optional.
    // An empty one is not a reason to skip registration.
    QString key, desc;
    CHECK(w._connectOrgRegistrationForWrite(key, desc));
    CHECK(key == QStringLiteral("rpi-org-key-123"));
    CHECK(desc.isEmpty());

    w.clearConnectOrgRegistration();
    w.setSetting(QStringLiteral("connect_org_enabled"), false);
}

// ══════════════════════════════════════════════════════════════
// Restarting a write that has stalled.
//
// WriteProgressWatchdog watches for a write making no progress. Async I/O on
// some card readers and USB bridges stops returning completions altogether;
// the watchdog notices, and restartWrite() abandons the attempt and starts
// again with synchronous I/O, which those devices do handle.
//
// It is a protected slot reached only from a watchdog signal, so it had no
// test. Two things have to happen or the recovery makes things worse: the
// retry has to actually be in sync mode, and the user has to be told, because
// from the outside a restart looks like the progress bar jumping back to zero
// for no reason.
// ══════════════════════════════════════════════════════════════

namespace {
class RestartableWriter : public ImageWriter
{
public:
    RestartableWriter() : ImageWriter(nullptr) {}
    using ImageWriter::restartWrite;
    using ImageWriter::_forceSyncMode;
};
} // namespace

TEST_CASE("A stalled write is restarted in synchronous mode",
          "[imagewriter][restart]")
{
    RestartableWriter w;
    REQUIRE_FALSE(w._forceSyncMode);

    QStringList warnings;
    QObject::connect(&w, &ImageWriter::operationWarning,
                     [&warnings](QVariant m) { warnings << m.toString(); });
    UiLog log(&w);

    const QString reason =
        QStringLiteral("The storage device stopped responding to asynchronous writes.");
    w.restartWrite(reason);

    // Without this the retry uses the same async path that just stalled, and
    // stalls again -- a loop the user cannot get out of except by cancelling.
    CHECK(w._forceSyncMode);

    // And they are told, in the watchdog's own words rather than a generic
    // "restarting": a write that silently begins again looks like a fault.
    REQUIRE(warnings.size() == 1);
    CHECK(warnings.first() == reason);
}

TEST_CASE("The restart goes on to start a write", "[imagewriter][restart]")
{
    // With no thread running there is nothing to wait for, so the new write
    // starts immediately. Nothing is selected here, so what comes back is the
    // ordinary refusal -- which is the evidence that startWrite() was reached
    // rather than the restart quietly ending after setting a flag.
    RestartableWriter w;
    UiLog log(&w);

    w.restartWrite(QStringLiteral("stalled"));

    REQUIRE(log.errors.size() == 1);
    INFO("reported: " << log.errors[0].toStdString());
    CHECK_THAT(log.errors[0].toStdString(), ContainsSubstring("Cannot start write"));
}

TEST_CASE("Sync mode stays on for the rest of the session",
          "[imagewriter][restart]")
{
    // _forceSyncMode is never cleared: once a device has shown it cannot keep
    // up with async I/O, every later write in the same session uses sync too,
    // including a write of a different image to a different card.
    //
    // Recording the behaviour rather than judging it. The cost is throughput
    // on a device that was fine; the alternative is stalling again on the one
    // that was not, and the flag is gone the next time Imager is launched.
    RestartableWriter w;
    UiLog log(&w);

    w.restartWrite(QStringLiteral("first stall"));
    REQUIRE(w._forceSyncMode);

    w.setDst(QStringLiteral("/dev/null"), 4ull * 1024 * 1024 * 1024);
    CHECK(w._forceSyncMode);
}

// ══════════════════════════════════════════════════════════════
// Whether a toggle in Interfaces & Features is offered at all.
//
// checkHWAndSWCapability is called ten times from the wizard. Five of those
// decide, between them, whether the Interfaces & Features step appears; the
// rest decide which individual toggles are enabled on it. Both halves have
// to agree: the board has to have the interface, and the operating system
// has to be able to switch it on.
//
// Answering false wrongly loses the user a customisation option with no
// explanation -- the step is simply not there. Answering true wrongly offers
// a toggle that cannot take effect, which is worse: the setting appears to
// have been applied and silently is not.
//
// It had no test of its own, despite being the only caller of the two
// single-sided checks that anything in the wizard actually uses.
// ══════════════════════════════════════════════════════════════

TEST_CASE("An interface needs both the board and the image to support it",
          "[imagewriter][capability]")
{
    ImageWriter w(nullptr);

    // A Pi 5 offering i2c and spi in hardware.
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("i2c"), QStringLiteral("spi")});

    SECTION("both sides agree, so the toggle is offered")
    {
        w.setSWCapabilitiesList(QStringLiteral("[\"i2c\",\"spi\"]"));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("i2c")));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("spi")));
    }

    SECTION("the board has it but this image cannot switch it on")
    {
        // A minimal or third-party image without raspi-config's handling.
        // Offering the toggle would write a setting nothing acts on.
        w.setSWCapabilitiesList(QStringLiteral("[\"spi\"]"));
        CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("i2c")));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("spi")));
    }

    SECTION("the image supports it but this board has no such interface")
    {
        w.setSWCapabilitiesList(QStringLiteral("[\"i2c\",\"onewire\"]"));
        CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("onewire")));
    }

    SECTION("neither side has it")
    {
        w.setSWCapabilitiesList(QStringLiteral("[\"spi\"]"));
        CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("usb_otg")));
    }
}

TEST_CASE("A capability named differently on each side is matched on both",
          "[imagewriter][capability]")
{
    // The second argument exists for the case where the hardware tag and the
    // software tag are not the same word. Given one, the hardware list is
    // searched for the first and the software list for the second -- not the
    // first for both, which would refuse every such pair.
    ImageWriter w(nullptr);
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("usb_otg")});
    w.setSWCapabilitiesList(QStringLiteral("[\"usb_gadget\"]"));

    CHECK(w.checkHWAndSWCapability(QStringLiteral("usb_otg"),
                                   QStringLiteral("usb_gadget")));

    // And it is genuinely two different lookups: neither name works for both.
    CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("usb_otg")));
    CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("usb_gadget")));

    // An empty second argument means "the same on both sides", which is how
    // all ten calls in the wizard use it.
    w.setSWCapabilitiesList(QStringLiteral("[\"usb_otg\"]"));
    CHECK(w.checkHWAndSWCapability(QStringLiteral("usb_otg"), QString()));
}

TEST_CASE("Capability lists are read whichever way the repository writes them",
          "[imagewriter][capability]")
{
    // The software list arrives as whatever the OS list JSON put in the
    // image's "capabilities" field, and both shapes are in use.
    ImageWriter w(nullptr);
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("i2c"), QStringLiteral("spi")});

    SECTION("a JSON array")
    {
        w.setSWCapabilitiesList(QStringLiteral("[\"i2c\", \"spi\"]"));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("i2c")));
    }

    SECTION("a comma-separated string, which is not JSON at all")
    {
        w.setSWCapabilitiesList(QStringLiteral("i2c,spi"));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("i2c")));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("spi")));
    }

    SECTION("a list handed straight over from QML")
    {
        w.setSWCapabilitiesList(QVariantList{QStringLiteral("i2c"), QStringLiteral("spi")});
        CHECK(w.checkHWAndSWCapability(QStringLiteral("i2c")));
    }

    SECTION("with stray spacing and capitals in either list")
    {
        // The three setters do not agree on normalising, which is why the
        // comparison does it. A single stray space in a repository file used
        // to remove the option from the wizard.
        w.setHWCapabilitiesList(QJsonArray{QStringLiteral(" I2C ")});
        w.setSWCapabilitiesList(QStringLiteral("[\" i2c \"]"));
        CHECK(w.checkHWAndSWCapability(QStringLiteral("i2c")));
    }
}

TEST_CASE("An empty software list offers nothing", "[imagewriter][capability]")
{
    // What the wizard sets when the selection is cleared. Every toggle has to
    // go with it, or the previous image's capabilities stay on screen.
    ImageWriter w(nullptr);
    w.setHWCapabilitiesList(QJsonArray{QStringLiteral("i2c"), QStringLiteral("spi")});
    w.setSWCapabilitiesList(QStringLiteral("[]"));

    CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("i2c")));
    CHECK_FALSE(w.checkHWAndSWCapability(QStringLiteral("spi")));
}

// ══════════════════════════════════════════════════════════════
// Carrying a Wi-Fi network name through to the image.
//
// The wizard hands the SSID to wifiSsidOctetsBase64 and stores the result;
// CustomisationGenerator decodes it back and writes those octets into the
// image's network configuration. The consumer side has a test. The producer
// -- the only thing that puts the value there -- did not.
//
// An SSID is a sequence of octets, not text: the standard does not say what
// encoding a network name is in, and routers ship with accented and CJK
// names out of the box. Base64 is used precisely so the exact bytes survive
// QSettings and the QML boundary. Get the encoding wrong and the Pi looks
// for a network with a different name, finds nothing, and boots with no
// connection and nothing on screen to say why.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A network name survives the trip to the image byte for byte",
          "[imagewriter][wifi]")
{
    ImageWriter w(nullptr);

    auto roundTrip = [&w](const QString& ssid) {
        return QByteArray::fromBase64(w.wifiSsidOctetsBase64(ssid).toLatin1());
    };

    SECTION("plain ASCII")
    {
        CHECK(roundTrip(QStringLiteral("HomeNetwork")) == QByteArray("HomeNetwork"));
    }

    SECTION("accented characters, which are encoded as UTF-8 and not Latin-1")
    {
        // Latin-1 would give one byte for the e-acute rather than two, and
        // the router would never answer to it.
        const QString ssid = QString::fromUtf8("Café");
        CHECK(roundTrip(ssid) == QByteArray("Caf\xC3\xA9"));
        CHECK(roundTrip(ssid) == ssid.toUtf8());
    }

    SECTION("characters Latin-1 cannot represent at all")
    {
        // A default SSID from a router sold in Japan. toLatin1() turns each
        // of these into '?', which is not a recovery -- it is a different
        // network name that happens to be the same length.
        const QString ssid = QString::fromUtf8("無線LAN");
        CHECK(roundTrip(ssid) == ssid.toUtf8());
        CHECK_FALSE(roundTrip(ssid).contains('?'));
    }

    SECTION("an emoji, which needs a surrogate pair")
    {
        const QString ssid = QString::fromUtf8("Pi \xF0\x9F\x93\xB6");
        CHECK(roundTrip(ssid) == ssid.toUtf8());
    }
}

TEST_CASE("Spacing in a network name is part of the name",
          "[imagewriter][wifi]")
{
    // SSIDs with a trailing space exist, and are a well-known way to get
    // caught out. Trimming here would produce a name that looks right in the
    // wizard and matches nothing on the air.
    ImageWriter w(nullptr);

    const QString padded = QStringLiteral("  Guest Wi-Fi  ");
    const QByteArray decoded =
        QByteArray::fromBase64(w.wifiSsidOctetsBase64(padded).toLatin1());

    CHECK(decoded == QByteArray("  Guest Wi-Fi  "));
    CHECK(decoded.size() == padded.toUtf8().size());
}

TEST_CASE("An empty network name encodes to nothing", "[imagewriter][wifi]")
{
    // Reached when the user clears the field. An empty result is what tells
    // the generator there is no SSID, rather than the base64 of an empty
    // string being written into the config as a name.
    ImageWriter w(nullptr);
    CHECK(w.wifiSsidOctetsBase64(QString()).isEmpty());
    CHECK(w.wifiSsidOctetsBase64(QStringLiteral("")).isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Throwing away a Connect key that was minted for a different device.
//
// In organisation mode the wizard mints a single-use Raspberry Pi Connect
// auth key over the organisation API, for the image and the card the user
// has chosen. Change either afterwards and that key no longer belongs to
// what is about to be written -- so the four places in the wizard where the
// OS or the storage can change all call discardOrgMintedConnectToken.
//
// Two failures either side of it. Not discarding writes a key intended for
// one device into another image, burning a single-use key and enrolling the
// wrong thing. Discarding too eagerly throws away a key the user typed in
// themselves, which they would have to go and fetch again -- and the
// difference between the two is one flag nothing tested.
// ══════════════════════════════════════════════════════════════

namespace {
class ConnectTokenWriter : public ImageWriter
{
public:
    ConnectTokenWriter() : ImageWriter(nullptr) {}
    using ImageWriter::_piConnectToken;
    using ImageWriter::_piConnectTokenIsOrgMinted;

    // What requestOrgAuthKey leaves behind on success, without the network.
    void pretendOrgKeyWasMinted(const QString& secret)
    {
        _piConnectToken = secret;
        _piConnectTokenIsOrgMinted = true;
    }
};
} // namespace

TEST_CASE("Changing the card throws away a key minted for the old one",
          "[imagewriter][connecttoken]")
{
    ConnectTokenWriter w;
    int cleared = 0;
    QObject::connect(&w, &ImageWriter::connectTokenCleared,
                     [&cleared]() { ++cleared; });

    w.pretendOrgKeyWasMinted(QStringLiteral("rpoak_2xVQqQ7mR4bT9pLzYnKwCdHf"));
    REQUIRE_FALSE(w.getRuntimeConnectToken().isEmpty());

    w.discardOrgMintedConnectToken();

    CHECK(w.getRuntimeConnectToken().isEmpty());
    // The signal is how the wizard knows to take the token off the screen.
    // Clearing the value without saying so leaves the UI showing a key that
    // is no longer there.
    CHECK(cleared == 1);
}

TEST_CASE("A key the user supplied themselves is left alone",
          "[imagewriter][connecttoken]")
{
    // Typed in, pasted, or arrived through the browser callback. It is not
    // tied to a particular card, and the user would have to go back to the
    // Connect site to get another one.
    ConnectTokenWriter w;
    int cleared = 0;
    QObject::connect(&w, &ImageWriter::connectTokenCleared,
                     [&cleared]() { ++cleared; });

    const QString typed = QStringLiteral("rpuak_2xVQqQ7mR4bT9pLzYnKwCdHf");
    w.overwriteConnectToken(typed);
    REQUIRE(w.getRuntimeConnectToken() == typed);

    w.discardOrgMintedConnectToken();

    CHECK(w.getRuntimeConnectToken() == typed);
    CHECK(cleared == 0);

    // The same again, but arriving after a mint rather than on a fresh
    // writer: the user asked for an organisation key and then pasted one of
    // their own over it. Supplying a token has to take the org-minted flag
    // down with it, or the next change of card silently discards the key
    // they just typed. Starting from a fresh writer would not show this --
    // the flag is false there to begin with.
    ConnectTokenWriter w2;
    int cleared2 = 0;
    QObject::connect(&w2, &ImageWriter::connectTokenCleared,
                     [&cleared2]() { ++cleared2; });

    w2.pretendOrgKeyWasMinted(QStringLiteral("rpoak_2xVQqQ7mR4bT9pLzYnKwCdHf"));
    w2.overwriteConnectToken(typed);
    REQUIRE(w2.getRuntimeConnectToken() == typed);

    w2.discardOrgMintedConnectToken();
    CHECK(w2.getRuntimeConnectToken() == typed);
    CHECK(cleared2 == 0);
}

TEST_CASE("Discarding when there is nothing to discard says nothing",
          "[imagewriter][connecttoken]")
{
    // The wizard calls this on every change of OS or storage, most of which
    // happen with no token at all. A signal each time would have the UI
    // reacting to a clear that never occurred.
    ConnectTokenWriter w;
    int cleared = 0;
    QObject::connect(&w, &ImageWriter::connectTokenCleared,
                     [&cleared]() { ++cleared; });

    w.discardOrgMintedConnectToken();
    CHECK(cleared == 0);
    CHECK(w.getRuntimeConnectToken().isEmpty());

    // And twice over, once a real one has gone: the second change of storage
    // in a row must not announce another clear.
    w.pretendOrgKeyWasMinted(QStringLiteral("rpoak_2xVQqQ7mR4bT9pLzYnKwCdHf"));
    w.discardOrgMintedConnectToken();
    REQUIRE(cleared == 1);
    w.discardOrgMintedConnectToken();
    CHECK(cleared == 1);
}

TEST_CASE("Clearing the token clears either kind", "[imagewriter][connecttoken]")
{
    // The explicit control, as opposed to the automatic discard. Asked to
    // remove the key, it removes whichever key is there -- and takes the
    // org-minted flag with it, so a later discard has nothing to act on.
    ConnectTokenWriter w;

    w.overwriteConnectToken(QStringLiteral("rpuak_2xVQqQ7mR4bT9pLzYnKwCdHf"));
    w.clearConnectToken();
    CHECK(w.getRuntimeConnectToken().isEmpty());

    w.pretendOrgKeyWasMinted(QStringLiteral("rpoak_2xVQqQ7mR4bT9pLzYnKwCdHf"));
    w.clearConnectToken();
    CHECK(w.getRuntimeConnectToken().isEmpty());
    CHECK_FALSE(w._piConnectTokenIsOrgMinted);
}

// ══════════════════════════════════════════════════════════════
// The settings that have a default other than false.
//
// getBoolSetting reads QSettings, and an unset key would ordinarily come
// back false. Three keys are given a real default instead, and each of them
// controls something the user notices: whether the card is ejected when the
// write finishes, whether Imager looks for a new version, and whether it
// reports anything back.
//
// Both directions matter. Losing the default turns the behaviour off for
// everybody who has never touched the setting -- which is nearly everybody,
// since the point of a default is that it is not configured. Letting the
// default win over a stored value ignores the person who did go and change
// it, and for telemetry that is a privacy question rather than a
// convenience one.
// ══════════════════════════════════════════════════════════════

namespace {
// getBoolSetting's defaults only apply to a key that has never been written,
// and the settings file outlives the test binary. Clear it first.
void forgetSetting(const QString& key)
{
    QSettings settings;
    settings.remove(key);
    settings.sync();
}
} // namespace

TEST_CASE("A card is ejected after writing unless told otherwise",
          "[imagewriter][settings][defaults]")
{
    // Nobody sets this; it has to be on by default. A user who pulls a card
    // that was never ejected can lose the write they just waited for.
    forgetSetting(QStringLiteral("eject"));
    ImageWriter w(nullptr);
    CHECK(w.getBoolSetting(QStringLiteral("eject")));

    SECTION("and turning it off is respected")
    {
        // The stored value wins over the default. Somebody who writes many
        // cards in a row turns this off deliberately.
        w.setSetting(QStringLiteral("eject"), false);
        CHECK_FALSE(w.getBoolSetting(QStringLiteral("eject")));
    }

    SECTION("and turning it back on is too")
    {
        w.setSetting(QStringLiteral("eject"), false);
        REQUIRE_FALSE(w.getBoolSetting(QStringLiteral("eject")));
        w.setSetting(QStringLiteral("eject"), true);
        CHECK(w.getBoolSetting(QStringLiteral("eject")));
    }

    forgetSetting(QStringLiteral("eject"));
}

TEST_CASE("The build's telemetry and update-check defaults are what is used",
          "[imagewriter][settings][defaults]")
{
    // Both are decided at build time -- ENABLE_TELEMETRY and
    // ENABLE_CHECK_VERSION -- and a distribution packaging Imager may turn
    // either off. Reading the key without its default would answer false
    // whatever the build said.
    //
    // These assertions distinguish the two only in a build where the default
    // is true, which is the default configuration and this one.
    forgetSetting(QStringLiteral("telemetry"));
    forgetSetting(QStringLiteral("check_version"));
    ImageWriter w(nullptr);

    CHECK(w.getBoolSetting(QStringLiteral("telemetry")) == TELEMETRY_ENABLED_DEFAULT);
    CHECK(w.getBoolSetting(QStringLiteral("check_version")) == CHECK_VERSION_DEFAULT);
}

TEST_CASE("Turning telemetry off keeps it off", "[imagewriter][settings][defaults]")
{
    // The direction that matters. A default of true that overrode the stored
    // value would keep reporting for somebody who had explicitly said not to.
    forgetSetting(QStringLiteral("telemetry"));
    ImageWriter w(nullptr);
    REQUIRE(w.getBoolSetting(QStringLiteral("telemetry")) == TELEMETRY_ENABLED_DEFAULT);

    w.setSetting(QStringLiteral("telemetry"), false);
    CHECK_FALSE(w.getBoolSetting(QStringLiteral("telemetry")));

    w.setSetting(QStringLiteral("check_version"), false);
    CHECK_FALSE(w.getBoolSetting(QStringLiteral("check_version")));

    forgetSetting(QStringLiteral("telemetry"));
    forgetSetting(QStringLiteral("check_version"));
}

TEST_CASE("A key with no default of its own reads as off",
          "[imagewriter][settings][defaults]")
{
    // Everything else. QML asks for settings that were never written, and
    // the answer has to be a definite no rather than whatever the last key
    // with a default happened to return.
    forgetSetting(QStringLiteral("disable_warnings"));
    ImageWriter w(nullptr);
    CHECK_FALSE(w.getBoolSetting(QStringLiteral("disable_warnings")));
    CHECK_FALSE(w.getBoolSetting(QStringLiteral("connect_org_enabled")));
}

// ══════════════════════════════════════════════════════════════
// Who can read the settings file.
//
// It is not a list of preferences. It holds the crypt hash of the account
// password that will be created on the Pi, the derived WPA PSK for the
// wireless network -- password-equivalent, since a PSK joins the network on
// its own -- and, in organisation mode, the Raspberry Pi Connect API key in
// plain text.
//
// QSettings creates its file with 0666 masked by the umask, so on a typical
// desktop that is 0644 or 0664 and every other account on the machine can
// read all of it. secureSettingsFile is called before anything writes a
// setting.
// ══════════════════════════════════════════════════════════════

#include "settings_permissions.h"
#include <sys/stat.h>

namespace {
// The mode bits, which QFile::permissions does not give back in a form that
// can be compared against an octal literal.
unsigned fileMode(const QString& path)
{
    struct stat st{};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return 0;
    return st.st_mode & 07777;
}
} // namespace

TEST_CASE("A new settings file is readable only by its owner",
          "[imagewriter][settingsperms]")
{
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("nested/Imager.conf"));

    const auto result = rpi_imager::secureSettingsFile(path);

    CHECK(result.created);
    CHECK(result.secured);
    CHECK(QFileInfo::exists(path));
    CHECK(fileMode(path) == 0600);

    // The directory it sits in, too -- otherwise the filenames are still on
    // show even where the contents are not.
    CHECK(result.directorySecured);
    CHECK(fileMode(QFileInfo(path).absolutePath()) == 0700);
}

TEST_CASE("An installation that already has a readable file is narrowed",
          "[imagewriter][settingsperms]")
{
    // The upgrade path, and the reason this cannot only apply to files it
    // creates: anyone who has run an earlier version already has the file,
    // and QSettings will keep whatever permissions it finds for ever --
    // QSaveFile copies them from the file it replaces on every write.
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
    REQUIRE(::chmod(QFile::encodeName(path).constData(), 0644) == 0);
    REQUIRE(fileMode(path) == 0644);

    const auto result = rpi_imager::secureSettingsFile(path);

    CHECK(result.tightened);
    CHECK_FALSE(result.created);
    CHECK(result.secured);
    CHECK(fileMode(path) == 0600);

    // And the settings themselves are still there. Tightening permissions
    // must not be a way to lose somebody's saved customisation.
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    CHECK(f.readAll() == existing);
}

TEST_CASE("A group-readable file is narrowed too", "[imagewriter][settingsperms]")
{
    // 0664 rather than 0644: what a umask of 002 produces, which is the
    // default on Debian and Raspberry Pi OS for a user in their own group.
    // This is the shape the file actually has in the field.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    { QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    REQUIRE(::chmod(QFile::encodeName(path).constData(), 0664) == 0);

    CHECK(rpi_imager::secureSettingsFile(path).secured);
    CHECK(fileMode(path) == 0600);
}

TEST_CASE("Settings written afterwards stay owner-only",
          "[imagewriter][settingsperms]")
{
    // The property the whole approach rests on: Qt replaces the file through
    // QSaveFile, which copies the permissions of what it is replacing. If
    // that stopped being true, securing the file once at startup would be
    // undone by the first setting anybody changed.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));

    REQUIRE(rpi_imager::secureSettingsFile(path).secured);

    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("imagecustomization/sshUserPassword"),
                      QStringLiteral("$y$jB5$notarealhash"));
    settings.sync();

    REQUIRE(settings.status() == QSettings::NoError);
    CHECK(fileMode(path) == 0600);

    // Written and readable back, so this is not passing because nothing was
    // stored.
    QSettings reread(path, QSettings::IniFormat);
    CHECK(reread.value(QStringLiteral("imagecustomization/sshUserPassword")).toString()
          == QStringLiteral("$y$jB5$notarealhash"));
}

TEST_CASE("A symlink at the settings path is not followed",
          "[imagewriter][settingsperms]")
{
    // Imager elevates itself to write to a disk, and applyQuirks() then
    // points HOME back at the invoking user -- so as root it walks a
    // directory an unprivileged account controls. Following a symlink there
    // would let that account choose a file for root to change the
    // permissions of.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString victim = tmp.filePath(QStringLiteral("someone-elses-file"));
    { QFile f(victim); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    REQUIRE(::chmod(QFile::encodeName(victim).constData(), 0644) == 0);

    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    REQUIRE(QFile::link(victim, path));

    const auto result = rpi_imager::secureSettingsFile(path);

    // The target is left exactly as it was.
    CHECK(fileMode(victim) == 0644);
    // And the attempt is reported as having failed rather than claimed as a
    // success, so the caller warns.
    CHECK_FALSE(result.created);
    CHECK_FALSE(result.secured);
}

TEST_CASE("An empty path is refused rather than acted on",
          "[imagewriter][settingsperms]")
{
    // QSettings::fileName() can be empty where no application name is set.
    const auto result = rpi_imager::secureSettingsFile(QString());
    CHECK_FALSE(result.created);
    CHECK_FALSE(result.tightened);
    CHECK_FALSE(result.secured);
}

// ══════════════════════════════════════════════════════════════
// Whose settings file it is.
//
// On Linux the file is very often not owned by the person using Imager. An
// elevated run creates it as root in the user's own home -- that is where
// applyQuirks() repointing HOME leads -- and leaves it root-owned. The file
// on the machine this was written on is exactly that: root:root, mode 0664.
//
// Narrowing such a file to owner-only would be worse than leaving it: at
// 0664 the user can at least read the settings they saved, and at 0600 owned
// by root they cannot open it at all. So ownership is settled first.
// ══════════════════════════════════════════════════════════════

namespace {
bool havePasswordlessSudoForOwnership()
{
    QProcess p;
    p.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    return p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit &&
           p.exitCode() == 0;
}

bool sudoRun(const QStringList& args)
{
    QProcess p;
    p.start(QStringLiteral("sudo"), QStringList{QStringLiteral("-n")} + args);
    return p.waitForFinished(15000) && p.exitCode() == 0;
}

int ownerUidOf(const QString& path)
{
    struct stat st{};
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
        return -1;
    return static_cast<int>(st.st_uid);
}
} // namespace

TEST_CASE("A settings file belonging to another account is left readable",
          "[imagewriter][settingsperms][root]")
{
    // The safety case. Running unelevated, Imager cannot take ownership, and
    // must not narrow a file it does not own even if it could -- doing so
    // would hide the user's own saved customisation from them.
    if (!havePasswordlessSudoForOwnership())
        SKIP("passwordless sudo is not available, so no file can be made to "
             "belong to another account");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    { QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("[General]\n"); }
    REQUIRE(::chmod(QFile::encodeName(path).constData(), 0644) == 0);
    REQUIRE(sudoRun({QStringLiteral("chown"), QStringLiteral("0:0"), path}));
    REQUIRE(ownerUidOf(path) == 0);

    // -1: not elevated, so there is no invoking user to hand it back to.
    const auto result = rpi_imager::secureSettingsFile(path, -1, -1);

    CHECK(result.foreignOwner);
    CHECK_FALSE(result.secured);
    CHECK_FALSE(result.tightened);
    // Untouched, so the user can still read what they saved.
    CHECK(fileMode(path) == 0644);

    sudoRun({QStringLiteral("rm"), QStringLiteral("-f"), path});
}

#ifdef SETTINGS_PERMISSIONS_PROBE_BINARY
TEST_CASE("An elevated run hands the settings file back to the user",
          "[imagewriter][settingsperms]")
{
    // The repair, and the case that decides whether this change helps or
    // hurts. An elevated Imager creates the settings file as root, in the
    // user's own home -- applyQuirks() having repointed HOME there. Narrowing
    // that to 0600 with root as the owner would leave the person using
    // Imager unable to open their own settings at all; at 0664 they could at
    // least read them.
    //
    // Whether we happen to own the file is the wrong question, because in
    // this case we do. The file has to go to the account that invoked us,
    // and only then be narrowed.
    //
    // Needs a real root, so it runs in a user namespace with a range of uids
    // mapped: uid 0 inside is this account, and 1000 inside is another.
    QProcess check;
    check.start(QStringLiteral("unshare"),
                {QStringLiteral("-r"), QStringLiteral("--map-auto"),
                 QStringLiteral("true")});
    if (!check.waitForFinished(10000) || check.exitCode() != 0)
        SKIP("unshare -r --map-auto is unavailable, so no second uid can be "
             "mapped to hand the file to");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));

    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-r"), QStringLiteral("--map-auto"),
             QStringLiteral(SETTINGS_PERMISSIONS_PROBE_BINARY), path,
             QStringLiteral("1000"), QStringLiteral("1000")});
    REQUIRE(p.waitForFinished(30000));
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    INFO("probe said:\n" << out.toStdString());

    // It really was root doing this, or the case proves nothing.
    REQUIRE(out.contains(QStringLiteral("EUID=0")));

    CHECK(out.contains(QStringLiteral("OWNER=1000")));
    CHECK(out.contains(QStringLiteral("REOWNED=1")));
    CHECK(out.contains(QStringLiteral("MODE=600")));
    CHECK(out.contains(QStringLiteral("SECURED=1")));
    // Not refused: root can always finish the job.
    CHECK(out.contains(QStringLiteral("FOREIGN=0")));

    // The namespace leaves the file owned by a subordinate uid this account
    // cannot touch from outside; the temporary directory goes with the test.
    QProcess cleanup;
    cleanup.start(QStringLiteral("unshare"),
                  {QStringLiteral("-r"), QStringLiteral("--map-auto"),
                   QStringLiteral("rm"), QStringLiteral("-f"), path});
    cleanup.waitForFinished(10000);
}
#endif // SETTINGS_PERMISSIONS_PROBE_BINARY

TEST_CASE("A file the user already owns is narrowed without ceremony",
          "[imagewriter][settingsperms]")
{
    // The ordinary case, and the one that must not be disturbed by any of
    // the above: no chown, no refusal, just the permissions.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("Imager.conf"));
    { QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    REQUIRE(::chmod(QFile::encodeName(path).constData(), 0664) == 0);

    const auto result = rpi_imager::secureSettingsFile(path,
                                                       static_cast<int>(::getuid()),
                                                       static_cast<int>(::getgid()));
    CHECK(result.tightened);
    CHECK(result.secured);
    CHECK_FALSE(result.foreignOwner);
    CHECK_FALSE(result.reowned);
    CHECK(fileMode(path) == 0600);
}

// ══════════════════════════════════════════════════════════════
// Handing back everything else an elevated run leaves behind.
//
// The settings file is not the only thing. An elevated Imager writes the
// rpi-imager:// handler into ~/.local/share/applications, has
// update-desktop-database rewrite mimeinfo.cache and xdg-mime rewrite
// mimeapps.list, and fills a cache tree under ~/.cache -- all as root, all
// in a directory belonging to somebody else. On the machine this was written
// on, every one of those was root:root.
//
// The two MIME files are the ones that reach past Imager: they are shared
// with every application on the desktop, so once root owns them nothing else
// can register a file association either.
// ══════════════════════════════════════════════════════════════

TEST_CASE("Nothing is handed over when there is no invoking user to hand to",
          "[imagewriter][ownership]")
{
    // An ordinary unelevated run, which is most of them. -1 means "not
    // elevated, or nobody identifiable" and the whole thing has to be inert:
    // this walks directories in the user's home on every launch.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("thing"));
    { QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }

    CHECK(rpi_imager::restoreUserOwnership(path, -1, -1) == 0);
    CHECK(rpi_imager::restoreUserOwnership(QString(), 1000, 1000) == 0);

    // And a path that is not there is not an error to report.
    CHECK(rpi_imager::restoreUserOwnership(tmp.filePath(QStringLiteral("absent")),
                                           1000, 1000) == 0);
}

TEST_CASE("A file already belonging to the user is left alone",
          "[imagewriter][ownership]")
{
    // Counted as unchanged rather than chowned again, so the log line at
    // startup stays quiet on the overwhelmingly common case.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("mimeinfo.cache"));
    { QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }

    CHECK(rpi_imager::restoreUserOwnership(path, static_cast<int>(::getuid()),
                                           static_cast<int>(::getgid())) == 0);
    CHECK(ownerUidOf(path) == static_cast<int>(::getuid()));
}

TEST_CASE("A symlink is handed over as itself and not followed",
          "[imagewriter][ownership]")
{
    // Same reasoning as the settings file: root is walking a directory an
    // unprivileged account controls. A symlink pointing out of it must not
    // become a way to change the ownership of whatever it names.
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString target = tmp.filePath(QStringLiteral("target"));
    { QFile f(target); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    const QString link = tmp.filePath(QStringLiteral("link"));
    REQUIRE(QFile::link(target, link));

    // Nothing can be chowned here without privileges; what matters is that
    // the walk does not descend through a link, which is visible in the
    // count even when every chown fails.
    const QString dirTarget = tmp.filePath(QStringLiteral("realdir"));
    REQUIRE(QDir().mkpath(dirTarget + QStringLiteral("/deep")));
    { QFile f(dirTarget + QStringLiteral("/deep/file"));
      REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    const QString dirLink = tmp.filePath(QStringLiteral("dirlink"));
    REQUIRE(QFile::link(dirTarget, dirLink));

    // Handing to ourselves: every entry is already ours, so nothing changes
    // and the count is zero either way. The assertion that bites is below.
    CHECK(rpi_imager::restoreUserOwnership(dirLink, static_cast<int>(::getuid()),
                                           static_cast<int>(::getgid())) == 0);
    CHECK(QFileInfo(dirTarget + QStringLiteral("/deep/file")).exists());
}

#ifdef SETTINGS_PERMISSIONS_PROBE_BINARY
TEST_CASE("An elevated run hands the whole cache tree back",
          "[imagewriter][ownership]")
{
    // The cache is a tree, not a file: QNetworkDiskCache spreads it over
    // per-hex-digit subdirectories, and every one of them is created by root
    // on an elevated run. Handing back only the top of it would leave the
    // contents unreadable to the person the cache is for.
    //
    // Needs a real root with a second uid mapped, so it runs in a user
    // namespace as the settings-file case does.
    QProcess check;
    check.start(QStringLiteral("unshare"),
                {QStringLiteral("-r"), QStringLiteral("--map-auto"),
                 QStringLiteral("true")});
    if (!check.waitForFinished(10000) || check.exitCode() != 0)
        SKIP("unshare -r --map-auto is unavailable");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("oslistcache0"));

    // Build the tree and hand it over, all as root inside the namespace,
    // then report what the ownership looks like afterwards.
    const QString script = tmp.filePath(QStringLiteral("run.sh"));
    {
        QFile f(script);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "mkdir -p \"$1/data8/a\" \"$1/data8/b\"\n"
                "touch \"$1/data8/a/entry\" \"$1/data8/b/entry\"\n"
                "\"$2\" \"$1\" 1000 1000 own >/dev/null 2>&1\n"
                "echo \"TOTAL=$(find \"$1\" | wc -l)\"\n"
                "echo \"FOREIGN=$(find \"$1\" ! -user 1000 | wc -l)\"\n");
        f.close();
        REQUIRE(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner));
    }

    QProcess p;
    p.start(QStringLiteral("unshare"),
            {QStringLiteral("-r"), QStringLiteral("--map-auto"),
             QStringLiteral("sh"), script, root,
             QStringLiteral(SETTINGS_PERMISSIONS_PROBE_BINARY)});
    REQUIRE(p.waitForFinished(30000));
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    INFO(out.toStdString());

    // The tree really was built, so a count of zero foreign entries below
    // means something: root, data8, a, b and the two entries.
    CHECK(out.contains(QStringLiteral("TOTAL=6")));
    // Every one of them, to the bottom.
    CHECK(out.contains(QStringLiteral("FOREIGN=0")));

    QProcess cleanup;
    cleanup.start(QStringLiteral("unshare"),
                  {QStringLiteral("-r"), QStringLiteral("--map-auto"),
                   QStringLiteral("rm"), QStringLiteral("-rf"), root});
    cleanup.waitForFinished(15000);
}
#endif // SETTINGS_PERMISSIONS_PROBE_BINARY

// ══════════════════════════════════════════════════════════════
// Whether the secure-boot step is offered.
//
// Three separate things can put it there, and the wizard ORs them together:
//
//   secureBootAvailable = checkSWCapability("secure_boot")
//                      || isSecureBootForcedByCliFlag()
//                      || getDebugForceSecureBoot()
//
// The middle one is --enable-secure-boot, which exists so an operator
// provisioning a fleet can sign images whose metadata does not declare the
// capability. If it stopped working the option would simply not be there,
// with nothing on screen to say why and a command-line flag that appeared
// to be accepted.
//
// It is a static, set once from main() before the ImageWriter the QML sees
// necessarily exists, so being class-wide is the mechanism rather than an
// implementation detail.
// ══════════════════════════════════════════════════════════════

namespace {
// The flag is process-wide and nothing resets it; Catch2 runs cases in a
// random order, so each of these puts it back.
struct ForceSecureBootGuard
{
    explicit ForceSecureBootGuard(const ImageWriter& w)
        : _was(w.isSecureBootForcedByCliFlag()) {}
    ~ForceSecureBootGuard() { ImageWriter::setForceSecureBootEnabled(_was); }
    bool _was;
};
} // namespace

TEST_CASE("Secure boot is not offered unless something asks for it",
          "[imagewriter][secureboot]")
{
    ImageWriter w(nullptr);
    ForceSecureBootGuard guard(w);
    ImageWriter::setForceSecureBootEnabled(false);

    // An ordinary launch, and an image that does not declare the capability.
    w.setSWCapabilitiesList(QStringLiteral("[\"rpi_connect\"]"));

    CHECK_FALSE(w.isSecureBootForcedByCliFlag());
    CHECK_FALSE(w.getDebugForceSecureBoot());
    CHECK_FALSE(w.checkSWCapability(QStringLiteral("secure_boot")));
}

TEST_CASE("The command-line flag offers secure boot whatever the image says",
          "[imagewriter][secureboot]")
{
    ImageWriter w(nullptr);
    ForceSecureBootGuard guard(w);

    // An image whose metadata says nothing about secure boot -- a plain
    // Raspberry Pi OS release, or anything built before the capability
    // existed. The operator passing --enable-secure-boot has said they know
    // what they are doing.
    w.setSWCapabilitiesList(QStringLiteral("[]"));
    REQUIRE_FALSE(w.checkSWCapability(QStringLiteral("secure_boot")));

    ImageWriter::setForceSecureBootEnabled(true);
    CHECK(w.isSecureBootForcedByCliFlag());

    // The two are independent inputs to the same OR: forcing it must not
    // quietly rewrite what the image claims, which other steps also read.
    CHECK_FALSE(w.checkSWCapability(QStringLiteral("secure_boot")));
}

TEST_CASE("The flag reaches the ImageWriter the wizard is actually using",
          "[imagewriter][secureboot]")
{
    // main() sets this from the command line early, and the instance QML
    // binds to is created separately. Class-wide is what makes that work; a
    // per-instance flag would be set on one object and read from another,
    // and the option would never appear.
    ImageWriter first(nullptr);
    ForceSecureBootGuard guard(first);

    ImageWriter::setForceSecureBootEnabled(true);

    ImageWriter later(nullptr);
    CHECK(later.isSecureBootForcedByCliFlag());
}

TEST_CASE("The debug toggle is separate from the command-line flag",
          "[imagewriter][secureboot]")
{
    // The secret debug menu can turn the step on for one session without a
    // restart. It is per-instance and must not be confused with the CLI
    // flag: turning it off again should not switch off an operator's
    // --enable-secure-boot.
    ImageWriter w(nullptr);
    ForceSecureBootGuard guard(w);
    ImageWriter::setForceSecureBootEnabled(true);

    REQUIRE_FALSE(w.getDebugForceSecureBoot());
    w.setDebugForceSecureBoot(true);
    CHECK(w.getDebugForceSecureBoot());
    CHECK(w.isSecureBootForcedByCliFlag());

    w.setDebugForceSecureBoot(false);
    CHECK_FALSE(w.getDebugForceSecureBoot());
    CHECK(w.isSecureBootForcedByCliFlag());

    // And the other way round: clearing the CLI flag leaves the debug one.
    w.setDebugForceSecureBoot(true);
    ImageWriter::setForceSecureBootEnabled(false);
    CHECK(w.getDebugForceSecureBoot());
    CHECK_FALSE(w.isSecureBootForcedByCliFlag());
}

TEST_CASE("An image that declares secure boot needs no flag",
          "[imagewriter][secureboot]")
{
    ImageWriter w(nullptr);
    ForceSecureBootGuard guard(w);
    ImageWriter::setForceSecureBootEnabled(false);

    w.setSWCapabilitiesList(QStringLiteral("[\"secure_boot\"]"));
    CHECK(w.checkSWCapability(QStringLiteral("secure_boot")));
    CHECK_FALSE(w.isSecureBootForcedByCliFlag());
}

// ══════════════════════════════════════════════════════════════
// Which server the OS list actually came from.
//
// customRepoHost() is the only place the origin of a third-party OS list is
// shown, which makes it a security control rather than a label -- covered
// above for what it displays. This is the other half: keeping it truthful
// when the server it names is not the one that answered.
//
// A custom repository URL can redirect. If the displayed host stayed as the
// one the user typed, somebody who entered a URL they trusted would go on
// seeing that name while reading a list served by whatever it redirected
// to. The code says as much -- "users should see where data actually came
// from" -- and nothing tested it.
// ══════════════════════════════════════════════════════════════

namespace {
struct RepoHostSpy
{
    int hostChanged = 0;
    explicit RepoHostSpy(ImageWriter* w)
    {
        QObject::connect(w, &ImageWriter::customRepoHostChanged,
                         [this]() { ++hostChanged; });
    }
};

const QByteArray kMinimalOsList =
    QByteArrayLiteral("{\"os_list\":[{\"name\":\"An OS\",\"description\":\"d\","
                      "\"url\":\"\",\"icon\":\"\"}]}");
} // namespace

TEST_CASE("A redirected custom repository shows where the list really came from",
          "[imagewriter][repo][redirect]")
{
    OnlineWriter w;
    const QUrl asked(QStringLiteral("https://mirror.example.com/os_list.json"));
    const QUrl answered(QStringLiteral("https://elsewhere.invalid/os_list.json"));
    w.setCustomRepo(asked);
    REQUIRE(w.customRepoHost() == QStringLiteral("mirror.example.com"));

    RepoHostSpy spy(&w);
    w.onOsListFetchComplete(kMinimalOsList, asked, answered);

    CHECK(w.customRepoHost() == QStringLiteral("elsewhere.invalid"));
    // And the UI is told, so the label on screen is refreshed rather than
    // staying at the name the user typed.
    CHECK(spy.hostChanged == 1);
}

TEST_CASE("A redirect that stays on the same host is not announced",
          "[imagewriter][repo][redirect]")
{
    // http to https, or a path the server rewrote. The origin has not
    // changed, so there is nothing for the user to re-evaluate and no reason
    // to redraw the label.
    OnlineWriter w;
    const QUrl asked(QStringLiteral("https://mirror.example.com/os_list.json"));
    const QUrl answered(QStringLiteral("https://mirror.example.com/v2/os_list.json"));
    w.setCustomRepo(asked);

    RepoHostSpy spy(&w);
    w.onOsListFetchComplete(kMinimalOsList, asked, answered);

    CHECK(w.customRepoHost() == QStringLiteral("mirror.example.com"));
    CHECK(spy.hostChanged == 0);
}

TEST_CASE("The official repository is not rewritten by a redirect",
          "[imagewriter][repo][redirect]")
{
    // downloadsraspberrypi.com sits behind a CDN and may well answer from
    // somewhere else. That is not a custom repository and must not start
    // being displayed as one -- the whole point of the label is that it
    // appears when the list is *not* Raspberry Pi's.
    OnlineWriter w;
    REQUIRE_FALSE(w.customRepo());
    const QUrl official = w.osListUrl();

    RepoHostSpy spy(&w);
    w.onOsListFetchComplete(kMinimalOsList, official,
                            QUrl(QStringLiteral("https://cdn.invalid/os_list.json")));

    CHECK_FALSE(w.customRepo());
    CHECK(w.customRepoHost().isEmpty());
    CHECK(spy.hostChanged == 0);
}

TEST_CASE("A redirect with nowhere to redirect to changes nothing",
          "[imagewriter][repo][redirect]")
{
    // No redirect happened: the effective URL is the one asked for. Also the
    // case where the transport could not report one, which arrives as an
    // invalid URL and must not blank the label.
    OnlineWriter w;
    const QUrl asked(QStringLiteral("https://mirror.example.com/os_list.json"));

    SECTION("the same URL came back")
    {
        w.setCustomRepo(asked);
        RepoHostSpy spy(&w);
        w.onOsListFetchComplete(kMinimalOsList, asked, asked);
        CHECK(w.customRepoHost() == QStringLiteral("mirror.example.com"));
        CHECK(spy.hostChanged == 0);
    }

    SECTION("no effective URL was reported")
    {
        w.setCustomRepo(asked);
        RepoHostSpy spy(&w);
        w.onOsListFetchComplete(kMinimalOsList, asked, QUrl());
        CHECK(w.customRepoHost() == QStringLiteral("mirror.example.com"));
        CHECK(spy.hostChanged == 0);
    }
}

// ══════════════════════════════════════════════════════════════
// Whether a fastboot write reads the cache or the network.
//
// The three easy answers are covered above. These are the two that decide
// what actually gets written to a compute module, and the comment on
// resolveFlashSource is explicit about why they differ: "Verified is the
// condition, not merely present -- an unverified cache file may be a partial
// download, and writing that to a board is worse than fetching it again."
//
// The failure on one side is a board flashed from a truncated image. On the
// other it is what the comment was written to fix: a fastboot write going to
// the network with the image already fully cached, which surfaced to the
// user as "Recv failure: Connection reset by peer" on a download they could
// see had already happened.
// ══════════════════════════════════════════════════════════════

TEST_CASE("A fastboot flash reads the cache only once it has been checked",
          "[imagewriter][rpiboot-handoff][cache]")
{
    // Both halves in one case, on one cache file, so the difference between
    // them is verification and nothing else. Asserting the unverified answer
    // on its own could not tell "present but unchecked" from "no cache at
    // all", which is already covered above.
    CacheFixture fx;
    ImageWriter w(nullptr);
    w.setVerifyEnabled(false);
    const QByteArray hash = sha256HexOf(fx.cacheBytes());

    w.setSrc(fx.sourceUrl(), 0, CacheFixture::kSize, hash);
    w.setCustomCacheFile(fx.cachePath(), hash);
    w.setDst(fx.target(), CacheFixture::kSize);

    // Nothing has checked the file yet. It might be half a download, and
    // writing that to a compute module is worse than fetching it again.
    CHECK(w.resolveFlashSource() == fx.sourceUrl());

    // A write verifies it on the way past.
    const WriteOutcome out = runWrite(w);
    INFO("errors: " << out.errors.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(out.succeeded);

    // Now the local file. Without this a fastboot write goes to the network
    // with the image already fully cached, which is what the comment on
    // resolveFlashSource was written to fix -- it reached the user as "Recv
    // failure: Connection reset by peer" on a download they could see had
    // already happened.
    CHECK(w.resolveFlashSource() == QUrl::fromLocalFile(fx.cachePath()));
}

// ══════════════════════════════════════════════════════════════
// What each tick of the network poll decides.
//
// isOnline() runs every second for as long as Imager is open, and the four
// branches it chooses between were only ever reachable by arranging a real
// network to change under the test. Two of them exist because they were
// missing: a user whose first fetch was blocked never got a list at all
// (#1212), and a user who started with no network saw an empty screen with
// no explanation (#809).
//
// The arithmetic is now separable, so every combination can be stated.
// ══════════════════════════════════════════════════════════════

#include "network_poll_action.h"

TEST_CASE("Connectivity arriving with no list fetches one",
          "[imagewriter][netpoll]")
{
    // GitHub #1212: the first attempt was refused by a firewall, the user
    // allowed it, and nothing ever tried again. The retry is this branch.
    CHECK(rpi_net::planPollAction(true, false, false)
          == rpi_net::PollAction::ComeOnlineAndFetch);
}

TEST_CASE("Connectivity arriving with a list already here fetches nothing",
          "[imagewriter][netpoll]")
{
    // Coming back from sleep, or a cable in after the list already loaded.
    // Worth recording; not worth downloading the list again.
    CHECK(rpi_net::planPollAction(true, false, true)
          == rpi_net::PollAction::ComeOnline);
}

TEST_CASE("A poll while already online and connected does nothing",
          "[imagewriter][netpoll]")
{
    // The overwhelmingly common tick, and the one with the sharpest
    // consequence if it decided anything: this runs once a second, so a
    // branch that fetched here would re-download the OS list every second
    // for as long as the window was open.
    CHECK(rpi_net::planPollAction(true, true, true) == rpi_net::PollAction::Nothing);
    CHECK(rpi_net::planPollAction(true, true, false) == rpi_net::PollAction::Nothing);
}

TEST_CASE("Losing the network is noticed once", "[imagewriter][netpoll]")
{
    CHECK(rpi_net::planPollAction(false, true, true) == rpi_net::PollAction::GoOffline);

    // With no list either, going offline still takes precedence on this tick.
    // The screen learns there is nothing to show on the *next* one, once
    // wasOnline has become false -- recorded because it is a real one-tick
    // delay, and a second of it is not worth reordering the branches for.
    CHECK(rpi_net::planPollAction(false, true, false) == rpi_net::PollAction::GoOffline);
    CHECK(rpi_net::planPollAction(false, false, false)
          == rpi_net::PollAction::ReportUnavailable);
}

TEST_CASE("No network and nothing to show says so", "[imagewriter][netpoll]")
{
    // GitHub #809: started with no network, and the screen sat empty with no
    // explanation and no Retry.
    CHECK(rpi_net::planPollAction(false, false, false)
          == rpi_net::PollAction::ReportUnavailable);
}

TEST_CASE("No network but a list already loaded leaves it alone",
          "[imagewriter][netpoll]")
{
    // The user is browsing a list that was fetched before the network went.
    // Announcing "unavailable" here would blank a screen they are using --
    // the list is still perfectly good, only the network is gone.
    CHECK(rpi_net::planPollAction(false, false, true) == rpi_net::PollAction::Nothing);
}

// ══════════════════════════════════════════════════════════════
// The repository a Pi carries in its bootloader EEPROM.
//
// Imager running on the Pi itself reads the bootloader configuration out of
// nvmem at startup, and an IMAGER_REPO_URL= there replaces the default OS
// list. It is how a fleet points every board it images at its own image
// server without anybody typing a URL, so it is a setting nobody sees
// working and everybody notices failing.
//
// Reaching it needs the device tree, a find across /sys/bus/nvmem and a Pi
// to run on, so none of the parsing was covered.
// ══════════════════════════════════════════════════════════════

#include "eeprom_repo_override.h"

TEST_CASE("A repository burned into the EEPROM is used",
          "[imagewriter][eeprom]")
{
    const QByteArray conf =
        "[all]\n"
        "BOOT_UART=1\n"
        "IMAGER_REPO_URL=https://images.example.invalid/os_list.json\n"
        "BOOT_ORDER=0xf41\n";
    CHECK(rpi_eeprom::repoUrlFromBlconfig(conf)
          == QStringLiteral("https://images.example.invalid/os_list.json"));
}

TEST_CASE("A configuration that does not mention it asks for nothing",
          "[imagewriter][eeprom]")
{
    CHECK(rpi_eeprom::repoUrlFromBlconfig("[all]\nBOOT_UART=1\n").isEmpty());
    CHECK(rpi_eeprom::repoUrlFromBlconfig(QByteArray()).isEmpty());
}

TEST_CASE("Only the exact key is honoured", "[imagewriter][eeprom]")
{
    // A longer key beginning the same way is a different setting, and a
    // commented-out line is somebody's note. Taking either would send a whole
    // fleet at a URL nobody meant to set.
    CHECK(rpi_eeprom::repoUrlFromBlconfig(
              "IMAGER_REPO_URL_BACKUP=https://wrong.invalid/os.json\n").isEmpty());
    CHECK(rpi_eeprom::repoUrlFromBlconfig(
              "#IMAGER_REPO_URL=https://wrong.invalid/os.json\n").isEmpty());
    CHECK(rpi_eeprom::repoUrlFromBlconfig(
              " IMAGER_REPO_URL=https://wrong.invalid/os.json\n").isEmpty());
}

TEST_CASE("The value survives how the flash region is written",
          "[imagewriter][eeprom]")
{
    const QString expected = QStringLiteral("https://images.example.invalid/os.json");

    SECTION("CRLF line endings")
    {
        CHECK(rpi_eeprom::repoUrlFromBlconfig(
                  "BOOT_UART=1\r\nIMAGER_REPO_URL=https://images.example.invalid/os.json\r\n")
              == expected);
    }

    SECTION("no trailing newline at the end of the region")
    {
        CHECK(rpi_eeprom::repoUrlFromBlconfig(
                  "IMAGER_REPO_URL=https://images.example.invalid/os.json")
              == expected);
    }

    SECTION("padding after the text, which nvmem leaves behind")
    {
        QByteArray conf = "IMAGER_REPO_URL=https://images.example.invalid/os.json\n";
        conf.append(QByteArray(64, '\0'));
        CHECK(rpi_eeprom::repoUrlFromBlconfig(conf) == expected);
    }
}

TEST_CASE("A key with nothing after it is not an override to nowhere",
          "[imagewriter][eeprom]")
{
    // A half-finished burn. Taking the empty value would replace the default
    // list with a URL that fetches nothing: the operator gets an empty OS
    // list and a screen saying the data came from nowhere in particular,
    // which tells them neither what happened nor what to do. Leaving them on
    // Raspberry Pi's list is wrong in a way they can see and act on.
    CHECK(rpi_eeprom::repoUrlFromBlconfig("IMAGER_REPO_URL=\n").isEmpty());
    CHECK(rpi_eeprom::repoUrlFromBlconfig("IMAGER_REPO_URL=   \n").isEmpty());

    // And it does not wipe out a good one that came before it, which is
    // what a re-burn that stopped half way leaves behind. On its own an
    // empty value is indistinguishable from no value -- both return nothing
    // -- so this pairing is the only case where skipping it can be told
    // apart from taking it.
    CHECK(rpi_eeprom::repoUrlFromBlconfig(
              "IMAGER_REPO_URL=https://images.example.invalid/os.json\n"
              "IMAGER_REPO_URL=\n")
          == QStringLiteral("https://images.example.invalid/os.json"));
}

TEST_CASE("With two of them, the last is taken", "[imagewriter][eeprom]")
{
    // Recorded rather than chosen. The original loop kept assigning without
    // breaking, so the last won; nothing says which one the bootloader
    // itself would honour, and a configuration carrying two is already
    // malformed. Pinned so a change of mind is deliberate.
    CHECK(rpi_eeprom::repoUrlFromBlconfig(
              "IMAGER_REPO_URL=https://first.invalid/os.json\n"
              "IMAGER_REPO_URL=https://second.invalid/os.json\n")
          == QStringLiteral("https://second.invalid/os.json"));
}

// ══════════════════════════════════════════════════════════════════════════
// The username the customisation dialog offers
//
// getCurrentUser() fills in the account name on the OS customisation page,
// and whatever is there when the user presses Next becomes the account on
// the Pi. The desktop account it starts from is not a Unix username: it can
// carry capitals, it can have a space in it on Windows and macOS, and when
// the imager has been started with sudo it is "root".
//
// A name with a space in it is not a valid Linux username; "root" is an
// account the first-boot script will not create, and offering it as the
// default hands the user a card that stops at first boot with nothing to say
// why. Only the plain case had ever been run.
// ══════════════════════════════════════════════════════════════════════════

namespace {

// Runs the accessor with USER and USERNAME set as given, and puts the
// environment back afterwards -- these are read from the process, and
// leaving them changed would follow into the next case.
class ScopedUserEnv
{
public:
    ScopedUserEnv(const char *user, const char *username)
        : _user(qgetenv("USER")), _username(qgetenv("USERNAME"))
    {
        set("USER", user);
        set("USERNAME", username);
    }

    ~ScopedUserEnv()
    {
        set("USER", _user.isNull() ? nullptr : _user.constData());
        set("USERNAME", _username.isNull() ? nullptr : _username.constData());
    }

    ScopedUserEnv(const ScopedUserEnv &) = delete;
    ScopedUserEnv &operator=(const ScopedUserEnv &) = delete;

private:
    static void set(const char *name, const char *value)
    {
        if (value)
            qputenv(name, QByteArray(value));
        else
            qunsetenv(name);
    }

    QByteArray _user;
    QByteArray _username;
};

} // namespace

TEST_CASE("The offered username is the account name, in lower case",
          "[imagewriter][username]")
{
    ScopedUserEnv env("Charlie", nullptr);
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("charlie"));
}

TEST_CASE("An account name with a space is cut at the first word",
          "[imagewriter][username]")
{
    // "Ada Lovelace" is a perfectly ordinary account name on Windows and
    // macOS and not a username Linux will accept. Offering it whole produces
    // a card whose first boot fails to create the account.
    ScopedUserEnv env("Ada Lovelace", nullptr);
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("ada"));
}

TEST_CASE("Running as root does not offer root as the account to create",
          "[imagewriter][username]")
{
    // The imager is routinely started with sudo on Linux, which is exactly
    // when this is read. "root" is not an account the first-boot script will
    // create, and it is not one anybody should be offered by default.
    ScopedUserEnv env("root", nullptr);
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("pi"));
}

TEST_CASE("With no account name in the environment the default is offered",
          "[imagewriter][username]")
{
    ScopedUserEnv env("", nullptr);
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("pi"));
}

TEST_CASE("The Windows spelling of the variable is read too",
          "[imagewriter][username]")
{
    // Windows sets USERNAME rather than USER, and the customisation page is
    // the same page there.
    ScopedUserEnv env("", "Grace");
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("grace"));
}

TEST_CASE("A Windows account name with a space is cut as well",
          "[imagewriter][username]")
{
    ScopedUserEnv env("", "Grace Hopper");
    ImageWriter writer(nullptr);
    CHECK(writer.getCurrentUser() == QStringLiteral("grace"));
}
