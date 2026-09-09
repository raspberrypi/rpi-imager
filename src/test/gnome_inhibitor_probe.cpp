/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for GnomeSuspendInhibitor, the other half of what stops the machine
 * going to sleep while a card is being written.
 *
 * On a GNOME session the inhibit is not a process holding a FIFO open, it is
 * a D-Bus call to org.gnome.SessionManager, and the cookie it returns is the
 * only handle on it. Two things can go wrong and neither is visible from
 * inside the application: the flags can ask for too little, so the machine
 * suspends anyway; or the cookie can fail to come back, so the inhibit is
 * never released and the machine cannot sleep afterwards either.
 *
 * Reaching that needs a session manager to talk to. This provides one --
 * a stub registered on whatever bus DBUS_SESSION_BUS_ADDRESS points at,
 * which the driver runs under dbus-run-session -- and records what it was
 * asked for. It runs in a child process rather than this one because the
 * inhibitor's calls are blocking, and a blocking call to a service living on
 * the calling thread has nothing to answer it.
 *
 * Modes:
 *   stub <logfile>   register org.gnome.SessionManager and record calls
 *   drive <logfile>  start the stub, hold and release an inhibit, report
 *   bare             hold and release with no session manager present
 *
 * Prints KEY=value lines on stdout; anything else goes to stderr.
 */

#include "linux/linux_suspend_inhibitor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <optional>

namespace {

constexpr const char *kService = "org.gnome.SessionManager";
constexpr const char *kPath    = "/org/gnome/SessionManager";

// The cookie the stub hands out. Any non-zero value would do; a distinctive
// one makes it obvious in the log that the number came from here and was
// carried back rather than being a default that happened to match.
constexpr uint kCookie = 0xC0DE;

void appendLine(const QString &logPath, const QString &line)
{
    QFile f(logPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream(&f) << line << '\n';
    f.close();
}

} // namespace

// A stand-in for GNOME's session manager: enough of it to answer an inhibit.
class SessionManagerStub : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.SessionManager")

public:
    explicit SessionManagerStub(QString logPath, QObject *parent = nullptr)
        : QObject(parent), _logPath(std::move(logPath))
    {
    }

public slots:
    uint Inhibit(const QString &appId, uint toplevelXid, const QString &reason, uint flags)
    {
        appendLine(_logPath, QStringLiteral("INHIBIT app=%1 xid=%2 reason=%3 flags=%4 cookie=%5")
                                 .arg(appId)
                                 .arg(toplevelXid)
                                 .arg(reason)
                                 .arg(flags)
                                 .arg(kCookie));
        return kCookie;
    }

    void Uninhibit(uint cookie)
    {
        appendLine(_logPath, QStringLiteral("UNINHIBIT cookie=%1").arg(cookie));
    }

    void Quit() { QCoreApplication::quit(); }

private:
    QString _logPath;
};

namespace {

int runStub(const QString &logPath)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "stub: no session bus\n");
        return 3;
    }

    auto *stub = new SessionManagerStub(logPath, QCoreApplication::instance());
    if (!bus.registerObject(QString::fromLatin1(kPath), stub,
                            QDBusConnection::ExportAllSlots)) {
        std::fprintf(stderr, "stub: could not export the object\n");
        return 4;
    }
    if (!bus.registerService(QString::fromLatin1(kService))) {
        std::fprintf(stderr, "stub: could not claim the name\n");
        return 5;
    }

    // A stub that outlived its driver would keep the bus alive and the test
    // waiting, so it gives up on its own.
    QTimer::singleShot(30000, QCoreApplication::instance(), &QCoreApplication::quit);
    return QCoreApplication::exec();
}

// Wait for the stub to claim the name. Registration is asynchronous from
// here: starting the inhibitor first would find no service and prove nothing.
bool waitForService(int timeoutMs)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        if (bus.isConnected() && bus.interface() &&
            bus.interface()->isServiceRegistered(QString::fromLatin1(kService)).value())
            return true;
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    return false;
}

int runDriver(const QString &probeBinary, const QString &logPath)
{
    QProcess stub;
    stub.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    stub.start(probeBinary, {QStringLiteral("stub"), logPath});
    if (!stub.waitForStarted(10000)) {
        std::fprintf(stderr, "drive: the stub would not start\n");
        return 6;
    }

    if (!waitForService(10000)) {
        std::fprintf(stderr, "drive: the stub never claimed the name\n");
        stub.kill();
        stub.waitForFinished(5000);
        return 7;
    }
    std::printf("SERVICE_PRESENT=1\n");
    std::fflush(stdout);

    {
        // Exactly what the writer does: hold one for the length of the write,
        // and let it go at the end.
        GnomeSuspendInhibitor inhibitor;
    }

    // The Uninhibit is a fire-and-forget call from the destructor; give the
    // bus a moment to deliver it before reading what the stub saw.
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 2000) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
        QFile f(logPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text) &&
            QString::fromUtf8(f.readAll()).contains(QStringLiteral("UNINHIBIT")))
            break;
    }

    QDBusConnection::sessionBus().call(
        QDBusMessage::createMethodCall(QString::fromLatin1(kService),
                                       QString::fromLatin1(kPath),
                                       QString::fromLatin1(kService),
                                       QStringLiteral("Quit")),
        QDBus::NoBlock);
    if (!stub.waitForFinished(10000)) {
        stub.kill();
        stub.waitForFinished(5000);
    }

    QFile f(logPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines =
            QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            std::printf("SAW=%s\n", line.toUtf8().constData());
    }
    std::fflush(stdout);
    return 0;
}

// No session manager on the bus at all, which is every desktop that is not
// GNOME. Constructing and destroying the inhibitor must be a no-op rather
// than a hang or a crash: the other two inhibitors are what does the work
// there, and this one is constructed unconditionally alongside them.
int runBare()
{
    std::printf("BUS_CONNECTED=%d\n", QDBusConnection::sessionBus().isConnected() ? 1 : 0);
    {
        GnomeSuspendInhibitor inhibitor;
    }
    std::printf("SURVIVED=1\n");
    std::fflush(stdout);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();

    if (args.size() >= 2 && args[1] == QLatin1String("bare"))
        return runBare();
    if (args.size() >= 3 && args[1] == QLatin1String("stub"))
        return runStub(args[2]);
    if (args.size() >= 3 && args[1] == QLatin1String("drive"))
        return runDriver(args[0], args[2]);

    std::fprintf(stderr, "usage: %s stub|drive <logfile> | bare\n", argv[0]);
    return 2;
}

#include "gnome_inhibitor_probe.moc"
