/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Does the user interface actually load?
 *
 * Every other test in this suite drives the C++ behind the UI. Nothing loads
 * the UI itself, and QML is not fully checked until it runs: a binding to a
 * property that does not exist, a component that fails to resolve, or a
 * singleton that is not registered are all runtime failures. The build
 * compiles the QML to C++, which catches syntax, and qmllint catches some
 * typing -- neither catches a component that throws on instantiation.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "imagewriter.h"
#include "drivelistmodel.h"
#include "hwlistmodel.h"
#include "oslistmodel.h"
#include "urlfmt.h"
#include "app_resources.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QStandardPaths>
#include <QUrl>
#include <QtQml>

namespace {

constexpr const char *kUri = "RpiImager";

void registerTypes()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    qmlRegisterUncreatableType<DriveListModel>(kUri, 1, 0, "DriveListModel", "Created by C++");
    qmlRegisterUncreatableType<HWListModel>(kUri, 1, 0, "HWListModel", "Created by C++");
    qmlRegisterUncreatableType<OSListModel>(kUri, 1, 0, "OSListModel", "Created by C++");
    qmlRegisterSingletonType<UrlFmt>(kUri, 1, 0, "UrlFmt",
                                     [](QQmlEngine *, QJSEngine *) -> QObject * {
                                         return new UrlFmt;
                                     });
}

// The build generates a complete QML module here -- a qmldir naming every
// component, alongside copies of the files. Using it means the wizard files
// resolve components from other directories exactly as they do in the running
// application, which loading straight out of the source tree does not.
// A receiver for string-based connects: QML-declared signals have no member
// pointer to hand the new-style connect.
class ClickCounter : public QObject
{
    Q_OBJECT
public:
    int count = 0;
public slots:
    void onClicked() { ++count; }
};

QString moduleParent() { return QStringLiteral(IMAGER_QML_MODULE_PARENT); }
QString moduleDir() { return moduleParent() + QStringLiteral("/RpiImager"); }
bool moduleBuilt() { return QFile::exists(moduleDir() + QStringLiteral("/qmldir")); }

// The generated qmldir carries "prefer :/qt/qml/RpiImager/", which points at
// resources compiled into the application executable. A test binary does not
// have them, and QML honours the preference and then fails to find anything.
// Copying the module and dropping that one line makes it load the .qml files
// sitting next to the qmldir instead.
class ModuleCopy
{
public:
    ModuleCopy()
    {
        if (!_dir.isValid() || !moduleBuilt())
            return;
        const QString dest = _dir.path() + QStringLiteral("/RpiImager");
        if (!copyTree(moduleDir(), dest))
            return;

        QFile in(dest + QStringLiteral("/qmldir"));
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
        const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
        in.close();

        QFile out(dest + QStringLiteral("/qmldir"));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return;
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("prefer ")))
                continue;
            out.write(line.toUtf8() + "\n");
        }
        out.close();
        _ready = true;
    }

    bool isReady() const { return _ready; }
    QString importPath() const { return _dir.path(); }

private:
    static bool copyTree(const QString &from, const QString &to)
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

    QTemporaryDir _dir;
    bool _ready = false;
};

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    initAppResources();
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("qml_load_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("The generated QML module is where the test expects it", "[qml]")
{
    if (!moduleBuilt())
        SKIP("the QML module has not been generated; build the application target");
    // If this fails the rest of the file is testing nothing.
    CHECK(QFile::exists(moduleDir() + QStringLiteral("/main.qml")));
}

TEST_CASE("Every QML component in the module resolves", "[qml]")
{
    if (!moduleBuilt())
        SKIP("the QML module has not been generated; build the application target");

    ModuleCopy module;
    if (!module.isReady())
        SKIP("could not stage a copy of the generated QML module");

    registerTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(module.importPath());

    QDir dir(module.importPath() + QStringLiteral("/RpiImager"));
    QStringList files;
    QDirIterator it(dir.absolutePath(), {QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort();
    REQUIRE(files.size() > 20);

    // A component that will not compile is one the interface cannot show.
    // The build compiles QML to C++, which catches syntax; this catches a
    // type that does not resolve -- a component renamed or removed while
    // something still refers to it, or a C++ type nobody registered.
    QStringList broken;
    for (const QString &file : files) {
        QQmlComponent component(&engine, QUrl::fromLocalFile(file));
        if (component.isError()) {
            for (const QQmlError &e : component.errors())
                broken << e.toString();
        }
    }
    INFO("errors:\n" << broken.join(QStringLiteral("\n")).toStdString());
    CHECK(broken.isEmpty());
}

TEST_CASE("The main window loads", "[qml]")
{
    if (!moduleBuilt())
        SKIP("the QML module has not been generated; build the application target");

    ModuleCopy module;
    if (!module.isReady())
        SKIP("could not stage a copy of the generated QML module");

    registerTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(module.importPath());

    QStringList errors;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                     [&errors](const QList<QQmlError> &list) {
                         for (const QQmlError &e : list)
                             errors << e.toString();
                     });

    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(module.importPath()
                                                + QStringLiteral("/RpiImager/main.qml")));
    for (const QQmlError &e : component.errors())
        errors << e.toString();

    INFO("errors:\n" << errors.join(QStringLiteral("\n")).toStdString());
    // The whole tree, from the root window down through the wizard.
    CHECK_FALSE(component.isError());
}

TEST_CASE("A component can be created and driven", "[qml][drive]")
{
    if (!moduleBuilt())
        SKIP("the QML module has not been generated; build the application target");

    ModuleCopy module;
    if (!module.isReady())
        SKIP("could not stage a copy of the generated QML module");

    registerTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(module.importPath());

    // Instantiating a single component and exercising it is the part of
    // driven testing that does not need a rendered scene: properties in,
    // signals out. The Qt we ship has no QtTest, so there is no mouseClick()
    // to reach for anyway.
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        module.importPath() + QStringLiteral("/RpiImager/qmlcomponents/ImButton.qml")));
    INFO("component errors: " << component.errorString().toStdString());
    REQUIRE_FALSE(component.isError());

    std::unique_ptr<QObject> button(component.create());
    REQUIRE(button != nullptr);

    // A property QML binds and the wizard sets to gate the Write button.
    button->setProperty("enabled", false);
    CHECK_FALSE(button->property("enabled").toBool());
    button->setProperty("enabled", true);
    CHECK(button->property("enabled").toBool());

    // And the signal a click turns into, invoked directly rather than
    // synthesised through a scene that would have to be laid out first.
    // Counted at a real receiver, so this checks delivery rather than just
    // that the signal exists.
    ClickCounter counter;
    REQUIRE(QObject::connect(button.get(), SIGNAL(clicked()), &counter, SLOT(onClicked())));
    REQUIRE(QMetaObject::invokeMethod(button.get(), "clicked"));
    CHECK(counter.count == 1);
}

#include "qml_load_test.moc"
