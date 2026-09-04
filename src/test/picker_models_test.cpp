/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The three models behind the choosers: which operating systems are offered,
 * which board the list is filtered for, and which drives can be written to.
 *
 * The failures here are the ones a user reports as "it doesn't work" with
 * nothing in the log: an empty OS list, a chooser full of blank rows, or a
 * drive that is missing from the list -- or worse, present when it should not
 * be. None of it was reachable before, because these models were only ever
 * built by the QML engine. They are not: they take an ImageWriter and
 * nothing else.
 *
 * roleNames() gets particular attention. QML binds to those strings by name,
 * so a renamed or absent role does not fail to compile or throw -- the
 * binding silently resolves to undefined and the row renders empty.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "imagewriter.h"
#include "app_resources.h"
#include "drivelistmodel.h"
#include "hwlistmodel.h"
#include "oslistmodel.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemModel>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>

namespace {

// onOsListFetchComplete() is a protected slot, so a subclass can hand the
// models a list without a network fetch.
class TestableImageWriter : public ImageWriter
{
public:
    TestableImageWriter() : ImageWriter(nullptr) {}

    void feedOsList(const QByteArray &json)
    {
        onOsListFetchComplete(json, QUrl(QStringLiteral("https://example.invalid/os_list.json")),
                              QUrl(QStringLiteral("https://example.invalid/os_list.json")));
    }
};

QByteArray osListJson()
{
    return QByteArray(R"JSON({
        "imager": {
            "latest_version": "1.9.0",
            "devices": [
                {
                    "name": "Raspberry Pi 5",
                    "tags": ["pi5-64bit"],
                    "capabilities": ["nvme"],
                    "icon": "icons/pi5.png",
                    "description": "The 2023 model",
                    "matching_type": "inclusive",
                    "architecture": "arm64"
                },
                {
                    "name": "Raspberry Pi Zero 2 W",
                    "tags": ["pi-zero2-64bit"],
                    "capabilities": [],
                    "icon": "icons/pizero2.png",
                    "description": "A small one",
                    "matching_type": "exclusive",
                    "architecture": "armhf",
                    "default": true
                }
            ]
        },
        "os_list": [
            {
                "name": "Raspberry Pi OS (64-bit)",
                "description": "A port of Debian Bookworm",
                "url": "https://downloads.raspberrypi.org/raspios.img.xz",
                "icon": "icons/raspios.png",
                "extract_size": 5368709120,
                "image_download_size": 1073741824,
                "init_format": "systemd"
            },
            {
                "name": "Other general-purpose OS",
                "description": "A category",
                "subitems": [
                    {
                        "name": "Ubuntu Desktop",
                        "url": "https://cdimage.ubuntu.com/ubuntu.img.xz",
                        "extract_size": 10737418240,
                        "init_format": "cloudinit"
                    }
                ]
            }
        ]
    })JSON");
}

// The model overrides are declared protected on these subclasses, but QML
// never sees the subclass -- it drives QAbstractItemModel, where they are
// public. Going through the abstract interface is both legal and the same
// path the UI takes.
QStringList roleNameList(const QAbstractItemModel &m)
{
    QStringList names;
    const auto roles = m.roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        names << QString::fromUtf8(it.value());
    names.sort();
    return names;
}

} // namespace

// ══════════════════════════════════════════════════════════════
// The OS chooser
// ══════════════════════════════════════════════════════════════

TEST_CASE("The OS list offers its built-in entries even with no list fetched",
          "[models][oslist]")
{
    // Before anything is downloaded the chooser is not empty: Erase and the
    // custom-image entry are synthesised. If that ever stopped being true the
    // user would be looking at a blank list with no way to reach either.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model != nullptr);
    QAbstractItemModel *view = model;
    REQUIRE(model->reload());

    CHECK(view->rowCount(QModelIndex()) > 0);
}

TEST_CASE("A fetched OS list reaches the chooser", "[models][oslist]")
{
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model != nullptr);
    QAbstractItemModel *view = model;

    const int before = (model->reload(), view->rowCount(QModelIndex()));
    writer.feedOsList(osListJson());
    REQUIRE(model->reload());
    const int after = view->rowCount(QModelIndex());

    INFO("rows before " << before << ", after " << after);
    CHECK(after > before);
}

TEST_CASE("Every OS role QML binds to is present", "[models][oslist]")
{
    // A missing role is not an error anywhere: the QML binding resolves to
    // undefined and the row draws blank.
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const QStringList names = roleNameList(*model);
    INFO("roles: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.isEmpty());
    for (const char *required : {"name", "description", "url", "icon"}) {
        INFO("required role: " << required);
        CHECK(names.contains(QString::fromLatin1(required)));
    }
}

TEST_CASE("Role names are unique", "[models][oslist]")
{
    // Two roles sharing a name means one of them is unreachable from QML.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const auto roles = static_cast<QAbstractItemModel *>(model)->roleNames();
    QSet<QByteArray> seen;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        INFO("role: " << it.value().toStdString());
        CHECK_FALSE(seen.contains(it.value()));
        seen.insert(it.value());
    }
}

TEST_CASE("Asking the OS list for a row that is not there is harmless",
          "[models][oslist]")
{
    // QML asks for indices during teardown and while a list is being
    // replaced; an out-of-range fetch must return an invalid value, not read
    // past the end of the vector.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    CHECK_FALSE(view->data(QModelIndex(), Qt::DisplayRole).isValid());
    CHECK_FALSE(view->data(view->index(9999, 0), Qt::DisplayRole).isValid());
    CHECK_FALSE(view->data(view->index(-1, 0), Qt::DisplayRole).isValid());
}

// ══════════════════════════════════════════════════════════════
// The hardware chooser
// ══════════════════════════════════════════════════════════════

TEST_CASE("The hardware list populates and has a current selection",
          "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    REQUIRE(model != nullptr);
    REQUIRE(model->reload());

    CHECK(view->rowCount(QModelIndex()) > 0);

    // The entry flagged "default" in the feed is what the chooser lands on
    // when nothing better is known about the machine. Without it there is no
    // selection at all, and the OS list has nothing to filter by.
    CHECK(model->currentIndex() >= 0);
    CHECK(model->currentName() == QStringLiteral("Raspberry Pi Zero 2 W"));
    CHECK(model->currentArchitecture() == QStringLiteral("armhf"));
}

TEST_CASE("With no default flagged, nothing is preselected", "[models][hwlist]")
{
    // -1 rather than an arbitrary row: silently landing on the first entry
    // would filter the OS list for a board the user never chose.
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": {
            "devices": [
                { "name": "Board A", "tags": [], "architecture": "arm64" },
                { "name": "Board B", "tags": [], "architecture": "armhf" }
            ]
        },
        "os_list": []
    })JSON"));

    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    CHECK(model->currentIndex() == -1);
}

TEST_CASE("Selecting a board changes the name and architecture reported",
          "[models][hwlist]")
{
    // currentArchitecture() is what orders the OS list for the attached
    // board, so it has to follow the selection.
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    REQUIRE(model->reload());
    const int rows = view->rowCount(QModelIndex());
    REQUIRE(rows > 1);

    model->setCurrentIndex(0);
    const QString firstName = model->currentName();

    model->setCurrentIndex(rows - 1);
    const QString lastName = model->currentName();

    INFO("first: " << firstName.toStdString() << "  last: " << lastName.toStdString());
    CHECK(firstName != lastName);
    CHECK_FALSE(model->currentArchitecture().isEmpty());
}

TEST_CASE("An out-of-range board selection does not take the chooser with it",
          "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    REQUIRE(model->reload());
    const int rows = view->rowCount(QModelIndex());

    model->setCurrentIndex(rows + 500);
    CHECK(model->currentIndex() >= -1);
    model->setCurrentIndex(-42);
    CHECK(model->currentIndex() >= -1);

    // Still usable afterwards.
    model->setCurrentIndex(0);
    CHECK(model->currentIndex() == 0);
}

TEST_CASE("Every hardware role QML binds to is present", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    REQUIRE(model->reload());

    const QStringList names = roleNameList(*model);
    INFO("roles: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.isEmpty());
    CHECK(names.contains(QStringLiteral("name")));
}

TEST_CASE("A list with no hardware section leaves the board chooser alone",
          "[models][hwlist]")
{
    // An OS list served without an imager.devices array -- an older or
    // trimmed feed -- must be declined rather than half-applied, so the
    // chooser keeps whatever it already had instead of emptying.
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": { "latest_version": "1.9.0" },
        "os_list": [ { "name": "Something" } ]
    })JSON"));

    HWListModel *model = writer.getHWList();
    CHECK_FALSE(model->reload());
}

// ══════════════════════════════════════════════════════════════
// The drive chooser
// ══════════════════════════════════════════════════════════════

TEST_CASE("Every drive role QML binds to is present", "[models][drivelist]")
{
    // These are the columns of the drive chooser. A missing one renders a
    // drive with no name or no size, which is how somebody picks the wrong
    // one.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();
    REQUIRE(model != nullptr);

    const QStringList names = roleNameList(*model);
    INFO("roles: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(names.isEmpty());
    for (const char *required : {"device", "description", "size"}) {
        INFO("required role: " << required);
        CHECK(names.contains(QString::fromLatin1(required)));
    }
}

TEST_CASE("Asking the drive list for a row that is not there is harmless",
          "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();
    QAbstractItemModel *view = model;

    CHECK_FALSE(view->data(QModelIndex(), Qt::DisplayRole).isValid());
    CHECK_FALSE(view->data(view->index(9999, 0), Qt::DisplayRole).isValid());
}

TEST_CASE("Drive polling can be started, paused, resumed and stopped",
          "[models][drivelist]")
{
    // The write path pauses polling while it writes -- enumerating drives
    // mid-write costs I/O and can disturb the device -- and resumes after.
    // Getting the lifecycle wrong leaves the chooser frozen once a write has
    // finished.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();
    QAbstractItemModel *view = model;

    model->startPolling();
    model->pausePolling();
    model->resumePolling();
    model->setSlowPolling();
    model->stopPolling();

    // Out of order, and repeated, as the real callers manage to do.
    model->stopPolling();
    model->resumePolling();
    model->pausePolling();
    model->startPolling();
    model->stopPolling();

    CHECK(view->rowCount(QModelIndex()) >= 0);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    initAppResources();
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("picker_models_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}
