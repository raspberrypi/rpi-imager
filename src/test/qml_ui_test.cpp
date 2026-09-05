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
        qmlRegisterSingletonType<ImageWriter>(kUri, 1, 0, "ImageWriterSingleton",
                                              [](QQmlEngine *e, QJSEngine *j) -> QObject * {
                                                  return ImageWriter::create(e, j);
                                              });
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        if (prepareModule())
            engine->addImportPath(importRoot()->path());
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qml_ui, Setup)

#include "qml_ui_test.moc"
