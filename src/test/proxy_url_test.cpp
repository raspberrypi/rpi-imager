/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The proxy URL curl is handed.
 *
 * Detection asks the operating system what proxy is configured, and on a
 * build machine the answer is "none" -- so the part that decides what curl
 * is actually told had never run. A wrong answer here sends every request to
 * the wrong place, or drops the credentials that would have let it through,
 * on exactly the machines nobody developing this has.
 */

#include <catch2/catch_test_macros.hpp>

#include "proxy_url.h"

using rpi_imager::proxyUrlFor;

TEST_CASE("An HTTP proxy becomes an http URL", "[proxy]")
{
    const QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                              QStringLiteral("proxy.example.com"), 3128);
    CHECK(proxyUrlFor(proxy) == QByteArray("http://proxy.example.com:3128"));
}

TEST_CASE("A SOCKS proxy resolves names at the far end", "[proxy]")
{
    // socks5h rather than socks5. Resolving here leaks every hostname to the
    // local resolver, and fails outright for a name that only exists on the
    // other side of the proxy -- which is the usual reason for having one.
    const QNetworkProxy proxy(QNetworkProxy::Socks5Proxy,
                              QStringLiteral("socks.example.com"), 1080);
    const QByteArray url = proxyUrlFor(proxy);
    INFO("url: " << url.constData());
    CHECK(url.startsWith("socks5h://"));
    CHECK(url == QByteArray("socks5h://socks.example.com:1080"));
}

TEST_CASE("A proxy that wants credentials carries them", "[proxy]")
{
    // A corporate proxy that answers 407 otherwise, which the user sees as
    // every download failing for no stated reason.
    QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                        QStringLiteral("proxy.example.com"), 8080);
    proxy.setUser(QStringLiteral("someone"));
    proxy.setPassword(QStringLiteral("a-secret"));

    const QByteArray url = proxyUrlFor(proxy);
    INFO("url: " << url.constData());
    CHECK(url == QByteArray("http://someone:a-secret@proxy.example.com:8080"));
}

TEST_CASE("A password with characters a URL reserves is encoded", "[proxy]")
{
    // Passwords contain @ and : more often than not, and either one unencoded
    // ends the credentials early: the host becomes part of the password and
    // the request goes nowhere.
    QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                        QStringLiteral("proxy.example.com"), 8080);
    proxy.setUser(QStringLiteral("dom\\user"));
    proxy.setPassword(QStringLiteral("p@ss:word/with#parts"));

    const QByteArray url = proxyUrlFor(proxy);
    INFO("url: " << url.constData());
    // The host survives as the host, which is the thing that breaks.
    CHECK(url.endsWith("@proxy.example.com:8080"));
    // And what is read back out is what went in.
    const QUrl parsed = QUrl::fromEncoded(url);
    CHECK(parsed.host() == QStringLiteral("proxy.example.com"));
    CHECK(parsed.port() == 8080);
    CHECK(parsed.userName() == QStringLiteral("dom\\user"));
    CHECK(parsed.password() == QStringLiteral("p@ss:word/with#parts"));
}

TEST_CASE("A proxy with a user and no password is still carried", "[proxy]")
{
    QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                        QStringLiteral("proxy.example.com"), 8080);
    proxy.setUser(QStringLiteral("someone"));

    const QByteArray url = proxyUrlFor(proxy);
    INFO("url: " << url.constData());
    CHECK(QUrl::fromEncoded(url).userName() == QStringLiteral("someone"));
}

TEST_CASE("No proxy means nothing is given to curl", "[proxy]")
{
    // The ordinary case on nearly every machine. An empty answer leaves the
    // proxy unset rather than setting it to something meaningless.
    CHECK(proxyUrlFor(QNetworkProxy(QNetworkProxy::NoProxy)).isEmpty());
}

TEST_CASE("A proxy with no host is refused rather than half-built", "[proxy]")
{
    // systemProxyForQuery can hand back a typed proxy with nothing in it.
    // Built into a URL that is a scheme and nothing else, it would be set on
    // curl and every request would fail with no explanation.
    const QNetworkProxy proxy(QNetworkProxy::HttpProxy, QString(), 8080);
    CHECK(proxyUrlFor(proxy).isEmpty());
}

TEST_CASE("A proxy with no port keeps the scheme's own", "[proxy]")
{
    // Port 0 is what a proxy with no port set reports, and writing :0 into
    // the URL points curl at a port nothing listens on.
    const QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                              QStringLiteral("proxy.example.com"), 0);
    const QByteArray url = proxyUrlFor(proxy);
    INFO("url: " << url.constData());
    CHECK(url == QByteArray("http://proxy.example.com"));
    CHECK_FALSE(url.contains(":0"));
}
