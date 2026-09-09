/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The single place every libcurl handle in the application is configured.
 *
 * Three profiles share it: icons and JSON on short timeouts, OS images on
 * long ones with keepalive, and telemetry that is allowed to fail silently.
 * The proxy, the user agent and the IPv4-only fallback all live here so they
 * cannot drift between the fetch paths -- which is the whole point of the
 * class, and exactly what a test can check.
 *
 * A user behind a corporate proxy is the case that matters. Get this wrong
 * and nothing downloads, with no indication why.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "curlnetworkconfig.h"

#include <curl/curl.h>

#include "local_http_server.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

namespace {

// The config is a process-wide singleton, so each case puts back what it
// found rather than leaving the next one to inherit it.
class ConfigGuard
{
public:
    ConfigGuard()
        : _proxy(CurlNetworkConfig::instance().proxy()),
          _ua(CurlNetworkConfig::instance().userAgent()),
          _ipv4(CurlNetworkConfig::instance().ipv4Only())
    {
    }
    ~ConfigGuard()
    {
        CurlNetworkConfig::instance().setProxy(_proxy);
        CurlNetworkConfig::instance().setUserAgent(_ua);
        CurlNetworkConfig::instance().setIPv4Only(_ipv4);
    }

private:
    QByteArray _proxy, _ua;
    bool _ipv4;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("The configuration is one shared instance", "[curlconfig]")
{
    // Every fetch path reaches the same object; two would let the proxy and
    // the user agent drift apart between them.
    CHECK(&CurlNetworkConfig::instance() == &CurlNetworkConfig::instance());
}

TEST_CASE("libcurl is initialised once and stays initialised", "[curlconfig]")
{
    CHECK_NOTHROW(CurlNetworkConfig::ensureInitialized());
    CHECK_NOTHROW(CurlNetworkConfig::ensureInitialized());
}

TEST_CASE("A proxy can be set and read back", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();

    cfg.setProxy(QByteArray("http://proxy.example.com:3128"));
    CHECK(cfg.proxy() == QByteArray("http://proxy.example.com:3128"));

    cfg.setProxy(QByteArray());
    CHECK(cfg.proxy().isEmpty());
}

TEST_CASE("A proxy the user set is not replaced by detection", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();

    const QByteArray mine("http://proxy.example.com:3128");
    cfg.setProxy(mine);

    // Detection runs lazily on the first fetch. It must not overwrite a
    // proxy the user configured deliberately.
    cfg.detectSystemProxy(QUrl(QStringLiteral("https://downloads.raspberrypi.org/os_list.json")));
    CHECK(cfg.proxy() == mine);
}

TEST_CASE("Proxy detection is attempted only once", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();
    cfg.setProxy(QByteArray());

    const QUrl url(QStringLiteral("https://downloads.raspberrypi.org/os_list.json"));
    cfg.detectSystemProxy(url);
    const QByteArray afterFirst = cfg.proxy();

    // Querying the system proxy is slow on some platforms, and every icon
    // fetch would otherwise pay for it.
    cfg.detectSystemProxy(url);
    CHECK(cfg.proxy() == afterFirst);
}

TEST_CASE("The user agent is shared by every fetch path", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();

    // Not empty by default: the repository uses it to tell the imager apart
    // from a scraper.
    CHECK_FALSE(cfg.userAgent().isEmpty());

    cfg.setUserAgent(QByteArray("rpi-imager-test/1.0"));
    CHECK(cfg.userAgent() == QByteArray("rpi-imager-test/1.0"));
}

TEST_CASE("IPv4-only mode can be turned on and off", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();

    // The fallback for machines whose DNS returns AAAA records that do not
    // route; the OS list fetch flips this and retries.
    cfg.setIPv4Only(true);
    CHECK(cfg.ipv4Only());

    cfg.setIPv4Only(false);
    CHECK_FALSE(cfg.ipv4Only());
}

TEST_CASE("Every profile configures a handle without complaint", "[curlconfig]")
{
    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();
    CurlNetworkConfig::ensureInitialized();

    for (auto profile : {CurlNetworkConfig::FetchProfile::SmallFile,
                         CurlNetworkConfig::FetchProfile::LargeFile,
                         CurlNetworkConfig::FetchProfile::FireAndForget}) {
        CURL *handle = curl_easy_init();
        REQUIRE(handle != nullptr);
        CHECK_NOTHROW(cfg.applyCurlSettings(handle, profile));
        curl_easy_cleanup(handle);
    }
}

namespace {

size_t discard(char *, size_t size, size_t nmemb, void *) { return size * nmemb; }

// Fetches a URL through a handle configured by applyCurlSettings().
CURLcode fetchThroughConfig(const QByteArray &url, CurlNetworkConfig::FetchProfile profile)
{
    CURL *handle = curl_easy_init();
    REQUIRE(handle != nullptr);
    CurlNetworkConfig::instance().applyCurlSettings(handle, profile);
    curl_easy_setopt(handle, CURLOPT_URL, url.constData());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &discard);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 10L);
    const CURLcode rc = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    return rc;
}

} // namespace

TEST_CASE("A configured proxy is actually used", "[curlconfig]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    QFile f(QDir(served.path()).filePath(QStringLiteral("a.txt")));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();
    CurlNetworkConfig::ensureInitialized();

    const QByteArray url = server.urlFor(QStringLiteral("a.txt"));

    cfg.setProxy(QByteArray());
    REQUIRE(fetchThroughConfig(url, CurlNetworkConfig::FetchProfile::SmallFile) == CURLE_OK);

    // Port 1 on loopback refuses immediately. If the proxy setting did not
    // reach the handle the fetch would succeed regardless -- which is the
    // failure that leaves a user behind a corporate proxy unable to download
    // anything, with the setting apparently applied.
    cfg.setProxy(QByteArray("http://127.0.0.1:1"));
    const CURLcode viaProxy = fetchThroughConfig(url, CurlNetworkConfig::FetchProfile::SmallFile);
    INFO("result: " << curl_easy_strerror(viaProxy));
    CHECK(viaProxy != CURLE_OK);
}

TEST_CASE("The user agent is actually sent", "[curlconfig]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    QFile f(QDir(served.path()).filePath(QStringLiteral("a.txt")));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();
    CurlNetworkConfig::ensureInitialized();
    cfg.setProxy(QByteArray());
    cfg.setUserAgent(QByteArray("rpi-imager-test/1.0"));

    // Not an assertion on the server side -- it discards headers -- but the
    // fetch has to still work with the agent applied, and a malformed one
    // makes libcurl refuse the handle outright.
    CHECK(fetchThroughConfig(server.urlFor(QStringLiteral("a.txt")),
                             CurlNetworkConfig::FetchProfile::LargeFile) == CURLE_OK);
}

TEST_CASE("IPv4-only still reaches a loopback server", "[curlconfig]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    QFile f(QDir(served.path()).filePath(QStringLiteral("a.txt")));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    ConfigGuard guard;
    auto &cfg = CurlNetworkConfig::instance();
    CurlNetworkConfig::ensureInitialized();
    cfg.setProxy(QByteArray());

    // The fallback for machines whose DNS returns AAAA records that do not
    // route. It has to remain usable, not just settable.
    cfg.setIPv4Only(true);
    CHECK(fetchThroughConfig(server.urlFor(QStringLiteral("a.txt")),
                             CurlNetworkConfig::FetchProfile::SmallFile) == CURLE_OK);
}
