/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The callback relay, run as the executable Windows actually starts.
 *
 * callback_relay_url_test covers what it accepts. This covers what it then
 * does with it: convert to UTF-8 and hand it to the running instance over the
 * loopback port. That path lives in the WIN32 executable itself, which nothing
 * links, so none of it was measured.
 *
 * Every case here binds the port first. The relay falls back to launching
 * rpi-imager.exe when the send fails, and a test must not start the
 * application on the machine running it -- with something listening, the send
 * succeeds and the fallback is never reached. A URL the relay refuses returns
 * before the socket is touched, so those cases cannot launch anything either.
 */

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

namespace {

// The port the relay is compiled to talk to.
constexpr quint16 kRelayPort = 49629;

QString relayPath() { return QStringLiteral(RPI_IMAGER_RELAY_EXE); }

#define REQUIRE_RELAY()                                                        \
    if (!QFileInfo::exists(relayPath()))                                       \
    SKIP("the callback relay was not built")

// A listener on the relay's port, standing in for a running Imager.
class FakeImager
{
public:
    FakeImager() { _listening = _server.listen(QHostAddress::LocalHost, kRelayPort); }

    bool isListening() const { return _listening; }

    // Whatever the relay sends, or empty if it sends nothing in time.
    QByteArray awaitDelivery(int timeoutMs = 5000)
    {
        if (!_server.waitForNewConnection(timeoutMs))
            return {};
        QTcpSocket *client = _server.nextPendingConnection();
        if (!client)
            return {};
        QByteArray got;
        // The relay closes once it has written, so read until it does.
        while (client->state() == QAbstractSocket::ConnectedState) {
            if (!client->waitForReadyRead(timeoutMs))
                break;
            got += client->readAll();
        }
        got += client->readAll();
        client->deleteLater();
        return got;
    }

private:
    QTcpServer _server;
    bool _listening = false;
};

#define REQUIRE_PORT(imager)                                                   \
    if (!(imager).isListening())                                               \
    SKIP("port 49629 is already taken, most likely by a running Imager")

int runRelay(const QString &url)
{
    QProcess relay;
    relay.start(relayPath(), {url});
    if (!relay.waitForStarted(5000))
        return -1;
    if (!relay.waitForFinished(15000)) {
        relay.kill();
        relay.waitForFinished(5000);
        return -1;
    }
    return relay.exitCode();
}

} // namespace

TEST_CASE("The relay hands a callback URL to the running instance",
          "[relay][process]")
{
    REQUIRE_RELAY();
    FakeImager imager;
    REQUIRE_PORT(imager);

    const QString url = QStringLiteral("rpi-imager://open?token=abc123");
    REQUIRE(runRelay(url) == 0);

    CHECK(imager.awaitDelivery() == url.toUtf8());
}

TEST_CASE("The URL arrives as UTF-8, not as a narrowed approximation",
          "[relay][process][i18n]")
{
    // The relay holds the URL as UTF-16 and converts before sending. A
    // narrowing conversion would put question marks where the characters were,
    // and the instance would act on a URL nobody asked for.
    REQUIRE_RELAY();
    FakeImager imager;
    REQUIRE_PORT(imager);

    const QString url =
        QStringLiteral("rpi-imager://open?name=測試-файл");
    REQUIRE(runRelay(url) == 0);

    const QByteArray got = imager.awaitDelivery();
    INFO("got: " << got.toStdString());
    CHECK(got == url.toUtf8());
    // Named separately from the comparison above: a narrowing conversion
    // substitutes a question mark per character, and the URL carries one of
    // those legitimately, ahead of the query.
    CHECK(got.endsWith(QStringLiteral("測試-файл").toUtf8()));
}

TEST_CASE("Nothing is sent for a URL of another scheme", "[relay][process]")
{
    REQUIRE_RELAY();
    FakeImager imager;
    REQUIRE_PORT(imager);

    // Refused before the socket is opened, so the launch fallback is not
    // reached either -- which is what makes this safe to run at all.
    CHECK(runRelay(QStringLiteral("http://example.com/")) == 0);
    CHECK(imager.awaitDelivery(1500).isEmpty());
}

TEST_CASE("Nothing is sent for a URL carrying a control character",
          "[relay][process]")
{
    REQUIRE_RELAY();
    FakeImager imager;
    REQUIRE_PORT(imager);

    CHECK(runRelay(QStringLiteral("rpi-imager://open\nsecond-line")) == 0);
    CHECK(imager.awaitDelivery(1500).isEmpty());
}

TEST_CASE("A relay started with no argument does nothing and says so",
          "[relay][process]")
{
    REQUIRE_RELAY();
    FakeImager imager;
    REQUIRE_PORT(imager);

    QProcess relay;
    relay.start(relayPath(), QStringList{});
    REQUIRE(relay.waitForStarted(5000));
    REQUIRE(relay.waitForFinished(15000));
    CHECK(relay.exitCode() == 0);
    CHECK(imager.awaitDelivery(1500).isEmpty());
}

// ── starting an Imager that is not already running ──────────────────────────
//
// With nothing listening, the relay starts the Imager beside it and hands the
// URL over as an argument. That is Pi Connect sign-in for anybody whose
// Imager is not open, and none of it ran: reaching it means launching the
// real binary, which asks for administrator and would put a UAC prompt in
// front of the suite.
//
// So the relay is copied somewhere of its own and given a stand-in to launch.
// What that proves is what the relay decides -- which directory it looks in,
// what it passes on, and when it declines -- not what the Imager then does.

namespace {

// A directory holding a copy of the relay, and optionally something for it to
// start. Named to match what the relay looks for beside itself.
class RelayScratch
{
public:
    RelayScratch()
    {
        if (!_dir.isValid())
            return;
        _relay = QDir(_dir.path()).filePath(QStringLiteral("relay.exe"));
        _copied = QFile::copy(relayPath(), _relay);
        _record = QDir(_dir.path()).filePath(QStringLiteral("launched.txt"));
    }

    bool isReady() const { return _dir.isValid() && _copied; }

    // Put the stand-in where the relay will look for the Imager.
    bool installProbe()
    {
        return QFile::copy(QStringLiteral(RPI_RELAY_LAUNCH_PROBE),
                           QDir(_dir.path()).filePath(QStringLiteral("rpi-imager.exe")));
    }

    int run(const QString &url)
    {
        QProcess relay;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("RPI_RELAY_PROBE_OUTPUT"), _record);
        relay.setProcessEnvironment(env);
        relay.start(_relay, {url});
        if (!relay.waitForStarted(5000))
            return -1;
        if (!relay.waitForFinished(15000)) {
            relay.kill();
            relay.waitForFinished(5000);
            return -1;
        }
        return relay.exitCode();
    }

    // What the stand-in was started with, once it has had a moment to write.
    // ShellExecuteEx returns before the child has run, so this waits rather
    // than reading an empty file and calling it a failure to launch.
    QByteArray launchedWith(int timeoutMs = 10000) const
    {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < timeoutMs) {
            QFile f(_record);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray got = f.readAll();
                if (!got.isEmpty())
                    return got;
            }
            QThread::msleep(100);
        }
        return {};
    }

private:
    QTemporaryDir _dir;
    QString _relay, _record;
    bool _copied = false;
};

// Nothing on the relay's port, so the send fails and the fallback runs. A
// real Imager holding it would take the URL instead and the launch would
// never be reached.
bool portIsFree()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, kRelayPort))
        return false;
    probe.close();
    return true;
}

} // namespace

TEST_CASE("With no Imager listening, the relay starts the one beside it",
          "[relay][process][launch]")
{
    REQUIRE_RELAY();
    if (!portIsFree())
        SKIP("port 49629 is taken, so the relay would deliver rather than launch");

    RelayScratch scratch;
    if (!scratch.isReady())
        SKIP("the relay could not be copied to a directory of its own");
    REQUIRE(scratch.installProbe());

    const QString url = QStringLiteral("rpi-imager://open?token=launched123");
    CHECK(scratch.run(url) == 0);

    // The URL reaches it as one argument. Quoted by ShellExecuteEx, so the
    // check is for the URL within the command line rather than equal to it.
    const QByteArray started = scratch.launchedWith();
    INFO("command line: " << started.toStdString());
    CHECK_FALSE(started.isEmpty());
    CHECK(started.contains(url.toUtf8()));
}

TEST_CASE("A relay with no Imager beside it launches nothing",
          "[relay][process][launch]")
{
    // The guard against starting whatever happens to be in the working
    // directory. Nothing is installed here, so the relay has to find the
    // Imager missing and stop.
    REQUIRE_RELAY();
    if (!portIsFree())
        SKIP("port 49629 is taken, so the relay would deliver rather than launch");

    RelayScratch scratch;
    if (!scratch.isReady())
        SKIP("the relay could not be copied to a directory of its own");

    CHECK(scratch.run(QStringLiteral("rpi-imager://open?token=nothing")) == 0);
    CHECK(scratch.launchedWith(1500).isEmpty());
}
