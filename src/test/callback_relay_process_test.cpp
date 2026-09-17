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
#include <QFileInfo>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>

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
