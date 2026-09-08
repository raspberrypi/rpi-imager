/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Driven tests for the QML controls: real mouse clicks and key presses
 * against real instances, rather than the C++ behind them.
 *
 * qml_load_test proves every component instantiates. That is a low bar --
 * it says nothing about what a component does once a user touches it. The
 * controls in the customisation step carry actual logic: a password field
 * that decides when to offer to reveal itself, a text field that scrubs
 * control characters out of what gets written to a Wi-Fi config. None of
 * that is reachable from C++; it lives in QML and only runs when something
 * types into it.
 *
 * Qt Quick Test needs the RpiImager module importable. The generated qmldir
 * carries a `prefer :/qt/qml/RpiImager/` line pointing at resources compiled
 * into the application binary, which a test binary does not have, so the
 * same copy-and-strip that qml_load_test uses is applied here before the
 * engine goes looking.
 */

#include <QtQuickTest/quicktest.h>

#include "imagewriter.h"
#include "drivelistmodel.h"
#include "hwlistmodel.h"
#include "oslistmodel.h"
#include "urlfmt.h"
#include "clipboardhelper.h"
#include "platformhelper.h"
#include "app_resources.h"
#include "drivelist/drivelist.h"

#include <QAccessible>
#include <QUrl>
#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtQml>

namespace {

constexpr const char *kUri = "RpiImager";

QString moduleDir()
{
    // RPI_QML_MODULE_OVERRIDE points the run at an instrumented copy of the
    // module (see tools/qml_coverage_instrument.py). Unset, which is every
    // ordinary run, this is the module the build generated.
    const QByteArray override = qgetenv("RPI_QML_MODULE_OVERRIDE");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    return QStringLiteral(IMAGER_QML_MODULE_PARENT) + QStringLiteral("/RpiImager");
}

bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString rel = QDir(from).relativeFilePath(src);
        const QString dst = to + QLatin1Char('/') + rel;
        if (QFileInfo(src).isDir()) {
            if (!QDir().mkpath(dst))
                return false;
        } else {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!QFile::copy(src, dst))
                return false;
        }
    }
    return true;
}

// Lives for the whole run: the engine resolves imports out of it lazily.
QTemporaryDir *importRoot()
{
    static QTemporaryDir dir;
    return &dir;
}

// Copy the generated module somewhere writable and drop the `prefer` line, so
// the .qml files next to the qmldir are what gets loaded.
bool prepareModule()
{
    static bool ready = [] {
        if (!importRoot()->isValid())
            return false;
        if (!QFile::exists(moduleDir() + QStringLiteral("/qmldir")))
            return false;

        const QString dest = importRoot()->path() + QStringLiteral("/RpiImager");
        if (!copyTree(moduleDir(), dest))
            return false;

        QFile in(dest + QStringLiteral("/qmldir"));
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
        in.close();

        QFile out(dest + QStringLiteral("/qmldir"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("prefer ")))
                continue;
            out.write(line.toUtf8() + "\n");
        }
        return true;
    }();
    return ready;
}

} // namespace

// Turns the accessibility flag on and off so a test can stand on both sides
// of it. This lives here rather than on PlatformHelper: production code
// should not carry a setter whose only caller is a test, and QAccessible's
// own setActive() is public API that needs nothing added to reach it.
class TestAccessibility : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void setActive(bool active) { QAccessible::setActive(active); }
    Q_INVOKABLE bool isActive() const { return QAccessible::isActive(); }
};

// Counts which instrumented QML sites ran.
//
// QML has no coverage tool -- gcov cannot see it, and the compiler that
// would turn it into instrumentable C++ is not in the open-source Qt -- so
// the sites are counted by a probe injected into a copy of the source. This
// is what those probes call. It exists only when RPI_QML_COVERAGE_HITS names
// somewhere to write the result, so an ordinary run neither instruments nor
// counts anything.
// Puts a file on disk for a case that needs one.
//
// Several things a user does end at the filesystem -- choosing a custom
// image, an SSH public key, a repository json -- and the code behind them
// refuses anything that is not a real file, which is correct and has its
// own tests. QML cannot write one, so the harness does, into a directory
// that goes away with the run. Like TestAccessibility, this is here rather
// than on anything shipped: production code should not carry a file writer
// whose only caller is a test.
class TestFiles : public QObject
{
    Q_OBJECT

public:
    // Returns the file:// URL of the written file, or an empty string if it
    // could not be written -- which a case should treat as a reason to fail
    // rather than to carry on against a path that is not there.
    Q_INVOKABLE QString write(const QString &name, const QString &contents)
    {
        if (!_dir.isValid())
            return QString();
        const QString filePath = _dir.filePath(name);
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        const QByteArray bytes = contents.toUtf8();
        if (f.write(bytes) != bytes.size())
            return QString();
        f.close();
        return QUrl::fromLocalFile(filePath).toString();
    }

    // The same file as a plain path, for the calls that want one.
    Q_INVOKABLE QString localPath(const QString &name) const
    {
        return _dir.isValid() ? _dir.filePath(name) : QString();
    }

private:
    QTemporaryDir _dir;
};

// The writer the QML singleton hands out. Kept here so the harness can reach
// the same drive list the steps bind to, rather than a second one.
ImageWriter *g_qmlWriter = nullptr;

// Puts drives in the list a case needs to choose from.
//
// StorageSelectionStep binds its list to ImageWriterSingleton.getDriveList(),
// a DriveListModel filled by a thread that polls the machine's actual disks.
// A test cannot plug a card in, and must not go near the disks that are
// already there.
//
// processDriveList() is the slot that poller calls, so handing it a device
// list puts rows into the real model, through the real insertion path, with
// the real roles. The alternative -- standing a look-alike model in front of
// the delegate -- would answer whatever the delegate asked for and keep
// answering it after a role was renamed, which is the drift these tests
// exist to catch.
//
// Only the fields the chooser reads are settable. Anything else keeps
// DeviceDescriptor's own default, so a case says what it is about.
class TestDrives : public QObject
{
    Q_OBJECT

public:
    // Each entry is an object: device, description, size, and the flags that
    // decide whether a row can be chosen at all.
    Q_INVOKABLE void set(const QVariantList &drives)
    {
        DriveListModel *model = driveList();
        if (!model)
            return;

        // Stop the poller first, or the next tick replaces these rows with
        // whatever disks are actually in the machine running the suite --
        // which is also what the list holds before this is called, and the
        // reason a case cannot simply assume it starts empty.
        model->stopPolling();

        std::vector<Drivelist::DeviceDescriptor> list;
        list.reserve(static_cast<size_t>(drives.size()));
        for (const QVariant &v : drives) {
            const QVariantMap m = v.toMap();
            Drivelist::DeviceDescriptor d;
            d.device = m.value(QStringLiteral("device"),
                               QStringLiteral("/dev/sdz")).toString().toStdString();
            d.description = m.value(QStringLiteral("description"),
                                    QStringLiteral("Test device")).toString().toStdString();
            d.size = m.value(QStringLiteral("size"), 32000000000ULL).toULongLong();
            // A row the chooser will show at all: the list drops anything
            // that is neither removable nor a system disk.
            d.isUSB = m.value(QStringLiteral("isUsb"), true).toBool();
            d.isRemovable = m.value(QStringLiteral("isRemovable"), true).toBool();
            d.isSCSI = m.value(QStringLiteral("isScsi"), false).toBool();
            d.isSystem = m.value(QStringLiteral("isSystem"), false).toBool();
            d.isReadOnly = m.value(QStringLiteral("isReadOnly"), false).toBool();
            d.isVirtual = m.value(QStringLiteral("isVirtual"), false).toBool();
            for (const QString &mp : m.value(QStringLiteral("mountpoints")).toStringList())
                d.mountpoints.push_back(mp.toStdString());
            list.push_back(d);
        }
        model->processDriveList(list);
    }

    // Back to an empty list. Polling stays stopped: restarting it would let
    // the machine's own disks back in, and every file in the run shares this
    // model, so an empty frozen list is the tidiest thing to leave behind.
    Q_INVOKABLE void clear()
    {
        if (DriveListModel *model = driveList()) {
            model->stopPolling();
            model->processDriveList({});
        }
    }

    Q_INVOKABLE int count()
    {
        DriveListModel *model = driveList();
        return model ? model->rowCount(QModelIndex()) : -1;
    }

private:
    static DriveListModel *driveList()
    {
        return g_qmlWriter ? g_qmlWriter->getDriveList() : nullptr;
    }
};

class QmlCoverage : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void hit(int id) { ++_hits[id]; }

    void writeTo(const QString &path) const
    {
        QJsonObject counts;
        for (auto it = _hits.cbegin(); it != _hits.cend(); ++it)
            counts.insert(QString::number(it.key()), it.value());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        out.write(QJsonDocument(counts).toJson(QJsonDocument::Compact));
    }

private:
    QHash<int, int> _hits;
};

QmlCoverage *coverageCounter()
{
    static QmlCoverage *counter = [] {
        if (qEnvironmentVariableIsEmpty("RPI_QML_COVERAGE_HITS"))
            return static_cast<QmlCoverage *>(nullptr);
        auto *c = new QmlCoverage;
        // Written on the way out rather than per hit: the probes fire in
        // tight loops inside bindings, and a run writes tens of thousands.
        qAddPostRoutine([] {
            if (auto *existing = coverageCounter())
                existing->writeTo(QString::fromLocal8Bit(qgetenv("RPI_QML_COVERAGE_HITS")));
        });
        return c;
    }();
    return counter;
}

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup() = default;

public slots:
    void applicationAvailable()
    {
        initAppResources();
        QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
        QCoreApplication::setApplicationName(
            QStringLiteral("qml_ui_test-%1").arg(QCoreApplication::applicationPid()));
        QStandardPaths::setTestModeEnabled(true);

        // The types the components reference. Registered by hand for the same
        // reason qml_load_test does it: the generated registration function
        // lives in the application target, not here.
        qmlRegisterUncreatableType<DriveListModel>(kUri, 1, 0, "DriveListModel",
                                                   QStringLiteral("Created by C++"));
        qmlRegisterUncreatableType<HWListModel>(kUri, 1, 0, "HWListModel",
                                                QStringLiteral("Created by C++"));
        qmlRegisterUncreatableType<OSListModel>(kUri, 1, 0, "OSListModel",
                                                QStringLiteral("Created by C++"));
        qmlRegisterSingletonType<UrlFmt>(kUri, 1, 0, "UrlFmt",
                                         [](QQmlEngine *, QJSEngine *) -> QObject * {
                                             return new UrlFmt;
                                         });

        // The controls reference these two directly -- ImTextField's context
        // menu reads ClipboardHelper.hasText, and every animation duration
        // goes through PlatformHelper.prefersReducedMotion. Unregistered they
        // are a ReferenceError at first paint rather than a load failure, so
        // the component still instantiates and the tests still pass while the
        // menu and the motion settings quietly do nothing.
        qmlRegisterSingletonType<ClipboardHelper>(kUri, 1, 0, "ClipboardHelper",
                                                  [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                      return new ClipboardHelper;
                                                  });
        qmlRegisterSingletonType<PlatformHelper>(kUri, 1, 0, "PlatformHelper",
                                                 [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                     return new PlatformHelper;
                                                 });

        // Style.qml asks the writer whether this is a kiosk build, and its
        // font scale hangs off the same object. Without it every point size
        // resolves to zero, so the controls lay out at nothing and anything
        // that clicks at a coordinate misses. The application owns this
        // instance the same way main() does.
        static ImageWriter writer(nullptr);
        ImageWriter::setQmlInstance(&writer);
        g_qmlWriter = &writer;
        qmlRegisterSingletonType<ImageWriter>(kUri, 1, 0, "ImageWriterSingleton",
                                              [](QQmlEngine *e, QJSEngine *j) -> QObject * {
                                                  return ImageWriter::create(e, j);
                                              });

        qmlRegisterSingletonType<TestAccessibility>(kUri, 1, 0, "TestAccessibility",
                                                    [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                        return new TestAccessibility;
                                                    });

        qmlRegisterSingletonType<TestFiles>(kUri, 1, 0, "TestFiles",
                                            [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                return new TestFiles;
                                            });

        qmlRegisterSingletonType<TestDrives>(kUri, 1, 0, "TestDrives",
                                             [](QQmlEngine *, QJSEngine *) -> QObject * {
                                                 return new TestDrives;
                                             });
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        if (prepareModule())
            engine->addImportPath(importRoot()->path());

        // Where the module actually got copied to. A test that wants to
        // load main.qml -- the application entry, which is not an exported
        // type -- has to name a file, and naming the one in the source tree
        // would bypass the instrumented copy and report the file as never
        // run while a test was driving it.
        engine->globalObject().setProperty(
            QStringLiteral("__qmlModuleRoot"),
            QUrl::fromLocalFile(importRoot()->path() + QStringLiteral("/RpiImager/")).toString());

        // A global rather than a registered singleton: the probe is injected
        // into files that already have their own imports, and adding one to
        // each of them would be a much larger edit to get wrong.
        if (auto *counter = coverageCounter()) {
            QQmlEngine::setObjectOwnership(counter, QQmlEngine::CppOwnership);
            engine->globalObject().setProperty(QStringLiteral("__qmlcov"),
                                               engine->newQObject(counter));
        }
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_ui, Setup)

#include "qml_ui_test.moc"
