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
#include <atomic>
#include <QEventLoop>
#include <QElapsedTimer>
#include "drivelistmodelpollthread.h"
#include "drivelist/drivelist.h"
#include "hwlistmodel.h"
#include "model_row_diff.h"
#include "oslistmodel.h"
#include "signal_log.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemModel>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <cstdint>
#include <string>
#include <vector>
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
// Working out what changed between two lists of rows
//
// The arithmetic both choosers share. It answers one question -- which run
// of rows came out and which run went in -- and the models do the
// signalling themselves, because beginRemoveRows and its siblings are
// protected members of QAbstractItemModel.
//
// Getting it wrong is not a cosmetic matter: an insert reported at the wrong
// row leaves the view showing one thing and the model holding another, which
// is how a user clicks the operating system above the one they meant.

TEST_CASE("Two identical lists have no difference", "[models][rowdiff]")
{
    const QStringList rows{"a", "b", "c"};
    const rpi_model::RowDiff diff = rpi_model::planRowDiff(rows, rows);

    CHECK(diff.isEmpty());
    CHECK(diff.removed == 0);
    CHECK(diff.inserted == 0);
}

TEST_CASE("Rows appended go in at the end", "[models][rowdiff]")
{
    const auto diff = rpi_model::planRowDiff({"a", "b"}, {"a", "b", "c", "d"});

    CHECK(diff.at == 2);
    CHECK(diff.removed == 0);
    CHECK(diff.inserted == 2);
}

TEST_CASE("Rows arriving above the ones already there go in at the top",
          "[models][rowdiff]")
{
    // The case the OS list is in before its fetch lands: the two internal
    // entries are there and the real list appears above them. Reporting it
    // this way is what lets those two keep their delegates -- and with them,
    // any click the user has in progress.
    const auto diff = rpi_model::planRowDiff(
        {"Erase", "Use custom"},
        {"Alpha", "Beta", "Gamma", "Erase", "Use custom"});

    CHECK(diff.at == 0);
    CHECK(diff.removed == 0);
    CHECK(diff.inserted == 3);
}

TEST_CASE("A row taken out of the middle is reported where it was",
          "[models][rowdiff]")
{
    const auto diff = rpi_model::planRowDiff({"a", "b", "c"}, {"a", "c"});

    CHECK(diff.at == 1);
    CHECK(diff.removed == 1);
    CHECK(diff.inserted == 0);
}

TEST_CASE("A row replaced in the middle is a removal and an insertion",
          "[models][rowdiff]")
{
    const auto diff = rpi_model::planRowDiff({"a", "b", "c"}, {"a", "x", "c"});

    CHECK(diff.at == 1);
    CHECK(diff.removed == 1);
    CHECK(diff.inserted == 1);
}

TEST_CASE("Emptying a list removes all of it", "[models][rowdiff]")
{
    const auto diff = rpi_model::planRowDiff({"a", "b", "c"}, {});

    CHECK(diff.at == 0);
    CHECK(diff.removed == 3);
    CHECK(diff.inserted == 0);
}

TEST_CASE("Filling an empty list inserts all of it", "[models][rowdiff]")
{
    const auto diff = rpi_model::planRowDiff({}, {"a", "b"});

    CHECK(diff.at == 0);
    CHECK(diff.removed == 0);
    CHECK(diff.inserted == 2);
}

TEST_CASE("Two empty lists have no difference", "[models][rowdiff]")
{
    CHECK(rpi_model::planRowDiff({}, {}).isEmpty());
}

TEST_CASE("A repeated key is not mistaken for the row before it",
          "[models][rowdiff]")
{
    // Keys are not required to be unique -- two entries can share a name and
    // an empty url -- so the walk must compare positions rather than search.
    const auto diff = rpi_model::planRowDiff({"a", "a", "b"}, {"a", "b"});

    CHECK(diff.removed == 1);
    CHECK(diff.inserted == 0);
}

TEST_CASE("A list that swapped ends replaces the part that moved",
          "[models][rowdiff]")
{
    // Not minimal, and deliberately so: a reorder is reported as replacing
    // the run that moved. Nothing here reorders, and doing better would mean
    // a full edit-distance pass. What matters is that it is correct.
    const auto diff = rpi_model::planRowDiff({"a", "b", "c"}, {"c", "b", "a"});

    CHECK(diff.at == 0);
    CHECK(diff.removed == 3);
    CHECK(diff.inserted == 3);
}

// ══════════════════════════════════════════════════════════════
// Rebuilding the OS list without throwing the view away
//
// reload() used to reset the model. A reset destroys every delegate in the
// view, and Qt delivers a click only when the press and the release reach
// the same item -- so a list rebuilt between the two swallows the click
// entirely. The user presses a row, the list refills, nothing is selected,
// and they have to click again. Reported by users as the OS list sometimes
// needing two clicks, on a first-level entry or on "Use custom".
//
// That window is not rare. Until the OS list arrives the model holds
// exactly two rows -- "Erase" and "Use custom", appended unconditionally --
// and the arrival of the real list is what rebuilds it. Anyone who reaches
// the screen before the fetch lands is clicking on a list that is about to
// be replaced.
//
// So reload() now reports the difference. The rows that were already there
// keep their delegates and shift down as the real entries are inserted
// above them, which also keeps the scroll position and the highlight.

namespace {

// A list whose entries are all real, so the two internal rows are appended
// after them.
QByteArray twoRealEntriesJson()
{
    return QByteArray(R"JSON({
        "imager": { "devices": [] },
        "os_list": [
            { "name": "Alpha OS", "description": "the first",
              "url": "https://example.invalid/alpha.img.xz",
              "icon": "", "release_date": "2025-01-01",
              "extract_size": 100, "image_download_size": 50,
              "extract_sha256": "aa" },
            { "name": "Beta OS", "description": "the second",
              "url": "https://example.invalid/beta.img.xz",
              "icon": "", "release_date": "2025-01-02",
              "extract_size": 100, "image_download_size": 50,
              "extract_sha256": "bb" }
        ]
    })JSON");
}

QStringList namesIn(QAbstractItemModel &model)
{
    QStringList out;
    const QHash<int, QByteArray> names = model.roleNames();
    int nameRole = -1;
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == "name")
            nameRole = it.key();
    }
    for (int i = 0; i < model.rowCount(QModelIndex()); ++i)
        out << model.data(model.index(i, 0), nameRole).toString();
    return out;
}

} // namespace

TEST_CASE("Before the list arrives there are two rows to click on",
          "[models][oslist]")
{
    // The premise of everything below: this state exists and is reachable.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model != nullptr);
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    CHECK(namesIn(*view) == QStringList{QStringLiteral("Erase"),
                                        QStringLiteral("Use custom")});
}

TEST_CASE("The list arriving does not throw the view away", "[models][oslist]")
{
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model != nullptr);
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    REQUIRE(view->rowCount(QModelIndex()) == 2);

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);

    writer.feedOsList(twoRealEntriesJson());
    REQUIRE(model->reload());

    CHECK(reset.count() == 0);  // the delegates, and any click in flight, survive
    CHECK(removed.count() == 0);
    REQUIRE(inserted.count() == 1);

    // Inserted above the two that were already there, which is what lets
    // them keep their delegates.
    CHECK(inserted.at(0).at(1).toInt() == 0);
    CHECK(inserted.at(0).at(2).toInt() == 1);

    CHECK(namesIn(*view) == QStringList{QStringLiteral("Alpha OS"),
                                        QStringLiteral("Beta OS"),
                                        QStringLiteral("Erase"),
                                        QStringLiteral("Use custom")});
}

TEST_CASE("Reloading the same list changes nothing at all", "[models][oslist]")
{
    // The periodic refresh, and every cache-status update behind it. A reset
    // here would drop the user's scroll position and highlight on a timer.
    TestableImageWriter writer;
    writer.feedOsList(twoRealEntriesJson());
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    const QStringList before = namesIn(*view);

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);
    rpi_test::SignalLog changed(view, &QAbstractItemModel::dataChanged);

    REQUIRE(model->reload());

    CHECK(reset.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(removed.count() == 0);
    CHECK(changed.count() == 0);
    CHECK(namesIn(*view) == before);
}

// The cases below build the rows by hand and hand them to the model.
// Feeding a second list through the fetch does not replace the first one --
// it merges into it, which is how sublists arrive -- so it cannot express
// "the list is now different". What is under test here is the difference
// being reported correctly, not how the list came to change.

namespace {

OSListModel::OS osRow(const QString &name, const QString &url,
                      const QString &description = QStringLiteral("something"))
{
    OSListModel::OS os;
    os.name = name;
    os.url = url;
    os.description = description;
    return os;
}

} // namespace

TEST_CASE("A list that loses an entry reports the removal", "[models][oslist]")
{
    // What a hardware filter change does. The rows either side of the one
    // that went keep their delegates, so a click on one of them survives.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    REQUIRE(model != nullptr);
    QAbstractItemModel *view = model;

    model->applyRows({osRow("Alpha", "a"), osRow("Beta", "b"),
                      osRow("Erase", "internal://format"),
                      osRow("Use custom", "internal://custom")});
    REQUIRE(view->rowCount(QModelIndex()) == 4);

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);

    model->applyRows({osRow("Alpha", "a"),
                      osRow("Erase", "internal://format"),
                      osRow("Use custom", "internal://custom")});

    CHECK(reset.count() == 0);
    CHECK(inserted.count() == 0);
    REQUIRE(removed.count() == 1);
    CHECK(removed.at(0).at(1).toInt() == 1);  // Beta, the second row
    CHECK(removed.at(0).at(2).toInt() == 1);
    CHECK(namesIn(*view) == QStringList{QStringLiteral("Alpha"),
                                        QStringLiteral("Erase"),
                                        QStringLiteral("Use custom")});
}

TEST_CASE("A row whose contents changed is reported as changed, not replaced",
          "[models][oslist]")
{
    // A description or a size moving is not a reason to rebuild the row.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    QAbstractItemModel *view = model;
    model->applyRows({osRow("Alpha", "a", "the first"),
                      osRow("Beta", "b", "the second")});

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);
    rpi_test::SignalLog changed(view, &QAbstractItemModel::dataChanged);

    model->applyRows({osRow("Alpha", "a", "now says something else"),
                      osRow("Beta", "b", "the second")});

    CHECK(reset.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(removed.count() == 0);
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).toModelIndex().row() == 0);
}

TEST_CASE("An entry appearing in the middle keeps the rows around it",
          "[models][oslist]")
{
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    QAbstractItemModel *view = model;
    model->applyRows({osRow("Alpha", "a"), osRow("Gamma", "g")});

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);

    model->applyRows({osRow("Alpha", "a"), osRow("Beta", "b"),
                      osRow("Gamma", "g")});

    CHECK(reset.count() == 0);
    CHECK(removed.count() == 0);
    REQUIRE(inserted.count() == 1);
    CHECK(inserted.at(0).at(1).toInt() == 1);
    CHECK(inserted.at(0).at(2).toInt() == 1);
}

TEST_CASE("Two entries that differ only by url are different rows",
          "[models][oslist]")
{
    // The key is the url and the name together. Two builds of the same
    // release share a name and differ by url, and treating them as one row
    // would leave the view showing one and the model holding the other.
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    QAbstractItemModel *view = model;
    model->applyRows({osRow("Raspberry Pi OS", "a.img.xz")});

    rpi_test::SignalLog changed(view, &QAbstractItemModel::dataChanged);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);

    model->applyRows({osRow("Raspberry Pi OS", "b.img.xz")});

    CHECK(removed.count() == 1);
    CHECK(inserted.count() == 1);
    CHECK(changed.count() == 0);
}

TEST_CASE("Emptying the list removes every row", "[models][oslist]")
{
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();
    QAbstractItemModel *view = model;
    model->applyRows({osRow("Alpha", "a"), osRow("Beta", "b")});

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);

    model->applyRows({});

    CHECK(reset.count() == 0);
    REQUIRE(removed.count() == 1);
    CHECK(view->rowCount(QModelIndex()) == 0);
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

// The chooser is a QML list view, so every field it shows is fetched by
// asking the model for a role by name. roleNames() and data() had never
// run: a role missing from the map, or a case missing from the switch,
// leaves that line of the device blank on screen with nothing in a log to
// say so. The board a user picks is what filters the whole OS list, so the
// description they are picking by has to be the right one.

namespace {

QByteArray hwListJson()
{
    return QByteArray(R"JSON({
        "imager": {
            "devices": [
                {
                    "name": "Raspberry Pi 5",
                    "tags": ["pi5-64bit"],
                    "capabilities": ["secure-boot"],
                    "icon": "icons/pi5.png",
                    "description": "The one with the fan header",
                    "matching_type": "exclusive",
                    "architecture": "arm64",
                    "default": true
                },
                {
                    "name": "Raspberry Pi 4",
                    "tags": ["pi4-64bit"],
                    "capabilities": [],
                    "icon": "https://example.invalid/pi4.png",
                    "description": "The one before that",
                    "matching_type": "inclusive",
                    "architecture": "arm64"
                }
            ]
        },
        "os_list": []
    })JSON");
}

int roleFor(const QAbstractItemModel &model, const QByteArray &name)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        if (it.value() == name)
            return it.key();
    }
    return -1;
}

} // namespace

TEST_CASE("Every field the chooser shows can be asked for by name",
          "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    REQUIRE(view->rowCount(QModelIndex()) == 2);

    // Everything the view can name has to come back with something. A role
    // in the map that the switch does not answer is a blank line on screen.
    const QHash<int, QByteArray> names = view->roleNames();
    REQUIRE(!names.isEmpty());
    QStringList unanswered;
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        const QVariant value = view->data(view->index(0, 0), it.key());
        if (!value.isValid())
            unanswered << QString::fromUtf8(it.value());
    }
    CHECK(unanswered.join(QStringLiteral(", ")).toStdString() == std::string());
}

TEST_CASE("The chooser shows what the feed said", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    const QModelIndex first = view->index(0, 0);

    CHECK(view->data(first, roleFor(*view, "name")).toString()
          == QStringLiteral("Raspberry Pi 5"));
    CHECK(view->data(first, roleFor(*view, "description")).toString()
          == QStringLiteral("The one with the fan header"));
    CHECK(view->data(first, roleFor(*view, "architecture")).toString()
          == QStringLiteral("arm64"));
    CHECK(view->data(first, roleFor(*view, "matching_type")).toString()
          == QStringLiteral("exclusive"));
    CHECK(view->data(first, roleFor(*view, "tags")).toJsonArray().size() == 1);
    CHECK(view->data(first, roleFor(*view, "capabilities")).toJsonArray().size() == 1);
}

TEST_CASE("A row that is not there is answered with nothing",
          "[models][hwlist]")
{
    // A view can ask past the end while a reload is in flight. Reading the
    // list out of bounds is worse than an empty cell.
    //
    // Not reversion-checked: removing the bounds check does not produce a
    // wrong answer, it indexes a QList out of range, which is undefined
    // rather than observable. The case is here for what it pins, not for a
    // failure it was watched to produce.
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    const int nameRole = roleFor(*view, "name");

    CHECK_FALSE(view->data(view->index(-1, 0), nameRole).isValid());
    CHECK_FALSE(view->data(view->index(2, 0), nameRole).isValid());
    CHECK_FALSE(view->data(view->index(99, 0), nameRole).isValid());
}

TEST_CASE("A remote board icon is fetched through the image provider",
          "[models][hwlist]")
{
    // Straight to the network the icons come back over HTTP/2 and fail; the
    // provider is what avoids that. A board with no icon on the chooser is
    // a row the user cannot tell apart from the next one.
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    const int iconRole = roleFor(*view, "icon");

    CHECK(view->data(view->index(1, 0), iconRole).toString()
          == QStringLiteral("image://icons/https://example.invalid/pi4.png"));
}

TEST_CASE("A bundled board icon is found from where the chooser lives",
          "[models][hwlist]")
{
    // The feed names it relative to the application; the chooser is a
    // directory further in.
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    CHECK(view->data(view->index(0, 0), roleFor(*view, "icon")).toString()
          == QStringLiteral("../icons/pi5.png"));
}

TEST_CASE("A board icon naming a host is dropped", "[models][hwlist][icon]")
{
    // The board list is filled from the same repository json as the OS list,
    // and a repository is not necessarily trusted -- it can arrive from
    // --repo, from the repository dialog, or from an rpi-imager:// link.
    // file://host/share/icon.png resolves to the UNC path
    // //host/share/icon.png, and an Image pointed at that on Windows reaches
    // out to the host over SMB.
    //
    // The OS list had been sanitising icons for exactly this reason and the
    // board list had not, so the same entry was checked in one list and
    // waved through in the other.
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({
        "imager": {
            "devices": [
                {
                    "name": "Raspberry Pi 5",
                    "tags": ["pi5-64bit"],
                    "capabilities": [],
                    "icon": "file://evil.example/share/pi5.png",
                    "description": "With an icon from somewhere else",
                    "matching_type": "exclusive",
                    "architecture": "arm64",
                    "default": true
                }
            ]
        },
        "os_list": []
    })JSON"));
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    // No icon rather than a fetch of somewhere else. The board is still
    // listed: a row without a picture is worth having, a row that phones
    // home is not.
    CHECK(view->data(view->index(0, 0), roleFor(*view, "icon")).toString().isEmpty());
    CHECK(view->data(view->index(0, 0), roleFor(*view, "name")).toString()
          == QStringLiteral("Raspberry Pi 5"));
}

TEST_CASE("The board list arriving does not throw the view away",
          "[models][hwlist]")
{
    // The same reason as the OS list: a reset destroys every delegate, and
    // any click in progress with it. The board chooser is refreshed by the
    // same osListPrepared that refreshes the OS list, so a refresh landing
    // while the user is clicking a board used to lose the click.
    TestableImageWriter writer;
    HWListModel *model = writer.getHWList();
    REQUIRE(model != nullptr);
    QAbstractItemModel *view = model;

    HWListModel::HardwareDevice pi5;
    pi5.name = QStringLiteral("Raspberry Pi 5");
    HWListModel::HardwareDevice pi4;
    pi4.name = QStringLiteral("Raspberry Pi 4");
    model->applyRows({pi5, pi4});
    REQUIRE(view->rowCount(QModelIndex()) == 2);

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);

    HWListModel::HardwareDevice pi3;
    pi3.name = QStringLiteral("Raspberry Pi 3");
    model->applyRows({pi5, pi4, pi3});

    CHECK(reset.count() == 0);
    CHECK(removed.count() == 0);
    REQUIRE(inserted.count() == 1);
    CHECK(inserted.at(0).at(1).toInt() == 2);
}

TEST_CASE("Reloading the same board list changes nothing", "[models][hwlist]")
{
    TestableImageWriter writer;
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    HWListModel::HardwareDevice pi5;
    pi5.name = QStringLiteral("Raspberry Pi 5");
    model->applyRows({pi5});

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog inserted(view, &QAbstractItemModel::rowsInserted);
    rpi_test::SignalLog removed(view, &QAbstractItemModel::rowsRemoved);
    rpi_test::SignalLog changed(view, &QAbstractItemModel::dataChanged);

    model->applyRows({pi5});

    CHECK(reset.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(removed.count() == 0);
    CHECK(changed.count() == 0);
}

TEST_CASE("A board whose description changed is reported as changed",
          "[models][hwlist]")
{
    TestableImageWriter writer;
    HWListModel *model = writer.getHWList();
    QAbstractItemModel *view = model;
    HWListModel::HardwareDevice before;
    before.name = QStringLiteral("Raspberry Pi 5");
    before.description = QStringLiteral("the one with the fan header");
    model->applyRows({before});

    rpi_test::SignalLog reset(view, &QAbstractItemModel::modelAboutToBeReset);
    rpi_test::SignalLog changed(view, &QAbstractItemModel::dataChanged);

    HWListModel::HardwareDevice after = before;
    after.description = QStringLiteral("now says something else");
    model->applyRows({after});

    CHECK(reset.count() == 0);
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).toModelIndex().row() == 0);
}

TEST_CASE("A board is marked attached only when its own chip is",
          "[models][hwlist]")
{
    // The chooser marks the board that is plugged in over USB. Marking the
    // wrong one sends the user down the rpiboot path for hardware that is
    // not there, and leaves the board that is there looking unavailable.
    TestableImageWriter writer;
    writer.feedOsList(hwListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;
    const int attachedRole = roleFor(*view, "isUsbBootConnected");
    const QModelIndex pi5 = view->index(0, 0);
    const QModelIndex pi4 = view->index(1, 0);

    SECTION("nothing attached") {
        CHECK_FALSE(view->data(pi5, attachedRole).toBool());
        CHECK_FALSE(view->data(pi4, attachedRole).toBool());
    }

    SECTION("a Pi 5 attached") {
        model->setConnectedRpibootChips({QStringLiteral("BCM2712")});
        CHECK(view->data(pi5, attachedRole).toBool());
        CHECK_FALSE(view->data(pi4, attachedRole).toBool());
    }

    SECTION("a Pi 4 attached") {
        model->setConnectedRpibootChips({QStringLiteral("BCM2711")});
        CHECK_FALSE(view->data(pi5, attachedRole).toBool());
        CHECK(view->data(pi4, attachedRole).toBool());
    }

    SECTION("a chip nothing on the list is built on") {
        model->setConnectedRpibootChips({QStringLiteral("BCM2837B0-not-a-thing")});
        CHECK_FALSE(view->data(pi5, attachedRole).toBool());
        CHECK_FALSE(view->data(pi4, attachedRole).toBool());
    }

    SECTION("unplugged again") {
        model->setConnectedRpibootChips({QStringLiteral("BCM2712")});
        REQUIRE(view->data(pi5, attachedRole).toBool());

        model->setConnectedRpibootChips({});

        CHECK_FALSE(view->data(pi5, attachedRole).toBool());
    }
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

// ══════════════════════════════════════════════════════════════
// What the drive chooser actually shows
//
// processDriveList() is a public slot, so the model can be handed a device
// list directly rather than waiting on the poller and whatever happens to be
// plugged into the machine running the tests.
//
// This is the list somebody picks a target from. A drive that should not be
// there is how the wrong disk gets overwritten; one that is missing is a
// card the user cannot write to and has no way to diagnose.
// ══════════════════════════════════════════════════════════════

namespace {

Drivelist::DeviceDescriptor makeDevice(const std::string &path,
                                       const std::string &description,
                                       std::uint64_t size)
{
    Drivelist::DeviceDescriptor d;
    d.device = path;
    d.description = description;
    d.size = size;
    d.isUSB = true;
    d.isRemovable = true;
    d.isSystem = false;
    d.isVirtual = false;
    d.isReadOnly = false;
    return d;
}

int rowsOf(QAbstractItemModel *m) { return m->rowCount(QModelIndex()); }

QVariant roleOfRow(QAbstractItemModel *m, int row, const char *roleName)
{
    const auto roles = m->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == QByteArray(roleName))
            return m->data(m->index(row, 0), it.key());
    return {};
}

QStringList devicePathsIn(QAbstractItemModel *m)
{
    QStringList paths;
    const auto roles = m->roleNames();
    int deviceRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == QByteArrayLiteral("device"))
            deviceRole = it.key();
    if (deviceRole < 0)
        return paths;
    for (int row = 0; row < m->rowCount(QModelIndex()); ++row)
        paths << m->data(m->index(row, 0), deviceRole).toString();
    return paths;
}

} // namespace

TEST_CASE("A plugged-in drive appears in the chooser", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();
    QAbstractItemModel *view = model;

    model->processDriveList({makeDevice("/dev/sdz", "SanDisk Cruzer", 32000000000ull)});

    CHECK(rowsOf(view) == 1);
    CHECK(devicePathsIn(view).contains(QStringLiteral("/dev/sdz")));
}

TEST_CASE("A zero-sized device is not offered", "[models][drivelist]")
{
    // An empty card reader reports zero. Offering it gives the user a target
    // that cannot be written.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    model->processDriveList({makeDevice("/dev/sdz", "Empty reader", 0)});
    CHECK(rowsOf(model) == 0);
}

TEST_CASE("A drive carrying the running system is not offered",
          "[models][drivelist]")
{
    // The last line of defence in front of the user. A virtual device that
    // is also a system device must never reach the chooser.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    auto sys = makeDevice("/dev/loop-system", "System image", 32000000000ull);
    sys.isVirtual = true;
    sys.isSystem = true;

    model->processDriveList({sys});
    CHECK(rowsOf(model) == 0);
}

TEST_CASE("A read-only virtual device is not offered", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    auto ro = makeDevice("/dev/loop-ro", "Mounted ISO", 700000000ull);
    ro.isVirtual = true;
    ro.isReadOnly = true;

    model->processDriveList({ro});
    CHECK(rowsOf(model) == 0);
}

TEST_CASE("A writable loopback image is offered", "[models][drivelist]")
{
    // Writing to a disk image is a supported thing to do, and on Linux a
    // loop device is never marked removable -- so requiring removable here
    // would hide every one of them.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    auto loop = makeDevice("/dev/loop42", "Disk image", 8000000000ull);
    loop.isVirtual = true;
    loop.isRemovable = false;

    model->processDriveList({loop});
    CHECK(rowsOf(model) == 1);
}

TEST_CASE("Several drives all appear", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    model->processDriveList({
        makeDevice("/dev/sdx", "First", 16000000000ull),
        makeDevice("/dev/sdy", "Second", 32000000000ull),
        makeDevice("/dev/sdz", "Third", 64000000000ull),
    });

    const QStringList paths = devicePathsIn(model);
    INFO("offered: " << paths.join(QStringLiteral(", ")).toStdString());
    CHECK(rowsOf(model) == 3);
    CHECK(paths.contains(QStringLiteral("/dev/sdy")));
}

TEST_CASE("Unplugging a drive removes it and says so", "[models][drivelist]")
{
    // The signal is what lets a write in progress notice its target has
    // gone, rather than carrying on writing to a device that is not there.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    QStringList removed;
    QObject::connect(model, &DriveListModel::deviceRemoved,
                     [&removed](const QString &d) { removed << d; });

    model->processDriveList({
        makeDevice("/dev/sdx", "Stays", 16000000000ull),
        makeDevice("/dev/sdy", "Goes", 32000000000ull),
    });
    REQUIRE(rowsOf(model) == 2);

    model->processDriveList({makeDevice("/dev/sdx", "Stays", 16000000000ull)});

    CHECK(rowsOf(model) == 1);
    CHECK(devicePathsIn(model).contains(QStringLiteral("/dev/sdx")));
    INFO("removed: " << removed.join(QStringLiteral(", ")).toStdString());
    CHECK(removed.contains(QStringLiteral("/dev/sdy")));
}

TEST_CASE("An enumeration failure is reported and then cleared",
          "[models][drivelist]")
{
    // The poller signals failure through a sentinel entry. The user needs to
    // be told the list is unreliable -- an empty chooser with no explanation
    // reads as "no drives attached".
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    QStringList errors;
    QObject::connect(model, &DriveListModel::enumerationError,
                     [&errors](const QString &e) { errors << e; });

    Drivelist::DeviceDescriptor sentinel;
    sentinel.device = "__error__";
    sentinel.error = "lsblk not found";
    model->processDriveList({sentinel});

    REQUIRE_FALSE(errors.isEmpty());
    CHECK(errors.last() == QStringLiteral("lsblk not found"));

    // A later good poll clears it.
    model->processDriveList({makeDevice("/dev/sdz", "Recovered", 16000000000ull)});
    CHECK(errors.last().isEmpty());
    CHECK(rowsOf(model) == 1);
}

TEST_CASE("The same drive reported twice is listed once",
          "[models][drivelist]")
{
    // Successive polls return the same devices; the chooser must not grow a
    // duplicate entry each time.
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    const auto dev = makeDevice("/dev/sdz", "Same", 32000000000ull);
    model->processDriveList({dev});
    model->processDriveList({dev});
    model->processDriveList({dev});

    CHECK(rowsOf(model) == 1);
}

// ══════════════════════════════════════════════════════════════
// The drive poller's lifecycle
//
// The poll thread enumerates the machine's drives on a timer. What it finds
// depends on the machine, so none of this asserts on that -- but the
// lifecycle around it is the application's own logic and is worth pinning.
//
// Polling is paused for the duration of a write, because enumerating drives
// costs I/O and can disturb the device being written. If pause does not take
// effect the write is disturbed; if resume does not, the chooser stays frozen
// once the write finishes and the user cannot select anything again.
// ══════════════════════════════════════════════════════════════

TEST_CASE("The drive poller starts, reports and stops", "[models][poller]")
{
    DriveListModelPollThread poller;

    std::atomic<int> polls{0};
    QObject::connect(&poller, &DriveListModelPollThread::newDriveList,
                     [&polls](std::vector<Drivelist::DeviceDescriptor>) { ++polls; });

    poller.start();

    // Whatever this machine has attached, one enumeration should land.
    QElapsedTimer t;
    t.start();
    while (polls.load() == 0 && t.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    poller.stop();
    REQUIRE(poller.wait(30000));

    INFO("polls seen: " << polls.load());
    CHECK(polls.load() > 0);
}

TEST_CASE("Stopping a poller that never started is harmless",
          "[models][poller]")
{
    // Teardown runs whether or not a write ever began.
    DriveListModelPollThread poller;
    CHECK_NOTHROW(poller.stop());
    CHECK_NOTHROW(poller.stop());
}

TEST_CASE("A paused poller stops reporting and resumes on request",
          "[models][poller]")
{
    DriveListModelPollThread poller;

    std::atomic<int> polls{0};
    QObject::connect(&poller, &DriveListModelPollThread::newDriveList,
                     [&polls](std::vector<Drivelist::DeviceDescriptor>) { ++polls; });

    poller.start();
    QElapsedTimer t;
    t.start();
    while (polls.load() == 0 && t.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    REQUIRE(polls.load() > 0);

    poller.pause();
    // Let anything already in flight drain, then take a reading.
    t.restart();
    while (t.elapsed() < 1500)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const int afterPause = polls.load();

    t.restart();
    while (t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    INFO("polls at pause " << afterPause << ", after waiting " << polls.load());
    CHECK(polls.load() == afterPause);

    poller.resume();
    t.restart();
    while (polls.load() == afterPause && t.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    poller.stop();
    REQUIRE(poller.wait(30000));
    INFO("polls after resume: " << polls.load());
    CHECK(polls.load() > afterPause);
}

TEST_CASE("A poller can be stopped while paused", "[models][poller]")
{
    // Cancelling a write leaves polling paused; teardown has to get through
    // that rather than waiting on a thread that is not looking at its flag.
    DriveListModelPollThread poller;
    poller.start();
    poller.pause();
    poller.stop();
    CHECK(poller.wait(30000));
}

TEST_CASE("The poller's scan options can be set before it runs",
          "[models][poller]")
{
    // These decide whether rpiboot and fastboot devices are scanned for at
    // all, which is what makes a Compute Module appear in the chooser.
    DriveListModelPollThread poller;
    CHECK_NOTHROW(poller.setRpibootEnabled(true));
    CHECK_NOTHROW(poller.setFastbootScanEnabled(true));
    CHECK_NOTHROW(poller.setRpibootEnabled(false));
    CHECK_NOTHROW(poller.setFastbootScanEnabled(false));

    poller.start();
    poller.stop();
    CHECK(poller.wait(30000));
}

// ═══════════════════════════════════════════════════════════════════════════
// Marking the board that is actually plugged in
//
// When a Compute Module is attached over USB, the board chooser marks the
// entries that correspond to the chip it reports. QML reads that through the
// isUsbBootConnected role.
//
// Getting it wrong is quiet in both directions: an unmarked board leaves the
// user hunting for the device they can see is connected, and a wrongly
// marked one invites them to flash an image built for a different SoC.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

int roleNumbered(const QAbstractItemModel *model, const char *name)
{
    const QHash<int, QByteArray> roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        if (it.value() == QByteArray(name))
            return it.key();
    return -1;
}

// Row index of a board by name.
int rowNamed(QAbstractItemModel *view, const QString &name)
{
    const int nameRole = roleNumbered(view, "name");
    for (int r = 0; r < view->rowCount(QModelIndex()); ++r)
        if (view->data(view->index(r, 0), nameRole).toString() == name)
            return r;
    return -1;
}

bool markedConnected(QAbstractItemModel *view, const QString &name)
{
    const int row = rowNamed(view, name);
    REQUIRE(row >= 0);
    return view->data(view->index(row, 0), roleNumbered(view, "isUsbBootConnected")).toBool();
}

} // namespace

TEST_CASE("A connected chip marks its own board and no other", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    // Nothing attached: nothing marked.
    CHECK_FALSE(markedConnected(view, QStringLiteral("Raspberry Pi 5")));

    model->setConnectedRpibootChips({QStringLiteral("BCM2712")});

    // The Pi 5 entry is tagged pi5-64bit; the Zero 2 W is not a BCM2712.
    // Marking the wrong one invites a flash of an image for another SoC.
    CHECK(markedConnected(view, QStringLiteral("Raspberry Pi 5")));
    CHECK_FALSE(markedConnected(view, QStringLiteral("Raspberry Pi Zero 2 W")));
}

TEST_CASE("A different generation's chip marks nothing", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    model->setConnectedRpibootChips({QStringLiteral("BCM2711")});
    CHECK_FALSE(markedConnected(view, QStringLiteral("Raspberry Pi 5")));
}

TEST_CASE("An unrecognised chip marks nothing", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    // Better to mark no board than the wrong one.
    model->setConnectedRpibootChips({QStringLiteral("BCM9999")});
    CHECK_FALSE(markedConnected(view, QStringLiteral("Raspberry Pi 5")));
}

TEST_CASE("Disconnecting the device clears the mark", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    QAbstractItemModel *view = model;

    model->setConnectedRpibootChips({QStringLiteral("BCM2712")});
    REQUIRE(markedConnected(view, QStringLiteral("Raspberry Pi 5")));

    // Unplugging must not leave the chooser claiming the board is attached.
    model->setConnectedRpibootChips({});
    CHECK_FALSE(markedConnected(view, QStringLiteral("Raspberry Pi 5")));
}

TEST_CASE("The chooser is told when the connected device changes", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());

    int changes = 0;
    QObject::connect(model, &QAbstractItemModel::dataChanged,
                     [&changes](const QModelIndex &, const QModelIndex &, const QList<int> &) {
                         ++changes;
                     });

    model->setConnectedRpibootChips({QStringLiteral("BCM2712")});
    REQUIRE(changes == 1);

    // Setting the same list again is not a change; repainting every row on
    // each poll would be visible in the UI.
    model->setConnectedRpibootChips({QStringLiteral("BCM2712")});
    CHECK(changes == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Drives that must never be offered, and devices that are not drives
//
// The cases above cover the ordinary ones. These are the exclusions and the
// special cases, which is where the consequences are: a system disk that
// appears in the chooser is one mis-click from overwriting the machine the
// imager is running on, and a naked rpiboot device offered as storage is an
// entry that cannot be written to at all.
//
// Also the error path. Enumeration can fail transiently -- a USB reset, a
// permissions change -- and the list must not blank itself when it does, or
// the user's selection disappears mid-flow.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("A drive mounted at the root is never offered", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor root = makeDevice("/dev/sda", "System disk", 512000000000ull);
    root.mountpoints = {"/"};

    model->processDriveList({root, makeDevice("/dev/sdz", "Card reader", 32000000000ull)});

    // One mis-click away from overwriting the machine the imager runs on.
    const QStringList paths = devicePathsIn(model);
    CHECK_FALSE(paths.contains(QStringLiteral("/dev/sda")));
    CHECK(paths.contains(QStringLiteral("/dev/sdz")));
}

TEST_CASE("A system drive is offered but flagged for confirmation", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor sys = makeDevice("/dev/sda", "Internal", 512000000000ull);
    sys.isSystem = true;
    sys.isUSB = false;
    sys.isRemovable = false;

    model->processDriveList({sys});

    // Deliberate: an internal disk can be a legitimate target, so it is
    // listed rather than hidden -- but the row carries isSystem, which is
    // what QML binds the "are you sure" dialog to. Losing that flag is how a
    // machine's own disk gets overwritten without a warning.
    REQUIRE(rowsOf(model) == 1);
    CHECK(roleOfRow(model, 0, "isSystem").toBool());
}

TEST_CASE("A virtual system device is hidden outright", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor v = makeDevice("/dev/loop9", "Snap mount", 100000000ull);
    v.isVirtual = true;
    v.isSystem = true;

    // A snap or APFS system volume is never a card; there is nothing to
    // confirm.
    model->processDriveList({v});
    CHECK(rowsOf(model) == 0);
}

TEST_CASE("A plain removable drive is not flagged for confirmation", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    model->processDriveList({makeDevice("/dev/sdz", "Card reader", 32000000000ull)});

    // Flagging everything would train the user to click through the dialog.
    REQUIRE(rowsOf(model) == 1);
    CHECK_FALSE(roleOfRow(model, 0, "isSystem").toBool());
}

TEST_CASE("An enumeration failure is reported without clearing the list", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    model->processDriveList({makeDevice("/dev/sdz", "Card reader", 32000000000ull)});
    REQUIRE(rowsOf(model) == 1);

    Drivelist::DeviceDescriptor sentinel;
    sentinel.device = "__error__";
    sentinel.error = "permission denied";

    int errors = 0;
    QString reported;
    QObject::connect(model, &DriveListModel::enumerationError, model,
                     [&](QString m) { ++errors; reported = m; });

    model->processDriveList({sentinel});

    // Blanking the list on a transient failure loses the user's selection
    // in the middle of setting up a write.
    CHECK(rowsOf(model) == 1);
    CHECK(errors == 1);
    CHECK(reported == QStringLiteral("permission denied"));
    CHECK(model->lastError() == QStringLiteral("permission denied"));
}

TEST_CASE("The same enumeration failure is not reported twice", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor sentinel;
    sentinel.device = "__error__";
    sentinel.error = "permission denied";

    model->processDriveList({sentinel});

    int errors = 0;
    QObject::connect(model, &DriveListModel::enumerationError, model,
                     [&](QString) { ++errors; });

    // The poll repeats every second; one banner, not sixty a minute.
    model->processDriveList({sentinel});
    CHECK(errors == 0);
}

TEST_CASE("A recovered enumeration clears the error", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor sentinel;
    sentinel.device = "__error__";
    sentinel.error = "permission denied";
    model->processDriveList({sentinel});
    REQUIRE_FALSE(model->lastError().isEmpty());

    QStringList reported;
    QObject::connect(model, &DriveListModel::enumerationError, model,
                     [&](QString m) { reported << m; });

    model->processDriveList({makeDevice("/dev/sdz", "Card reader", 32000000000ull)});

    // The banner has to go away by itself once scanning works again.
    CHECK(model->lastError().isEmpty());
    REQUIRE(reported.size() == 1);
    CHECK(reported.at(0).isEmpty());
    CHECK(rowsOf(model) == 1);
}

TEST_CASE("A naked rpiboot device is announced but not offered", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor rpiboot = makeDevice("rpiboot://1:5", "Compute Module", 0);
    rpiboot.isRpiboot = true;
    rpiboot.rpibootPid = 0x2712;

    int announced = 0;
    QString seenUri;
    quint8 seenBus = 0, seenAddr = 0;
    QObject::connect(model, &DriveListModel::rpibootDeviceDetected, model,
                     [&](QString uri, quint8 bus, quint8 addr, QList<uint8_t>, quint16) {
                         ++announced;
                         seenUri = uri;
                         seenBus = bus;
                         seenAddr = addr;
                     });

    model->processDriveList({rpiboot});

    // It is a device state to react to, not somewhere to write. Listing it
    // gives the user a target that cannot accept an image.
    CHECK(rowsOf(model) == 0);
    REQUIRE(announced == 1);
    CHECK(seenUri == QStringLiteral("rpiboot://1:5"));
    CHECK(seenBus == 1);
    CHECK(seenAddr == 5);
}

TEST_CASE("An rpiboot device already seen is not announced again", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor rpiboot = makeDevice("rpiboot://1:5", "Compute Module", 0);
    rpiboot.isRpiboot = true;

    model->processDriveList({rpiboot});

    int announced = 0;
    QObject::connect(model, &DriveListModel::rpibootDeviceDetected, model,
                     [&](QString, quint8, quint8, QList<uint8_t>, quint16) { ++announced; });

    // Announcing on every poll would re-trigger auto-bootstrap once a second.
    model->processDriveList({rpiboot});
    CHECK(announced == 0);
}

TEST_CASE("A fastboot storage target is offered even at zero size", "[models][drivelist]")
{
    TestableImageWriter writer;
    DriveListModel *model = writer.getDriveList();

    Drivelist::DeviceDescriptor fb = makeDevice("fastboot://1:6", "CM5 eMMC", 0);
    fb.isFastbootStorage = true;

    // Zero-sized devices are normally dropped as empty readers, but a
    // fastboot target reports no size and is still writable.
    model->processDriveList({fb});
    CHECK(rowsOf(model) == 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Building the board chooser, and what selecting a board does
//
// reload() turns the repository's "devices" array into the list of boards.
// It rewrites icon paths on the way, because the manifest gives them
// relative to the repository and the wizard needs them relative to itself --
// a rewrite that goes wrong is a chooser full of boards with no pictures.
//
// setCurrentIndex() is the other half: choosing a board sets the filter that
// decides which images are offered, and clears the image already picked when
// the board actually changes. Not clearing it leaves an image selected that
// the new board cannot boot.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

QByteArray hwListWith(const QString &deviceFields)
{
    return QStringLiteral(R"JSON({
        "imager": { "devices": [ %1 ] },
        "os_list": []
    })JSON").arg(deviceFields).toUtf8();
}

} // namespace

TEST_CASE("A devices entry that is not an array leaves the chooser empty", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({"imager":{"devices":"not-an-array"},"os_list":[]})JSON"));
    HWListModel *model = writer.getHWList();

    // Returning false rather than throwing: the OS list may simply not have
    // arrived yet, and the wizard re-asks.
    CHECK_FALSE(model->reload());
    CHECK(rowsOf(model) == 0);
}

TEST_CASE("A repository icon path is rewritten for the wizard", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(hwListWith(QStringLiteral(
        R"({"name":"Pi 5","tags":["pi5-64bit"],"capabilities":[],"icon":"icons/pi5.png","architecture":"arm64"})")));
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());

    // The manifest gives it relative to the repository root; the wizard is a
    // directory deeper.
    CHECK(roleOfRow(model, 0, "icon").toString() == QStringLiteral("../icons/pi5.png"));
}

TEST_CASE("A remote icon is routed through the image provider", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(hwListWith(QStringLiteral(
        R"({"name":"Pi 5","tags":[],"capabilities":[],"icon":"https://example.com/pi5.png","architecture":"arm64"})")));
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());

    // Loading it directly hits Qt's HTTP/2 stack; the provider is the
    // fetcher with the shared cache behind it.
    CHECK(roleOfRow(model, 0, "icon").toString()
          == QStringLiteral("image://icons/https://example.com/pi5.png"));
}

TEST_CASE("An icon path that needs no rewriting is left alone", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(hwListWith(QStringLiteral(
        R"({"name":"Pi 5","tags":[],"capabilities":[],"icon":"qrc:/icons/pi5.png","architecture":"arm64"})")));
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());

    CHECK(roleOfRow(model, 0, "icon").toString() == QStringLiteral("qrc:/icons/pi5.png"));
}

TEST_CASE("Reloading replaces the boards rather than appending", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();

    REQUIRE(model->reload());
    const int first = rowsOf(model);
    REQUIRE(first > 0);

    // Stepping back and forward through the wizard re-enters this; appending
    // would show every board twice.
    REQUIRE(model->reload());
    CHECK(rowsOf(model) == first);
}

TEST_CASE("Selecting a board out of range changes nothing", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());

    const int before = model->currentIndex();
    model->setCurrentIndex(9999);
    CHECK(model->currentIndex() == before);

    model->setCurrentIndex(-2);
    CHECK(model->currentIndex() == before);
}

TEST_CASE("Clearing the board selection is allowed", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    REQUIRE(model->currentIndex() >= 0);

    int changes = 0;
    QObject::connect(model, &HWListModel::currentIndexChanged, model, [&] { ++changes; });

    const QString chosen = model->currentName();
    model->setCurrentIndex(-1);

    CHECK(model->currentIndex() == -1);
    // The button falls back to its placeholder rather than keeping the name
    // of a board that is no longer selected.
    CHECK(model->currentName() != chosen);
    CHECK(model->currentName() == QStringLiteral("CHOOSE DEVICE"));
    CHECK(changes == 1);
}

TEST_CASE("Re-selecting the same board is not a change", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    const int current = model->currentIndex();
    REQUIRE(current >= 0);

    int changes = 0;
    QObject::connect(model, &HWListModel::currentIndexChanged, model, [&] { ++changes; });

    model->setCurrentIndex(current);
    CHECK(changes == 0);
}

TEST_CASE("Choosing a different board drops the image already picked", "[models][hwlist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListJson());
    HWListModel *model = writer.getHWList();
    REQUIRE(model->reload());
    REQUIRE(rowsOf(model) >= 2);

    writer.setSrc(QUrl(QStringLiteral("https://example.invalid/pi5.img.xz")), 0, 1024);
    REQUIRE_FALSE(writer.srcFileName().isEmpty());

    // The image was chosen for the previous board and may not boot on this
    // one. Leaving it selected is how the wrong image gets written.
    const int other = (model->currentIndex() == 0) ? 1 : 0;
    model->setCurrentIndex(other);

    CHECK(writer.srcFileName().isEmpty());
}

// ═══════════════════════════════════════════════════════════════════════════
// The OS chooser's contents
//
// reload() turns the filtered list into rows: names, sizes, icons, and the
// "(Recommended)" label on whichever entry ends up first after the
// architecture sort.
//
// The label is worth pinning because it moves. Sorting for a different board
// changes which entry is first, and the old label has to come off before the
// new one goes on -- two entries claiming to be recommended, or none, is what
// the user sees when that fails. It must also never land on Erase or Use
// custom, which are fallbacks rather than operating systems.
// ═══════════════════════════════════════════════════════════════════════════

namespace {

QByteArray osListWithEntries(const QString &entries)
{
    return QStringLiteral(R"JSON({
        "imager": { "devices": [] },
        "os_list": [ %1 ]
    })JSON").arg(entries).toUtf8();
}

QString osEntry(const QString &name, const QString &extra = {})
{
    return QStringLiteral(R"({"name":"%1","description":"An operating system",)"
                          R"("url":"https://example.invalid/%1.img.xz",)"
                          R"("image_download_size":100,"extract_size":200%2})")
        .arg(name, extra.isEmpty() ? QString() : QStringLiteral(",") + extra);
}

int rowNamedInOsList(QAbstractItemModel *view, const QString &name)
{
    for (int r = 0; r < view->rowCount(QModelIndex()); ++r)
        if (roleOfRow(view, r, "name").toString() == name)
            return r;
    return -1;
}

} // namespace

TEST_CASE("With no OS list the chooser still offers the built-ins", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(QByteArray(R"JSON({"imager":{},"os_list":[]})JSON"));
    OSListModel *model = writer.getOSList();

    // getFilteredOSlistDocument() always appends Erase and Use custom, so an
    // empty repository list still reloads successfully. That is what lets a
    // user with no network format a card or write their own image.
    REQUIRE(model->reload());
    REQUIRE(rowsOf(model) == 2);

    QStringList urls;
    for (int r = 0; r < rowsOf(model); ++r)
        urls << roleOfRow(model, r, "url").toString();
    CHECK(urls.contains(QStringLiteral("internal://format")));
    CHECK(urls.contains(QStringLiteral("internal://custom")));
}

TEST_CASE("The first operating system is marked recommended", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(osEntry(QStringLiteral("Alpha")) + QStringLiteral(",")
                                        + osEntry(QStringLiteral("Beta"))));
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const int alpha = rowNamedInOsList(model, QStringLiteral("Alpha"));
    const int beta = rowNamedInOsList(model, QStringLiteral("Beta"));
    REQUIRE(alpha >= 0);
    REQUIRE(beta >= 0);

    CHECK(roleOfRow(model, alpha, "description").toString().contains(QStringLiteral("Recommended")));
    CHECK_FALSE(roleOfRow(model, beta, "description").toString().contains(QStringLiteral("Recommended")));
}

TEST_CASE("Exactly one entry is ever marked recommended", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(osEntry(QStringLiteral("Alpha")) + QStringLiteral(",")
                                        + osEntry(QStringLiteral("Beta")) + QStringLiteral(",")
                                        + osEntry(QStringLiteral("Gamma"))));
    OSListModel *model = writer.getOSList();

    // Reloading happens every time the board changes. The previous label has
    // to be stripped, or the list accumulates them.
    REQUIRE(model->reload());
    REQUIRE(model->reload());
    REQUIRE(model->reload());

    int marked = 0;
    for (int r = 0; r < rowsOf(model); ++r)
        if (roleOfRow(model, r, "description").toString().contains(QStringLiteral("Recommended")))
            ++marked;
    CHECK(marked == 1);
}

TEST_CASE("Erase and Use custom are never recommended", "[models][oslist]")
{
    TestableImageWriter writer;
    // No real entries at all: the built-ins are all that is left.
    writer.feedOsList(QByteArray(R"JSON({"imager":{},"os_list":[]})JSON"));
    OSListModel *model = writer.getOSList();
    model->reload();

    for (int r = 0; r < rowsOf(model); ++r) {
        const QString url = roleOfRow(model, r, "url").toString();
        if (!url.startsWith(QStringLiteral("internal://")))
            continue;
        INFO("row: " << roleOfRow(model, r, "name").toString().toStdString());
        // Recommending "Format card as FAT32" would be actively misleading.
        CHECK_FALSE(roleOfRow(model, r, "description").toString()
                        .contains(QStringLiteral("Recommended")));
    }
}

TEST_CASE("A remote OS icon is routed through the image provider", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(
        osEntry(QStringLiteral("Alpha"), QStringLiteral(R"("icon":"https://example.com/a.png")"))));
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const int alpha = rowNamedInOsList(model, QStringLiteral("Alpha"));
    REQUIRE(alpha >= 0);
    CHECK(roleOfRow(model, alpha, "icon").toString()
          == QStringLiteral("image://icons/https://example.com/a.png"));
}

TEST_CASE("A local OS icon is left as it is", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(
        osEntry(QStringLiteral("Alpha"), QStringLiteral(R"("icon":"qrc:/icons/a.png")"))));
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const int alpha = rowNamedInOsList(model, QStringLiteral("Alpha"));
    REQUIRE(alpha >= 0);
    CHECK(roleOfRow(model, alpha, "icon").toString() == QStringLiteral("qrc:/icons/a.png"));
}

TEST_CASE("Sizes and hashes reach the chooser", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(osEntry(
        QStringLiteral("Alpha"),
        QStringLiteral(R"("extract_sha256":"abc123","release_date":"2026-01-01","architecture":"arm64")"))));
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    const int alpha = rowNamedInOsList(model, QStringLiteral("Alpha"));
    REQUIRE(alpha >= 0);

    // These feed the capacity check and the hash the download is verified
    // against; a role that does not arrive is a write that cannot be checked.
    CHECK(roleOfRow(model, alpha, "extract_size").toDouble() == 200);
    CHECK(roleOfRow(model, alpha, "image_download_size").toDouble() == 100);
    CHECK(roleOfRow(model, alpha, "extract_sha256").toString() == QStringLiteral("abc123"));
    CHECK(roleOfRow(model, alpha, "release_date").toString() == QStringLiteral("2026-01-01"));
}

TEST_CASE("A soft refresh repaints without rebuilding", "[models][oslist]")
{
    TestableImageWriter writer;
    writer.feedOsList(osListWithEntries(osEntry(QStringLiteral("Alpha"))));
    OSListModel *model = writer.getOSList();
    REQUIRE(model->reload());

    int resets = 0, changes = 0;
    QObject::connect(model, &QAbstractItemModel::modelReset, model, [&] { ++resets; });
    QObject::connect(model, &QAbstractItemModel::dataChanged, model,
                     [&](const QModelIndex &, const QModelIndex &, const QList<int> &) { ++changes; });

    // Used when only the display needs updating (a language change, say).
    // A full reset would scroll the list back to the top under the user.
    model->softRefresh();

    CHECK(resets == 0);
    CHECK(changes == 1);
}

TEST_CASE("A soft refresh of an empty list is harmless", "[models][oslist]")
{
    TestableImageWriter writer;
    OSListModel *model = writer.getOSList();

    int changes = 0;
    QObject::connect(model, &QAbstractItemModel::dataChanged, model,
                     [&](const QModelIndex &, const QModelIndex &, const QList<int> &) { ++changes; });

    model->softRefresh();
    CHECK(changes == 0);
}

// ══════════════════════════════════════════════════════════════
// The role number StorageSelectionStep.qml hardcodes
//
// conditionalNext() in the storage step decides whether pressing Enter
// carries the user straight on to the next step, and it must not do that
// for a system drive -- the confirmation dialog is the only thing between
// the user and writing over the operating system they are running.
//
// It normally asks the delegate. When the delegate has not been realised
// -- which happens with a screen reader attached on Windows -- it falls
// back to asking the model directly, and QML cannot see the C++ enum, so
// it passes the role as a literal: 0x107.
//
// Nothing ties that literal to the enum. Insert a role anywhere before
// isSystemRole and the QML silently starts reading isReadOnlyRole instead,
// with no compiler error and no failing test. A read-only system drive
// would then answer "not a system drive", auto-advance would fire, and the
// person carried past the confirmation is the screen-reader user who
// triggered the fallback in the first place.
//
// isStorageItemSelectable() hardcodes two: the system role and the
// read-only one. Both are checked here, because a wrong read-only role
// either offers an unwritable card as a target or hides a good one, and
// neither shows up as anything but a confused user.
// ══════════════════════════════════════════════════════════════

TEST_CASE("The role number the storage step hardcodes is still the system-drive role",
          "[models][roles]")
{
    // If either fails, fix the literal in
    // src/wizard/StorageSelectionStep.qml -- conditionalNext() hardcodes
    // the system role and isStorageItemSelectable() hardcodes both --
    // rather than these numbers.
    constexpr int kSystemRoleUsedByQml = 0x107;
    constexpr int kReadOnlyRoleUsedByQml = 0x106;

    CHECK(static_cast<int>(DriveListModel::isSystemRole) == kSystemRoleUsedByQml);
    CHECK(static_cast<int>(DriveListModel::isReadOnlyRole) == kReadOnlyRoleUsedByQml);

    DriveListModel model;
    const auto names = model.roleNames();
    REQUIRE(names.contains(kSystemRoleUsedByQml));
    CHECK(names.value(kSystemRoleUsedByQml) == QByteArray("isSystem"));
    REQUIRE(names.contains(kReadOnlyRoleUsedByQml));
    CHECK(names.value(kReadOnlyRoleUsedByQml) == QByteArray("isReadOnly"));
}

TEST_CASE("Every role the QML asks for by name resolves", "[models][roles]")
{
    // The delegate binds these by name. A rename in the C++ hash makes the
    // property undefined in QML, which reads as false -- so a renamed
    // isSystem would silently stop hiding system drives rather than error.
    DriveListModel model;
    const auto names = model.roleNames();

    for (const char *bound : {"device", "description", "size", "isUsb", "isScsi",
                              "isReadOnly", "isSystem", "mountpoints"}) {
        INFO("role bound by the storage delegate: " << bound);
        CHECK(names.key(QByteArray(bound), -1) != -1);
    }
}

// ══════════════════════════════════════════════════════════════
// Which images a board is allowed to be offered
//
// Choosing a board sets a hardware filter, and the OS list is cut down to the
// entries that say they support it. Get that wrong and someone with a Pi Zero
// is offered a 64-bit-only image, writes it, and the board does not boot --
// with nothing on screen to connect the two.
//
// The filter itself is file-local, so these drive it the way the application
// does: feed a list, set a filter, read back getFilteredOSlist().
// ══════════════════════════════════════════════════════════════

namespace {

// A list with one entry per shape the filter has to deal with: tagged for one
// board, tagged for another, tagged for both, and untagged.
QByteArray taggedOsListJson()
{
    return QByteArray(R"JSON({
        "os_list": [
            {
                "name": "For the Pi 5 only",
                "url": "https://example.invalid/pi5.img.xz",
                "devices": ["pi5-64bit"],
                "image_download_size": 100,
                "extract_size": 200,
                "extract_sha256": "aa"
            },
            {
                "name": "For the Pi Zero only",
                "url": "https://example.invalid/zero.img.xz",
                "devices": ["pizero-32bit"],
                "image_download_size": 100,
                "extract_size": 200,
                "extract_sha256": "bb"
            },
            {
                "name": "For either board",
                "url": "https://example.invalid/both.img.xz",
                "devices": ["pi5-64bit", "pizero-32bit"],
                "image_download_size": 100,
                "extract_size": 200,
                "extract_sha256": "cc"
            },
            {
                "name": "Says nothing about boards",
                "url": "https://example.invalid/untagged.img.xz",
                "image_download_size": 100,
                "extract_size": 200,
                "extract_sha256": "dd"
            }
        ]
    })JSON");
}

// A list where the only entries live inside a category, so the category's own
// survival can be checked.
QByteArray categorisedOsListJson()
{
    return QByteArray(R"JSON({
        "os_list": [
            {
                "name": "Other general-purpose OS",
                "subitems": [
                    {
                        "name": "Only for the Pi 5",
                        "url": "https://example.invalid/cat-pi5.img.xz",
                        "devices": ["pi5-64bit"],
                        "image_download_size": 100,
                        "extract_size": 200,
                        "extract_sha256": "ee"
                    }
                ]
            }
        ]
    })JSON");
}

// Every entry name the chooser would show, categories included, flattened so a
// case can say what is on offer.
QStringList offeredNames(const QByteArray &filtered)
{
    QStringList out;
    const QJsonArray list = QJsonDocument::fromJson(filtered)
                                .object()
                                .value(QStringLiteral("os_list"))
                                .toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        out << o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &sub : o.value(QStringLiteral("subitems")).toArray())
            out << sub.toObject().value(QStringLiteral("name")).toString();
    }
    return out;
}

} // namespace

TEST_CASE("A board is offered the images that name it", "[models][oslist][hwfilter]")
{
    TestableImageWriter writer;
    writer.feedOsList(taggedOsListJson());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pizero-32bit")}, false);

    const QStringList offered = offeredNames(writer.getFilteredOSlist());
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK(offered.contains(QStringLiteral("For the Pi Zero only")));
    CHECK(offered.contains(QStringLiteral("For either board")));
}

TEST_CASE("A board is not offered images that name a different one",
          "[models][oslist][hwfilter]")
{
    // The one that matters: a 64-bit image on a 32-bit board writes fine and
    // then does not boot.
    TestableImageWriter writer;
    writer.feedOsList(taggedOsListJson());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pizero-32bit")}, false);

    const QStringList offered = offeredNames(writer.getFilteredOSlist());
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(offered.contains(QStringLiteral("For the Pi 5 only")));
}

TEST_CASE("An untagged image is offered only when the filter is inclusive",
          "[models][oslist][hwfilter]")
{
    // An entry that says nothing about boards is a judgement call, and the
    // filter's inclusive flag is where it is made: inclusive keeps it on the
    // grounds that it probably works anywhere, exclusive drops it on the
    // grounds that it has not said so.
    {
        TestableImageWriter writer;
        writer.feedOsList(taggedOsListJson());
        writer.setHWFilterList(QJsonArray{QStringLiteral("pizero-32bit")}, true);

        const QStringList offered = offeredNames(writer.getFilteredOSlist());
        INFO("inclusive: " << offered.join(QStringLiteral(", ")).toStdString());
        CHECK(offered.contains(QStringLiteral("Says nothing about boards")));
    }
    {
        TestableImageWriter writer;
        writer.feedOsList(taggedOsListJson());
        writer.setHWFilterList(QJsonArray{QStringLiteral("pizero-32bit")}, false);

        const QStringList offered = offeredNames(writer.getFilteredOSlist());
        INFO("exclusive: " << offered.join(QStringLiteral(", ")).toStdString());
        CHECK_FALSE(offered.contains(QStringLiteral("Says nothing about boards")));
    }
}

TEST_CASE("With no filter set, every image is offered", "[models][oslist][hwfilter]")
{
    // Before a board is chosen there is nothing to filter against, and hiding
    // everything would leave the chooser empty.
    TestableImageWriter writer;
    writer.feedOsList(taggedOsListJson());
    writer.setHWFilterList(QJsonArray{}, false);

    const QStringList offered = offeredNames(writer.getFilteredOSlist());
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK(offered.contains(QStringLiteral("For the Pi 5 only")));
    CHECK(offered.contains(QStringLiteral("For the Pi Zero only")));
    CHECK(offered.contains(QStringLiteral("Says nothing about boards")));
}

TEST_CASE("A category whose images all belong to another board disappears",
          "[models][oslist][hwfilter]")
{
    // Rather than being shown empty. A category the user can open and find
    // nothing in reads as a failure to load.
    TestableImageWriter writer;
    writer.feedOsList(categorisedOsListJson());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pizero-32bit")}, false);

    const QStringList offered = offeredNames(writer.getFilteredOSlist());
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK_FALSE(offered.contains(QStringLiteral("Other general-purpose OS")));
    CHECK_FALSE(offered.contains(QStringLiteral("Only for the Pi 5")));
}

TEST_CASE("A category keeps the images that do belong to this board",
          "[models][oslist][hwfilter]")
{
    // The other side, so the case above is not passing because categories are
    // dropped wholesale.
    TestableImageWriter writer;
    writer.feedOsList(categorisedOsListJson());
    writer.setHWFilterList(QJsonArray{QStringLiteral("pi5-64bit")}, false);

    const QStringList offered = offeredNames(writer.getFilteredOSlist());
    INFO("offered: " << offered.join(QStringLiteral(", ")).toStdString());
    CHECK(offered.contains(QStringLiteral("Other general-purpose OS")));
    CHECK(offered.contains(QStringLiteral("Only for the Pi 5")));
}
