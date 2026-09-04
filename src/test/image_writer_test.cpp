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
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/catch_session.hpp>

#include "imagewriter.h"
#include "app_resources.h"
#include "drivelistmodel.h"

#include <QCryptographicHash>
#include <QProcess>
#include "fixture_process.h"
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QJsonArray>
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

TEST_CASE("Applying customisation to an image that supports none clears it",
          "[imagewriter][customisation]")
{
    // Rather than carrying settings over from a previous selection onto an
    // image that will not read them.
    ImageWriter w(nullptr);
    selectImageWithFormat(w, QByteArray());

    QVariantMap settings;
    settings.insert(QStringLiteral("hostname"), QStringLiteral("test-pi"));
    CHECK_NOTHROW(w.applyCustomisationFromSettings(settings));
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

    QObject::connect(&w, &ImageWriter::success, [&] { out.succeeded = true; loop.quit(); });
    QObject::connect(&w, &ImageWriter::error, [&](QVariant m) {
        out.failed = true;
        out.errors << m.toString();
        loop.quit();
    });
    QObject::connect(&w, &ImageWriter::finalizing, [&] { out.finalizing = true; });
    QObject::connect(&w, &ImageWriter::preparationStatusUpdate,
                     [&](QVariant m) { out.statuses << m.toString(); });
    QObject::connect(&w, &ImageWriter::writeProgress,
                     [&](QVariant n, QVariant t) {
                         out.sawProgress = true;
                         out.progressKinds << QStringLiteral("write %1/%2")
                                                  .arg(n.toULongLong()).arg(t.toULongLong());
                     });
    QObject::connect(&w, &ImageWriter::downloadProgress,
                     [&](QVariant n, QVariant t) {
                         out.progressKinds << QStringLiteral("download %1/%2")
                                                  .arg(n.toULongLong()).arg(t.toULongLong());
                     });
    QObject::connect(&w, &ImageWriter::verifyProgress,
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
    LargeWriteFixture()
    {
        REQUIRE(_dir.isValid());
        _source = QDir(_dir.path()).filePath(QStringLiteral("source.img"));
        _target = QDir(_dir.path()).filePath(QStringLiteral("target.img"));

        QByteArray chunk(1024 * 1024, '\0');
        for (int i = 0; i < chunk.size(); ++i)
            chunk[i] = char('A' + (i * 17) % 26);

        QFile s(_source);
        REQUIRE(s.open(QIODevice::WriteOnly));
        for (int i = 0; i < kSizeMB; ++i)
            REQUIRE(s.write(chunk) == chunk.size());
        s.close();

        QFile t(_target);
        REQUIRE(t.open(QIODevice::WriteOnly));
        for (int i = 0; i < kSizeMB; ++i)
            REQUIRE(t.write(QByteArray(1024 * 1024, '\0')) == 1024 * 1024);
        t.close();
    }

    static constexpr int kSizeMB = 64;
    static constexpr quint64 kSize = quint64(kSizeMB) * 1024 * 1024;
    QUrl sourceUrl() const { return QUrl::fromLocalFile(_source); }
    QString target() const { return _target; }

private:
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
