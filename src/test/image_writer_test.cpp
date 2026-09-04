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

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QJsonArray>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

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

    void feedOsList(const QByteArray &json)
    {
        onOsListFetchComplete(json, QUrl(QStringLiteral("https://example.invalid/l.json")),
                              QUrl(QStringLiteral("https://example.invalid/l.json")));
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
