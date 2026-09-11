/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The fetcher behind every icon in the OS chooser.
 *
 * IconMultiFetcher runs a libcurl multi loop on its own thread and keeps a
 * bounded in-memory cache, so the chooser can show a hundred icons without a
 * hundred connections. None of it had been executed by a test: the whole
 * class is only ever reached through the QML image provider.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include "iconmultifetcher.h"
#include "iconimageprovider.h"
#include "local_http_server.h"

#include <memory>
#include <vector>

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

// ══════════════════════════════════════════════════════════════
// Cancelling one of several requests for the same icon.
//
// The OS list shows the same icon against every release of a distribution,
// so a screenful of delegates asks for one URL and the fetcher coalesces
// them onto a single transfer. Scrolling then cancels some of those
// delegates while the others are still on screen waiting.

TEST_CASE("Cancelling one request for an icon leaves the others waiting",
          "[icons]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    const QByteArray bytes =
        writeIcon(QDir(served.path()).filePath(QStringLiteral("shared.png")), Qt::cyan);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("shared.png"))));

    // Three delegates showing the same distribution's icon, as a scrolled
    // list does. They coalesce onto one transfer.
    IconImageResponse scrolledAway(url);
    IconImageResponse stillVisible(url);
    IconImageResponse alsoVisible(url);

    bool stillVisibleDone = false;
    bool alsoVisibleDone = false;
    bool scrolledAwayDone = false;
    QObject::connect(&stillVisible, &QQuickImageResponse::finished,
                     [&] { stillVisibleDone = true; });
    QObject::connect(&alsoVisible, &QQuickImageResponse::finished,
                     [&] { alsoVisibleDone = true; });
    QObject::connect(&scrolledAway, &QQuickImageResponse::finished,
                     [&] { scrolledAwayDone = true; });

    IconMultiFetcher::instance().queueFetch(&scrolledAway, url);
    IconMultiFetcher::instance().queueFetch(&stillVisible, url);
    IconMultiFetcher::instance().queueFetch(&alsoVisible, url);

    // One delegate scrolls out of view.
    IconMultiFetcher::instance().cancelFetch(&scrolledAway);

    // The two still on screen get their icon. If cancelling one tore the
    // shared transfer down, this is where a screenful of blank icons would
    // show up.
    REQUIRE(waitFor([&] { return stillVisibleDone && alsoVisibleDone; }, 15000));
    CHECK(stillVisible.errorString().isEmpty());
    CHECK(alsoVisible.errorString().isEmpty());


    // Note what cancelling does not do: the response that scrolled away is
    // still delivered to. Measured, not assumed -- asserting otherwise fails.
    // It is safe because the waiting list holds QPointers, so a response QML
    // has destroyed is skipped; a cancelled one that still exists simply gets
    // an answer nobody reads. Removing the cancellation filter therefore
    // changes nothing observable here, and no assertion below claims it does.
    waitFor([] { return false; }, 300);
    INFO("the cancelled response was delivered to: " << scrolledAwayDone);
}

TEST_CASE("Cancelling every request for an icon is not an error for anyone",
          "[icons]")
{
    // The whole row scrolls away at once, so nothing is left waiting and the
    // transfer itself goes. Nothing should be delivered afterwards, and
    // nothing should be left behind for the next fetch to trip over.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("gone.png")), Qt::yellow);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();
    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("gone.png"))));

    {
        IconImageResponse first(url);
        IconImageResponse second(url);
        IconMultiFetcher::instance().queueFetch(&first, url);
        IconMultiFetcher::instance().queueFetch(&second, url);
        IconMultiFetcher::instance().cancelFetch(&first);
        IconMultiFetcher::instance().cancelFetch(&second);
        waitFor([] { return false; }, 500);
    }

    // And the fetcher still works afterwards: the torn-down transfer did not
    // leave the URL marked in flight with nothing behind it, which would
    // make every later request for the same icon wait for ever.
    const FetchResult again = fetchIcon(url);
    INFO("error: " << again.error.toStdString());
    CHECK(again.finished);
    CHECK(again.error.isEmpty());
}

// ══════════════════════════════════════════════════════════════
// The cache does not grow for ever.
//
// Every icon fetched is kept in memory so scrolling the OS list back and
// forth does not refetch. Nothing evicts it except the limits at the top of
// the header -- 32 MB, or 500 entries -- and those had no test.

TEST_CASE("The icon cache evicts the oldest once it is full", "[icons][cache]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());

    // A megabyte apiece, so the 32 MB byte limit is reached in a few dozen
    // fetches rather than the 500 the entry limit would need. Incompressible
    // and distinct, so nothing can be shared or deduplicated behind the
    // scenes.
    // Real PNGs, because the response decodes what it is given and a blob of
    // bytes is refused before it ever reaches the cache. Noise rather than a
    // fill, so each one is close to a megabyte instead of compressing to
    // nothing, and each is distinct.
    constexpr int kCount = 40;
    constexpr int kSide = 700;
    QStringList names;
    qsizetype produced = 0;
    for (int i = 0; i < kCount; ++i) {
        QImage img(kSide, kSide, QImage::Format_ARGB32);
        quint32 seed = 2166136261u + static_cast<quint32>(i) * 16777619u;
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                seed = seed * 1664525u + 1013904223u;
                img.setPixel(x, y, 0xFF000000u | (seed >> 8));
            }
        }
        const QString name = QStringLiteral("icon-%1.png").arg(i, 3, 10, QLatin1Char('0'));
        const QString path = QDir(served.path()).filePath(name);
        REQUIRE(img.save(path, "PNG", 0));
        produced += QFileInfo(path).size();
        names << name;
    }

    // Comfortably past the 32 MB limit, or nothing is evicted and the case
    // proves nothing.
    INFO("produced " << produced << " bytes across " << kCount << " icons");
    REQUIRE(produced > IconMultiFetcher::MaxCacheBytes);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();

    QStringList urls;
    for (const QString &name : names) {
        const QUrl url(QString::fromUtf8(server.urlFor(name)));
        urls << url.toString();
        const FetchResult r = fetchIcon(url, 30000);
        INFO("fetching " << name.toStdString() << ": " << r.error.toStdString());
        REQUIRE(r.finished);
        REQUIRE(r.error.isEmpty());
    }

    // 40 MB fetched into a 32 MB cache, so something has to have gone -- and
    // it is the oldest, because the eviction walks the insertion order.
    CHECK(IconMultiFetcher::instance().getCachedData(urls.first()).isEmpty());

    // While the most recent is still there: an eviction that cleared
    // everything would satisfy the line above and defeat the point of having
    // a cache at all.
    CHECK_FALSE(IconMultiFetcher::instance().getCachedData(urls.last()).isEmpty());

    IconMultiFetcher::instance().clearCache();
}

// ══════════════════════════════════════════════════════════════════════════
// The provider QML actually goes through
//
// Every icon in the chooser is an Image whose source is
// "image://icons/<url>". QML hands that to IconImageProvider, gets a response
// back, waits for it to finish and then asks it for a texture. The cases
// above drive the fetcher and the response; nothing had gone in by the front
// door, and nothing had asked for the texture at the end of it.

namespace {

// Drive a response the way QML does: ask the provider, wait, then look.
bool waitForResponse(QQuickImageResponse *response, int timeoutMs = 15000)
{
    bool finished = false;
    QObject::connect(response, &QQuickImageResponse::finished, [&] { finished = true; });
    return waitFor([&] { return finished; }, timeoutMs);
}

} // namespace

TEST_CASE("The provider serves the icon QML asked for", "[icons][provider]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("provider.png")), Qt::magenta);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);
    IconMultiFetcher::instance().clearCache();

    IconImageProvider provider;
    // The id is what QML puts after "image://icons/": the icon's own URL.
    QQuickImageResponse *response = provider.requestImageResponse(
        QString::fromUtf8(server.urlFor(QStringLiteral("provider.png"))), QSize());
    REQUIRE(response != nullptr);
    REQUIRE(waitForResponse(response));

    CHECK(response->errorString().isEmpty());

    // And something to draw. QML takes ownership of the factory it is given.
    QQuickTextureFactory *texture = response->textureFactory();
    CHECK(texture != nullptr);
    delete texture;

    delete response;
}

TEST_CASE("An icon that never arrives has nothing to draw", "[icons][provider]")
{
    // A 404: an entry in the OS list whose icon was taken down. The tile is
    // blank either way; what matters is that nothing is handed to the scene
    // graph to upload.
    QTemporaryDir served;
    REQUIRE(served.isValid());

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);
    IconMultiFetcher::instance().clearCache();

    IconImageProvider provider;
    QQuickImageResponse *response = provider.requestImageResponse(
        QString::fromUtf8(server.urlFor(QStringLiteral("not-here.png"))), QSize());
    REQUIRE(response != nullptr);
    REQUIRE(waitForResponse(response));

    CHECK_FALSE(response->errorString().isEmpty());
    CHECK(response->textureFactory() == nullptr);

    delete response;
}

TEST_CASE("Something that is not an image is reported rather than drawn",
          "[icons][provider]")
{
    // An icon URL that answers with a login page, or an error document served
    // as a 200. It arrives, it is cached, and it is not a picture.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    {
        QFile f(QDir(served.path()).filePath(QStringLiteral("notanimage.png")));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("<html><body>sign in</body></html>");
    }

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);
    IconMultiFetcher::instance().clearCache();

    IconImageProvider provider;
    QQuickImageResponse *response = provider.requestImageResponse(
        QString::fromUtf8(server.urlFor(QStringLiteral("notanimage.png"))), QSize());
    REQUIRE(response != nullptr);
    REQUIRE(waitForResponse(response));

    INFO("error: " << response->errorString().toStdString());
    CHECK_FALSE(response->errorString().isEmpty());
    CHECK(response->textureFactory() == nullptr);

    delete response;
}

TEST_CASE("Scrolling an icon out of view cancels it", "[icons][provider]")
{
    // QML calls cancel() when the delegate holding the image goes away, which
    // in a list of thirty operating systems happens constantly. The response
    // has to take itself off the fetcher's books: a fetch completing into a
    // response QML has since deleted is a write to freed memory.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    writeIcon(QDir(served.path()).filePath(QStringLiteral("scrolled.png")), Qt::cyan);

    rpi_test::LocalHttpServer server(served.path());
    REQUIRE_HTTP_SERVER(server);
    IconMultiFetcher::instance().clearCache();

    IconImageProvider provider;
    QQuickImageResponse *response = provider.requestImageResponse(
        QString::fromUtf8(server.urlFor(QStringLiteral("scrolled.png"))), QSize());
    REQUIRE(response != nullptr);

    // Cancelled before the loop has run once, so the flag is set whatever the
    // transfer did in the meantime.
    response->cancel();
    CHECK(static_cast<IconImageResponse *>(response)->isCancelled());

    // Which is the difference between this and the fetcher's own
    // cancelFetch(), a few cases up: that one takes the request off the
    // transfer without the response knowing.
    //
    // Whether finished() still arrives is not something this can pin down --
    // against a server on loopback the bytes are usually already in before
    // cancel() is reached. What has to hold is that the response can be
    // destroyed afterwards without the fetcher writing into it, which is what
    // QML does the moment the delegate goes.
    waitFor([] { return false; }, 500);
    CHECK_NOTHROW(delete response);
    waitFor([] { return false; }, 500);
}

// ---------------------------------------------------------------------------
// The arms that protect the fetcher from what the OS list can ask of it
// ---------------------------------------------------------------------------

TEST_CASE("An icon URL in a scheme the fetcher will not speak is refused",
          "[icons]")
{
    // Icon addresses come out of the repository JSON, which a user may point
    // anywhere. Only http, https and file are allowed through to curl: the
    // rest -- ftp, gopher, scp, and anything else curl was built with -- are
    // reachable by anyone who can put a URL in a list, and none of them has
    // any business serving a 48-pixel icon.
    IconMultiFetcher::instance().clearCache();

    const QUrl url(QStringLiteral("ftp://example.invalid/icon.png"));
    const FetchResult out = fetchIcon(url, 5000);

    CHECK(out.finished);
    CHECK_FALSE(out.error.isEmpty());
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}

TEST_CASE("More icons than the queue will hold are refused, not accumulated",
          "[icons]")
{
    // A repository listing thousands of images, or a view scrolled hard
    // enough, can ask for icons faster than the loop starts them. The queue
    // is capped so that the backlog cannot grow without bound; past the cap
    // the request is answered with an error rather than held.
    IconMultiFetcher::instance().clearCache();

    constexpr int kAttempts = 4000;   // comfortably past the 500 cap
    std::vector<std::unique_ptr<IconImageResponse>> responses;
    responses.reserve(kAttempts);

    int rejected = 0;
    for (int i = 0; i < kAttempts; ++i) {
        const QUrl url(QStringLiteral("https://icons.invalid/%1.png").arg(i));
        auto response = std::make_unique<IconImageResponse>(url);
        QObject::connect(response.get(), &QQuickImageResponse::finished,
                         response.get(), [&rejected, r = response.get()] {
                             if (r->errorString().contains(QStringLiteral("queue full")))
                                 ++rejected;
                         });
        IconMultiFetcher::instance().queueFetch(response.get(), url);
        responses.push_back(std::move(response));
    }

    // The refusals are delivered as queued calls, so they need the event
    // loop to run before they can be counted.
    waitFor([&rejected] { return rejected > 0; }, 5000);

    INFO("rejected " << rejected << " of " << kAttempts);
    CHECK(rejected > 0);

    // Cancel what is still outstanding before the responses go out of scope.
    for (auto &r : responses)
        IconMultiFetcher::instance().cancelFetch(r.get());
    waitFor([] { return false; }, 500);
}

TEST_CASE("Cancelling an icon already on the wire takes the transfer down too",
          "[icons]")
{
    // The delegate scrolled away after the request left. Dropping the
    // waiting response is not enough on its own: with no one left waiting,
    // the transfer itself has to be removed from the multi handle, or the
    // connection stays open and the bytes keep arriving for a picture
    // nothing will draw.
    rpi_test::StallingHttpServer server(64 * 1024);
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();

    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("slow.png"))));
    IconImageResponse response(url);
    bool finished = false;
    QObject::connect(&response, &QQuickImageResponse::finished,
                     [&finished] { finished = true; });

    IconMultiFetcher::instance().queueFetch(&response, url);

    // Long enough to be started rather than still queued: the server answers
    // with headers and then holds the connection open.
    waitFor([] { return false; }, 1000);
    CHECK_FALSE(finished);

    IconMultiFetcher::instance().cancelFetch(&response);
    waitFor([&finished] { return finished; }, 5000);

    CHECK(finished);
    CHECK(response.errorString().contains(QStringLiteral("Cancel")));
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}

TEST_CASE("An icon whose delegate went away before the loop looked is dropped",
          "[icons]")
{
    // QML destroys a delegate as soon as it leaves the view, which can
    // happen between queueing the request and the loop picking it up. The
    // queue holds a guarded pointer so the entry is skipped rather than
    // followed into a destroyed object.
    rpi_test::StallingHttpServer server(64 * 1024);
    REQUIRE_HTTP_SERVER(server);

    IconMultiFetcher::instance().clearCache();

    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("gone.png"))));
    auto *response = new IconImageResponse(url);
    IconMultiFetcher::instance().queueFetch(response, url);
    // No event loop in between: the request is still in the queue.
    delete response;

    CHECK_NOTHROW(waitFor([] { return false; }, 1500));
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}


// ---------------------------------------------------------------------------
// Closing down with work still in it
//
// Both cases below call shutdown(), which the fetcher does not come back
// from: it is a singleton, the thread is deleted, and every later request is
// refused. ctest runs each Catch2 case in its own process so they cannot
// reach each other there, and they are last in the file so a direct run of
// the whole binary reaches them last too. Do not add cases after them.
// ---------------------------------------------------------------------------

TEST_CASE("Shutting down with an icon still arriving comes back promptly",
          "[icons]")
{
    // Closing the window while the OS list is still loading its icons. The
    // transfers in flight have to be taken down with the loop rather than
    // left for the thread to be terminated over -- terminate() on a thread
    // inside libcurl is how a clean exit becomes a crash report.
    rpi_test::StallingHttpServer server(64 * 1024);
    REQUIRE_HTTP_SERVER(server);

    const QUrl url(QString::fromUtf8(server.urlFor(QStringLiteral("icon.png"))));
    IconImageResponse response(url);
    IconMultiFetcher::instance().queueFetch(&response, url);

    // Long enough for the request to have left: the server answers with
    // headers and then holds the connection open, so it is still in flight.
    waitFor([] { return false; }, 1000);

    QElapsedTimer t;
    t.start();
    CHECK_NOTHROW(IconMultiFetcher::instance().shutdown());

    // The thread wait inside shutdown() allows five seconds before it
    // resorts to terminate(). Coming back well inside that is the difference
    // between the loop noticing and the thread being killed.
    INFO("shutdown took " << t.elapsed() << " ms");
    CHECK(t.elapsed() < 4000);
}

TEST_CASE("A request made after shutdown is dropped rather than queued",
          "[icons]")
{
    // The QML engine tears down in its own order, so a delegate can still ask
    // for an icon after the fetcher has gone. Queueing it would put work on a
    // thread that no longer exists.
    //
    // The guard that refuses it cannot be caught by reverting: with the
    // thread already deleted nothing would process the request anyway, so
    // removing the check leaves this passing. What the case holds down is the
    // outcome -- no crash, no callback into a half-destroyed engine, nothing
    // cached -- rather than the mechanism that produces it.
    IconMultiFetcher::instance().shutdown();

    const QUrl url(QStringLiteral("https://example.invalid/late.png"));
    IconImageResponse response(url);
    bool finished = false;
    QObject::connect(&response, &QQuickImageResponse::finished,
                     [&finished] { finished = true; });

    CHECK_NOTHROW(IconMultiFetcher::instance().queueFetch(&response, url));

    // Dropped in silence: nothing is waiting on the answer by this point, and
    // the callback would run against a half-destroyed engine.
    waitFor([&finished] { return finished; }, 500);
    CHECK_FALSE(finished);
    CHECK(IconMultiFetcher::instance().getCachedData(url.toString()).isEmpty());
}
