/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef RPI_IMAGER_TEST_SCRATCH_H
#define RPI_IMAGER_TEST_SCRATCH_H

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <utility>

// Naming and cleanup for the writable locations a test process uses.
//
// Test mode moves QStandardPaths under a scratch root, and each binary adds
// its own pid so parallel cases cannot read each other's settings. Nothing
// removed them: ctest runs one process per case, so a full run left tens of
// thousands of directories behind, and the accumulated runs reached 18 GB.
namespace rpi_imager_test {

namespace detail {

inline QString &scratchName()
{
    static QString name;
    return name;
}

inline QStringList &scratchPaths()
{
    static QStringList paths;
    return paths;
}

// A path is only removed when its last segment is still the unique name this
// process claimed, so a run that never enabled test mode, or one that renamed
// itself afterwards, removes nothing.
inline bool ownedByThisProcess(const QString &path)
{
    const QString &name = scratchName();
    if (path.isEmpty() || name.isEmpty())
        return false;
    const QString leaf = QFileInfo(path).fileName();
    return leaf == name || leaf == name + QLatin1String(".conf");
}

inline void removeScratchPaths()
{
    for (const QString &path : std::as_const(scratchPaths())) {
        if (!ownedByThisProcess(path))
            continue;
        QFileInfo info(path);
        if (info.isDir())
            QDir(path).removeRecursively();
        else if (info.exists())
            QFile::remove(path);
    }
    scratchPaths().clear();
}

} // namespace detail

/**
 * @brief Give this test process its own writable locations, and take them away again.
 *
 * Sets the organisation and a pid-unique application name, enables test mode,
 * and arranges for the directories that names creates to be removed when the
 * application is destroyed. Call it once, after QCoreApplication exists.
 */
inline void useScratchPaths(const QString &testName)
{
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    const QString name =
        QStringLiteral("%1-%2").arg(testName).arg(QCoreApplication::applicationPid());
    QCoreApplication::setApplicationName(name);
    QStandardPaths::setTestModeEnabled(true);

    // And the settings themselves, which test mode does not reach on Windows.
    // QSettings defaults to NativeFormat, and that is the registry there -- so
    // a run wrote to the developer's own HKCU\Software\Raspberry Pi, including
    // the imagecustomization key that holds their hostname, username and
    // password hash. The organisation and application names above kept the
    // .conf file on Unix out of the way and did nothing at all here.
    //
    // IniFormat puts it in a file instead, and test mode redirects that file
    // into the scratch tree with everything else. Set for every platform rather
    // than only Windows: one storage shape under test is easier to reason about
    // than two, and the cases that read settings back do not care which it is.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    detail::scratchName() = name;
    detail::scratchPaths() = QStringList{
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
        // QSettings writes beside the config directory, not inside it.
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1Char('/') + QCoreApplication::organizationName()
            + QLatin1Char('/') + name + QLatin1String(".conf"),
    };
    qAddPostRoutine(&detail::removeScratchPaths);
}

} // namespace rpi_imager_test

#endif // RPI_IMAGER_TEST_SCRATCH_H
