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
#include <QRegularExpression>
#include <QUuid>
#include <QtGlobal>

#include <memory>
#include "fixture_process.h"

#ifdef Q_OS_MACOS
#include <dlfcn.h>
#include <cstdint>
#endif

namespace rpi_imager::testing {

inline bool canRunPrivileged()
{
#ifdef Q_OS_MACOS
    return false;
#else
    QProcess probe;
    probe.start(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("true")});
    probe.waitForFinished(10000);
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
#endif
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

#ifdef Q_OS_MACOS
namespace detail {

using ArmFn      = int (*)(const char *, std::uint64_t, std::uint64_t, std::uint64_t,
                           std::uint64_t);
using DisarmFn   = void (*)();
using FailuresFn = std::uint64_t (*)();

inline ArmFn armFn()
{
    static ArmFn fn = reinterpret_cast<ArmFn>(dlsym(RTLD_DEFAULT, "rpi_faulty_arm"));
    return fn;
}

inline DisarmFn disarmFn()
{
    static DisarmFn fn = reinterpret_cast<DisarmFn>(dlsym(RTLD_DEFAULT, "rpi_faulty_disarm"));
    return fn;
}

inline FailuresFn failuresFn()
{
    static FailuresFn fn =
        reinterpret_cast<FailuresFn>(dlsym(RTLD_DEFAULT, "rpi_faulty_injected_failures"));
    return fn;
}

} // namespace detail
#endif

inline bool canInjectFaults()
{
#ifdef Q_OS_MACOS
    return detail::armFn() != nullptr;
#else
    return canRunPrivileged();
#endif
}

// -1 on Linux: the count is the kernel's.
inline qint64 faultsInjected()
{
#ifdef Q_OS_MACOS
    if (auto fn = detail::failuresFn())
        return static_cast<qint64>(fn());
#endif
    return -1;
}

// macOS runs async writes on a DISPATCH_QUEUE_SERIAL, so N delayed writes
// cost N delays there; io_uring overlaps them.
inline bool asyncWritesOverlap()
{
#ifdef Q_OS_MACOS
    return false;
#else
    return true;
#endif
}

// A plain loop device backed by a temporary file.
//
// A real block device, with the alignment rules, the direct-I/O path, the
// discard before the write and the size read back out of the kernel -- none
// of which a regular file reproduces, and all of which are what actually
// fails on a card.
class LoopDevice
{
public:
    explicit LoopDevice(int megabytes)
    {
#ifdef Q_OS_MACOS
        _dir = QDir::temp().filePath(
            QStringLiteral("rpi-imager-hdi-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
        if (!QDir().mkpath(_dir))
            return;

        _backing = QDir(_dir).filePath(QStringLiteral("backing.img"));
        QFile f(_backing);
        if (!f.open(QIODevice::WriteOnly) ||
            !f.resize(static_cast<qint64>(megabytes) * 1024 * 1024))
            return;
        f.close();

        QProcess attach;
        attach.start(QStringLiteral("/usr/bin/hdiutil"),
                     {QStringLiteral("attach"), QStringLiteral("-nomount"),
                      QStringLiteral("-imagekey"),
                      QStringLiteral("diskimage-class=CRawDiskImage"), _backing});
        if (!attach.waitForFinished(rpi_test::kFixtureProcessTimeoutMs) ||
            attach.exitCode() != 0)
            return;

        // First field of the first line: the whole-disk node.
        const QString block = QString::fromUtf8(attach.readAllStandardOutput())
                                  .section(QLatin1Char('\n'), 0, 0)
                                  .section(QRegularExpression(QStringLiteral("\\s")), 0, 0)
                                  .trimmed();
        if (!isDiskNode(block))
            return;
        _attached = block;

        // The raw node: it refuses non-sector-multiple I/O, as a card does.
        _loop = QStringLiteral("/dev/r") + block.mid(5);
        _ready = QFileInfo::exists(_loop);
#else
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
#endif
    }

    ~LoopDevice()
    {
#ifdef Q_OS_MACOS
        // May already be detached: a successful write ejects it.
        if (!_attached.isEmpty()) {
            QProcess detach;
            detach.start(QStringLiteral("/usr/bin/hdiutil"),
                         {QStringLiteral("detach"), _attached});
            detach.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
        }
#else
        if (!_loop.isEmpty())
            runPrivileged(QStringLiteral("losetup"), {QStringLiteral("-d"), _loop});
#endif
        if (!_dir.isEmpty())
            QDir(_dir).removeRecursively();
    }

    LoopDevice(const LoopDevice &) = delete;
    LoopDevice &operator=(const LoopDevice &) = delete;

    bool isReady() const { return _ready; }
    QString path() const { return _loop; }

    // The node on Linux; the backing file on macOS, whose node the write
    // under test ejects.
#ifdef Q_OS_MACOS
    QString readPath() const { return _backing; }

    static bool isDiskNode(const QString &s)
    {
        static const QRegularExpression re(QStringLiteral("^/dev/disk[0-9]+$"));
        return re.match(s).hasMatch();
    }
#else
    QString readPath() const { return _loop; }
#endif

private:
    QString _dir, _loop, _backing, _attached;
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
        if (!fromEnv.isEmpty() && isSafeToWrite(QString::fromLatin1(fromEnv))) {
            _path = QString::fromLatin1(fromEnv);
            _readPath = _path;
            return;
        }
        _owned = std::make_unique<LoopDevice>(megabytes);
        if (_owned->isReady()) {
            _path = _owned->path();
            _readPath = _owned->readPath();
        }
    }

    bool isReady() const { return !_path.isEmpty(); }
    QString path() const { return _path; }
    QString readPath() const { return _readPath; }

private:
    // On Linux the name is proof: only a loop device is /dev/loopN. On
    // macOS /dev/rdisk2 may be the user's system disk, so hdiutil is asked
    // whether it is image-backed.
    static bool isSafeToWrite(const QString &device)
    {
#ifdef Q_OS_MACOS
        static const QRegularExpression re(QStringLiteral("^/dev/r?disk[0-9]+$"));
        if (!re.match(device).hasMatch())
            return false;

        QProcess info;
        info.start(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("info")});
        if (!info.waitForFinished(rpi_test::kFixtureProcessTimeoutMs) ||
            info.exitCode() != 0)
            return false;

        // hdiutil names the block node; compare on that.
        QString block = device;
        block.remove(QStringLiteral("/dev/r"));
        block.prepend(QStringLiteral("/dev/"));
        block.replace(QStringLiteral("/dev//dev/"), QStringLiteral("/dev/"));

        const QString out = QString::fromUtf8(info.readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n'))) {
            if (line.section(QRegularExpression(QStringLiteral("\\s")), 0, 0) == block)
                return true;
        }
        return false;
#else
        return device.startsWith(QStringLiteral("/dev/loop"));
#endif
    }

    std::unique_ptr<LoopDevice> _owned;
    QString _path;
    QString _readPath;
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

    FaultyDevice(const FaultyDevice &) = delete;
    FaultyDevice &operator=(const FaultyDevice &) = delete;

private:
    static constexpr qint64 kMB = 1024 * 1024;

#ifdef Q_OS_MACOS
    // A regular file, not /dev/rdiskN, per authopen above. Sized
    // exactly: the writer fstats it.
    FaultyDevice(int totalMegabytes, int goodMegabytes, int bandStartMB, int bandLenMB,
                 int writeDelayMs)
    {
        const auto arm = detail::armFn();
        if (!arm)
            return;

        _dir = QDir::temp().filePath(
            QStringLiteral("rpi-imager-faulty-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().mkpath(_dir))
            return;
        _path = QDir(_dir).filePath(QStringLiteral("backing.img"));

        const qint64 totalBytes = static_cast<qint64>(totalMegabytes) * kMB;
        QFile f(_path);
        if (!f.open(QIODevice::WriteOnly) || !f.resize(totalBytes))
            return;
        f.close();

        qint64 badStart = 0, badLen = 0;
        if (writeDelayMs > 0) {
            // Nothing fails; writes just take time.
        } else if (bandStartMB >= 0) {
            // good | error | good
            badStart = static_cast<qint64>(bandStartMB) * kMB;
            badLen = static_cast<qint64>(bandLenMB) * kMB;
            if (badStart + badLen > totalBytes)
                return;
        } else if (goodMegabytes < totalMegabytes) {
            // good | error, to the end.
            badStart = static_cast<qint64>(goodMegabytes) * kMB;
            badLen = totalBytes - badStart;
        }
        // else: fully writable, separating device from harness.

        if (arm(_path.toLocal8Bit().constData(), static_cast<std::uint64_t>(totalBytes),
                static_cast<std::uint64_t>(badStart), static_cast<std::uint64_t>(badLen),
                static_cast<std::uint64_t>(writeDelayMs) * 1000000ull) != 0)
            return;

        _armed = true;
    }

public:
    ~FaultyDevice()
    {
        // Process-wide state: a fault left set would follow later tests.
        if (_armed) {
            if (auto disarm = detail::disarmFn())
                disarm();
        }
        if (!_dir.isEmpty())
            QDir(_dir).removeRecursively();
    }

    bool isReady() const { return _armed && QFileInfo::exists(_path); }
    QString path() const { return _path; }

private:
    QString _dir;
    QString _path;
    bool _armed = false;

#else  // Linux: a device-mapper table over a loop device.

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
            !f.resize(static_cast<qint64>(totalMegabytes) * kMB))
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

        const qint64 goodSectors = static_cast<qint64>(goodMegabytes) * kMB / 512;
        const qint64 totalSectors = static_cast<qint64>(totalMegabytes) * kMB / 512;
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
            const qint64 bandStart = static_cast<qint64>(bandStartMB) * kMB / 512;
            const qint64 bandLen = static_cast<qint64>(bandLenMB) * kMB / 512;
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

    bool isReady() const { return _mapped && QFileInfo::exists(path()); }
    QString path() const { return QStringLiteral("/dev/mapper/") + _name; }

private:
    QString _name;
    QString _dir;
    QString _backing;
    QString _loop;
    bool _mapped = false;
#endif
};

}  // namespace rpi_imager::testing

#endif  // RPI_IMAGER_TEST_FAULTY_BLOCK_DEVICE_H_
