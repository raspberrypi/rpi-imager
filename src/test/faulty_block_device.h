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

#include <memory>
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

// A plain loop device backed by a temporary file.
//
// A real block device, with the alignment rules, the direct-I/O path, the
// discard before the write and the size read back out of the kernel -- none
// of which a regular file reproduces, and all of which are what actually
// fails on a card.
//
// The device tests took theirs from RPI_IMAGER_TEST_BLOCK_DEVICE and skipped
// when it was unset, so the write path was only exercised by someone who had
// set a device up by hand. In practice that meant never: the whole group sat
// skipped in every run. This provisions one where the suite is allowed to,
// and still skips where it is not.
class LoopDevice
{
public:
    explicit LoopDevice(int megabytes)
    {
        if (!canRunPrivileged())
            return;

        _dir = QDir::temp().filePath(
            QStringLiteral("rpi-imager-loop-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
        if (!QDir().mkpath(_dir))
            return;

        const QString backing = QDir(_dir).filePath(QStringLiteral("backing.img"));
        QFile f(backing);
        if (!f.open(QIODevice::WriteOnly) ||
            !f.resize(static_cast<qint64>(megabytes) * 1024 * 1024))
            return;
        f.close();

        // -f only ever returns a device that is not already attached, so this
        // cannot land on anything of the user's.
        QByteArray out;
        if (!runPrivileged(QStringLiteral("losetup"),
                           {QStringLiteral("-f"), QStringLiteral("--show"), backing}, &out))
            return;
        _loop = QString::fromUtf8(out).trimmed();
        if (!_loop.startsWith(QStringLiteral("/dev/loop"))) {
            _loop.clear();
            return;
        }

        // Writable by this user, so the imager opens it without being root.
        runPrivileged(QStringLiteral("chmod"), {QStringLiteral("0666"), _loop});
        _ready = QFileInfo::exists(_loop);
    }

    ~LoopDevice()
    {
        if (!_loop.isEmpty())
            runPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
        if (!_dir.isEmpty())
            QDir(_dir).removeRecursively();
    }

    LoopDevice(const LoopDevice &) = delete;
    LoopDevice &operator=(const LoopDevice &) = delete;

    bool isReady() const { return _ready; }
    QString path() const { return _loop; }

private:
    QString _dir, _loop;
    bool _ready = false;
};

// The device a test should write to: one named in the environment if the
// runner supplied it, otherwise one provisioned here. Refuses anything that
// is not a loop device however it arrived, so a mistyped environment variable
// cannot point the write path at a real disk.
class TestBlockDevice
{
public:
    explicit TestBlockDevice(int megabytes)
    {
        const QByteArray fromEnv = qgetenv("RPI_IMAGER_TEST_BLOCK_DEVICE");
        if (fromEnv.startsWith("/dev/loop")) {
            _path = QString::fromLatin1(fromEnv);
            return;
        }
        _owned = std::make_unique<LoopDevice>(megabytes);
        if (_owned->isReady())
            _path = _owned->path();
    }

    bool isReady() const { return !_path.isEmpty(); }
    QString path() const { return _path; }

private:
    std::unique_ptr<LoopDevice> _owned;
    QString _path;
};

// Teardown for a device something may still be finishing with: retry for a
// while before giving up, so a slow write in flight does not leak the mapping.
inline bool retryPrivileged(const QString &program, const QStringList &args)
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (runPrivileged(program, args))
            return true;
        QThread::msleep(500);
    }
    return false;
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

    // A device that takes every write but takes its time over it: a card whose
    // controller is slow rather than broken, which is a different failure for
    // the writer to handle. dm-delay holds each bio for the requested number
    // of milliseconds before passing it down, and reads are left at full speed
    // so a test can still check what landed without paying the delay twice.
    struct SlowWrites
    {
        int milliseconds;
    };

    FaultyDevice(int totalMegabytes, BadBand band)
        : FaultyDevice(totalMegabytes, -1, band.startMegabytes, band.lengthMegabytes, 0)
    {
    }

    explicit FaultyDevice(int totalMegabytes, int goodMegabytes)
        : FaultyDevice(totalMegabytes, goodMegabytes, -1, -1, 0)
    {
    }

    FaultyDevice(int totalMegabytes, SlowWrites slow)
        : FaultyDevice(totalMegabytes, -1, -1, -1, slow.milliseconds)
    {
    }

private:
    FaultyDevice(int totalMegabytes, int goodMegabytes, int bandStartMB, int bandLenMB,
                 int writeDelayMs)
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
        if (writeDelayMs > 0) {
            // dm-delay is a separate module from dm-mod, and dmsetup does not
            // always pull it in on its own. Best effort: if it is unavailable
            // the create below fails and isReady() stays false, which is what
            // the caller skips on anyway.
            runPrivileged(QStringLiteral("modprobe"), {QStringLiteral("dm-delay")});
            // <read dev> <read offset> <read delay> <write dev> <write offset>
            // <write delay>. Reads pass straight through.
            table = QStringLiteral("0 %1 delay %2 0 0 %2 0 %3\n")
                        .arg(totalSectors)
                        .arg(_loop)
                        .arg(writeDelayMs);
        } else if (bandStartMB >= 0) {
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
        // Both of these fail with EBUSY while an I/O is still outstanding
        // against the device, which is exactly the state a test of the write
        // timeouts leaves behind: the code under test closes the handle and
        // walks away, and the write it abandoned is still in the block layer.
        // Giving up on the first refusal would leak a mapping and a loop
        // device onto the machine running the suite.
        if (_mapped)
            retryPrivileged(QStringLiteral("dmsetup"), {QStringLiteral("remove"), _name});
        if (!_loop.isEmpty())
            retryPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
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
