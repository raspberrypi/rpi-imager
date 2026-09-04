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
