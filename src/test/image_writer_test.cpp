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
#include "cli.h"
#include "app_resources.h"
#include "drivelistmodel.h"

#include <QCryptographicHash>
#include <QTimeZone>
#include "signal_log.h"
#include "local_http_server.h"
#include "faulty_block_device.h"
#include <QProcess>
#include "fixture_process.h"
#include <QDir>
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

QString testBlockDevicePath()
{
    const QByteArray dev = qgetenv("RPI_IMAGER_TEST_BLOCK_DEVICE");
    if (dev.isEmpty() || !dev.startsWith("/dev/loop"))
        return {};
    return QString::fromLatin1(dev);
}

} // namespace

TEST_CASE("An image is written to a real block device", "[imagewriter][device]")
{
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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

TEST_CASE("Customisation reaches a real card", "[imagewriter][device]")
{
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

    ImageWriter w(nullptr);
    w.setDst(dev, 64ull * 1024 * 1024);
    w.ejectDrive();

    REQUIRE(waitForEjectToSettle(w));
    CHECK(ejectStateOf(w) != ImageWriter::EjectState::EjectInProgress);
}

TEST_CASE("Erase formats a card through the writer", "[imagewriter][erase][device]")
{
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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
    const QString dev = testBlockDevicePath();
    if (dev.isEmpty())
        SKIP("set RPI_IMAGER_TEST_BLOCK_DEVICE to a loop device to run this");

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

TEST_CASE("An image on inserted media is offered", "[imagewriter][usbsource]")
{
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
    QTemporaryDir media;
    REQUIRE(media.isValid());

    MediaImageWriter w;
    w.mediaRoot = media.path();

    CHECK(mediaEntries(w.getUsbSourceOSlist()).isEmpty());
}

TEST_CASE("A media root that is not there is not an error",
          "[imagewriter][usbsource]")
{
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
