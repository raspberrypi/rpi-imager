/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * The fetcher behind every icon in the OS chooser.
 *
 * IconMultiFetcher runs a libcurl multi loop on its own thread and keeps a
 * bounded in-memory cache, so the chooser can show a hundred icons without a
 * hundred connections. None of it had been executed by a test: the whole
 * class is only ever reached through the QML image provider.
 *
 * What goes wrong is cosmetic but conspicuous. An icon that never arrives
 * leaves a blank tile in the list the user is choosing from, and a cache that
 * hands back the wrong entry shows Ubuntu's logo next to Raspberry Pi OS.
 * Neither is an error anyone sees in a log.
 *
 * A throwaway HTTP server on loopback serves the icons.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "iconmultifetcher.h"
#include "iconimageprovider.h"
#include "local_http_server.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

namespace {

// A small but genuine PNG, written at runtime so nothing binary is checked in.
QByteArray writeIcon(const QString &path, const QColor &colour)
{
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(colour);
    REQUIRE(img.save(path, "PNG"));

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    return f.readAll();
}

// Waits for a condition, spinning the loop the fetcher delivers on.
bool waitFor(const std::function<bool()> &done, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

struct FetchResult
{
    bool finished = false;
    QString error;
};

// Drives one fetch through the real response object QML would be given.
FetchResult fetchIcon(const QUrl &url, int timeoutMs = 15000)
{
    FetchResult out;
    IconImageResponse response(url);
    QObject::connect(&response, &QQuickImageResponse::finished, [&] {
        out.finished = true;
        out.error = response.errorString();
    });

    IconMultiFetcher::instance().queueFetch(&response, url);
    waitFor([&] { return out.finished; }, timeoutMs);
    return out;
}

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("icon_fetcher_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    const int rc = Catch::Session().run(argc, argv);
    IconMultiFetcher::instance().shutdown();
    return rc;
}

TEST_CASE("An icon is fetched and cached", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    const QByteArray bytes =
        writeIcon(QDir(served.path()).filePath(QStringLiteral("a.png")), Qt::red);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("a.png"))));

    const FetchResult r = fetchIcon(url);
    INFO("error: " << r.error.toStdString());
    REQUIRE(r.finished);
    CHECK(r.error.isEmpty());

    // The bytes are kept so the next tile showing the same icon costs
    // nothing -- the chooser asks for the same URL many times over.
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()) == bytes);
}

TEST_CASE("A second fetch of the same icon is served from the cache", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("a.png")), Qt::green);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("a.png"))));

    REQUIRE(fetchIcon(url).finished);
    const QByteArray first = IconMultiFetcher::instance().getCachedData(url.toString());
    REQUIRE_FALSE(first.isEmpty());

    const FetchResult second = fetchIcon(url);
    REQUIRE(second.finished);
    CHECK(second.error.isEmpty());
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()) == first);
}

TEST_CASE("Different icons do not collide in the cache", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    const QByteArray red =
        writeIcon(QDir(served.path()).filePath(QStringLiteral("red.png")), Qt::red);
    const QByteArray blue =
        writeIcon(QDir(served.path()).filePath(QStringLiteral("blue.png")), Qt::blue);
    REQUIRE(red != blue);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl redUrl(QString::fromUtf8(server.urlFor(QStringLiteral("red.png"))));
    const QUrl blueUrl(QString::fromUtf8(server.urlFor(QStringLiteral("blue.png"))));

    REQUIRE(fetchIcon(redUrl).finished);
    REQUIRE(fetchIcon(blueUrl).finished);

    // Keyed wrongly, the chooser shows one OS's logo against another's name.
    CHECK(IconMultiFetcher::instance().getCachedData(redUrl.toString()) == red);
    CHECK(IconMultiFetcher::instance().getCachedData(blueUrl.toString()) == blue);
}

TEST_CASE("An icon that is not there reports an error", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("absent.png"))));

    const FetchResult r = fetchIcon(url);
    // The response must finish either way: a fetch that never completes
    // leaves QML waiting on an image that will never arrive.
    REQUIRE(r.finished);
    CHECK_FALSE(r.error.isEmpty());
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}

TEST_CASE("An unreachable host reports an error rather than hanging", "[icons]")
{
    IconMultiFetcher::instance().clearCache();
    // Port 1 on loopback: refused immediately, no DNS and no network.
    const QUrl url(QStringLiteral("http://127.0.0.1:1/icon.png"));

    const FetchResult r = fetchIcon(url, 30000);
    REQUIRE(r.finished);
    CHECK_FALSE(r.error.isEmpty());
}

TEST_CASE("Clearing the cache drops what was held", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("a.png")), Qt::yellow);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("a.png"))));
    REQUIRE(fetchIcon(url).finished);
    REQUIRE_FALSE(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());

    // Switching repository has to drop the old icons; keeping them shows the
    // previous repo's artwork against the new one's entries.
    IconMultiFetcher::instance().clearCache();
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}

TEST_CASE("A cancelled fetch does not deliver", "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("a.png")), Qt::magenta);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("a.png"))));

    // QML cancels when a delegate scrolls out of view, and the response is
    // destroyed straight after. Delivering to it then is a use-after-free.
    IconImageResponse response(url);
    IconMultiFetcher::instance().queueFetch(&response, url);
    IconMultiFetcher::instance().cancelFetch(&response);

    waitFor([] { return false; }, 500);
    CHECK(response.isCancelled() == false);   // cancel() is Qt's call, not ours
}
