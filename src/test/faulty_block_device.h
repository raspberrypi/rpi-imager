// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// A block device that fails.
//
// Backed by a scratch file on a loop device with a device-mapper table that
// maps the first N megabytes straight through and returns EIO for everything
// past them. No real media is involved and nothing outside the mapping this
// object creates is touched.
//
// This is the only way to reach the error handling in the async write path:
// those branches run on a *negative completion* coming back from the block
// layer, which cannot be provoked by feeding the writer bad arguments. It is
// also a faithful stand-in for a counterfeit card, which accepts writes up to
// its real capacity and errors beyond it.
//
// Creating the mapping needs root, so callers should skip when
// canRunPrivileged() is false -- matching how disk_formatter_test handles
// privileged setup.

#ifndef RPI_IMAGER_TEST_FAULTY_BLOCK_DEVICE_H_
#define RPI_IMAGER_TEST_FAULTY_BLOCK_DEVICE_H_

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUuid>

#include "fixture_process.h"

namespace rpi_imager::testing {

inline bool canRunPrivileged()
{
    QProcess probe;
    probe.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    probe.waitForFinished(10000);
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

inline bool runPrivileged(const QString &program, const QStringList &args, QByteArray *stdOut = nullptr)
{
    QProcess proc;
    proc.start(QStringLiteral("sudo"), QStringList{QStringLiteral("-n"), program} << args);
    proc.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    if (stdOut)
        *stdOut = proc.readAllStandardOutput();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

// A block device whose first `goodMegabytes` are writable and whose remainder
// returns EIO. Tears itself down, including on an assertion failure.
class FaultyDevice
{
public:
    // A device that is writable at both ends and fails in a band in the
    // middle. A counterfeit card behaves this way once past its real
    // capacity, and it is the only shape that lets a write get started and
    // then fail: a device whose *tail* is bad is caught by the end-of-device
    // check during preparation, before any image data is written at all.
    struct BadBand
    {
        int startMegabytes;
        int lengthMegabytes;
    };

    FaultyDevice(int totalMegabytes, BadBand band)
        : FaultyDevice(totalMegabytes, -1, band.startMegabytes, band.lengthMegabytes)
    {
    }

    explicit FaultyDevice(int totalMegabytes, int goodMegabytes)
        : FaultyDevice(totalMegabytes, goodMegabytes, -1, -1)
    {
    }

private:
    FaultyDevice(int totalMegabytes, int goodMegabytes, int bandStartMB, int bandLenMB)
        // Unique per instance, not just per process: two of these exist in
        // quick succession within a run, and a name collision would have one
        // tear down the other's mapping underneath it.
        : _name(QStringLiteral("rpi-imager-test-%1-%2")
                    .arg(QCoreApplication::applicationPid())
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)))
    {
        _dir = QDir::temp().filePath(
            QStringLiteral("rpi-imager-faulty-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(_dir);
        _backing = QDir(_dir).filePath(QStringLiteral("backing.img"));

        QFile f(_backing);
        if (!f.open(QIODevice::WriteOnly) ||
            !f.resize(static_cast<qint64>(totalMegabytes) * 1024 * 1024))
            return;
        f.close();

        // losetup -f only ever hands back a device that is not in use, so
        // this cannot collide with anything already attached.
        QByteArray out;
        if (!runPrivileged(QStringLiteral("losetup"),
                           {QStringLiteral("-f"), QStringLiteral("--show"), _backing}, &out))
            return;
        _loop = QString::fromUtf8(out).trimmed();
        if (!_loop.startsWith(QStringLiteral("/dev/loop")))
            return;

        const qint64 goodSectors = static_cast<qint64>(goodMegabytes) * 1024 * 1024 / 512;
        const qint64 totalSectors = static_cast<qint64>(totalMegabytes) * 1024 * 1024 / 512;
        // A fully writable device when asked for one, so a control case can
        // tell "the device failed" from "the harness is broken".
        QString table;
        if (bandStartMB >= 0) {
            // good | error | good
            const qint64 bandStart = static_cast<qint64>(bandStartMB) * 1024 * 1024 / 512;
            const qint64 bandLen = static_cast<qint64>(bandLenMB) * 1024 * 1024 / 512;
            const qint64 tailStart = bandStart + bandLen;
            if (tailStart > totalSectors)
                return;
            table = QStringLiteral("0 %1 linear %2 0\n").arg(bandStart).arg(_loop)
                  + QStringLiteral("%1 %2 error\n").arg(bandStart).arg(bandLen)
                  + QStringLiteral("%1 %2 linear %3 %4\n")
                        .arg(tailStart)
                        .arg(totalSectors - tailStart)
                        .arg(_loop)
                        .arg(tailStart);
        } else {
            // A fully writable device when asked for one, so a control case
            // can tell "the device failed" from "the harness is broken".
            table = goodSectors >= totalSectors
                ? QStringLiteral("0 %1 linear %2 0\n").arg(totalSectors).arg(_loop)
                : QStringLiteral("0 %1 linear %2 0\n%3 %4 error\n")
                      .arg(goodSectors)
                      .arg(_loop)
                      .arg(goodSectors)
                      .arg(totalSectors - goodSectors);
        }

        QProcess create;
        create.start(QStringLiteral("sudo"),
                     {QStringLiteral("-n"), QStringLiteral("dmsetup"), QStringLiteral("create"),
                      _name});
        if (!create.waitForStarted(10000))
            return;
        create.write(table.toUtf8());
        create.closeWriteChannel();
        create.waitForFinished(30000);
        if (create.exitCode() != 0)
            return;
        _mapped = true;

        // The device node is created by udev; give it a moment to appear.
        for (int i = 0; i < 100 && !QFileInfo::exists(path()); ++i)
            QThread::msleep(20);

        // Writable by this user, so the imager can open it without being root.
        runPrivileged(QStringLiteral("chmod"), {QStringLiteral("0666"), path()});
    }

public:
    ~FaultyDevice()
    {
        if (_mapped)
            runPrivileged(QStringLiteral("dmsetup"), {QStringLiteral("remove"), _name});
        if (!_loop.isEmpty())
            runPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
        QDir(_dir).removeRecursively();
    }

    FaultyDevice(const FaultyDevice &) = delete;
    FaultyDevice &operator=(const FaultyDevice &) = delete;

    bool isReady() const { return _mapped && QFileInfo::exists(path()); }
    QString path() const { return QStringLiteral("/dev/mapper/") + _name; }

private:
    QString _name;
    QString _dir;
    QString _backing;
    QString _loop;
    bool _mapped = false;
};


}  // namespace rpi_imager::testing

#endif  // RPI_IMAGER_TEST_FAULTY_BLOCK_DEVICE_H_
