// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// CurlFetcher is how the imager retrieves the OS list and its sublists. If it
// reports success on a 404 the user gets an empty or bogus list; if it never
// reports at all the UI waits forever. It had no tests.
//
// Everything here runs against a throwaway HTTP server on 127.0.0.1 bound to
// port 0, so the cases exercise real requests and real status handling
// without touching the network or depending on a fixed port.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "curlfetcher.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-fetch-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    QString filePath(const QString &name) const { return QDir(_path).filePath(name); }
    QString path() const { return _path; }

private:
    QString _path;
};

bool havePython() { return QFileInfo::exists(QStringLiteral("/usr/bin/python3")); }

// A localhost HTTP server serving one directory, torn down with the object.
class LocalHttpServer
{
public:
    explicit LocalHttpServer(const QString &directory)
    {
        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "class H(http.server.SimpleHTTPRequestHandler):\n"
            "    def log_message(self, *a): pass\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "h = lambda *a, **k: H(*a, directory=sys.argv[1], **k)\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), h)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        _process.start(QStringLiteral("/usr/bin/python3"),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript), directory});
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~LocalHttpServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    LocalHttpServer(const LocalHttpServer &) = delete;
    LocalHttpServer &operator=(const LocalHttpServer &) = delete;

    bool isRunning() const { return _port > 0; }

    QUrl urlFor(const QString &name) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(_port).arg(name));
    }

private:
    QProcess _process;
    int _port = 0;
};

// A server whose every response is a redirect to a fixed location. Used to
// check what the fetcher will and will not follow.
class RedirectingHttpServer
{
public:
    explicit RedirectingHttpServer(const QString &target)
    {
        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "target = sys.argv[1]\n"
            "class H(http.server.BaseHTTPRequestHandler):\n"
            "    def log_message(self, *a): pass\n"
            "    def do_GET(self):\n"
            "        self.send_response(302)\n"
            "        self.send_header('Location', target)\n"
            "        self.end_headers()\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), H)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        _process.start(QStringLiteral("/usr/bin/python3"),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript), target});
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~RedirectingHttpServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    RedirectingHttpServer(const RedirectingHttpServer &) = delete;
    RedirectingHttpServer &operator=(const RedirectingHttpServer &) = delete;

    bool isRunning() const { return _port > 0; }
    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/os_list.json").arg(_port));
    }

private:
    QProcess _process;
    int _port = 0;
};

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const bool ok = f.write(contents) == contents.size();
    f.close();
    return ok;
}

// What a fetch ended up doing. Exactly one of the two must happen.
struct FetchResult {
    bool finished = false;
    bool failed = false;
    QByteArray data;
    QUrl effectiveUrl;
    QString errorMessage;
    bool sawStats = false;
};

FetchResult fetchAndWait(CurlFetcher &fetcher, const QUrl &url, int timeoutMs = 30000)
{
    FetchResult result;
    QEventLoop loop;

    QObject::connect(&fetcher, &CurlFetcher::finished, &loop,
                     [&](const QByteArray &data, const QUrl &, const QUrl &effective) {
                         result.finished = true;
                         result.data = data;
                         result.effectiveUrl = effective;
                         loop.quit();
                     });
    QObject::connect(&fetcher, &CurlFetcher::error, &loop,
                     [&](const QString &message, const QUrl &) {
                         result.failed = true;
                         result.errorMessage = message;
                         loop.quit();
                     });
    QObject::connect(&fetcher, &CurlFetcher::connectionStats, &loop,
                     [&](const QString &, const QUrl &) { result.sawStats = true; });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);

    fetcher.fetch(url);
    loop.exec();
    return result;
}

} // namespace

#define REQUIRE_SERVER(server)                                                                     \
    if (!havePython())                                                                             \
        SKIP("python3 is not installed, so no local HTTP server can be started");                  \
    if (!(server).isRunning())                                                                     \
    SKIP("the local HTTP server did not start")

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("CurlFetcher starts out uncancelled", "[curl-fetcher]")
{
    CurlFetcher fetcher;

    CHECK_FALSE(fetcher.isCancelled());
    CHECK(fetcher.url().isEmpty());
}

TEST_CASE("CurlFetcher can be cancelled before fetching", "[curl-fetcher]")
{
    CurlFetcher fetcher;

    // Cancelling something that never started must be safe: the UI can
    // navigate away before a fetch has been dispatched.
    CHECK_NOTHROW(fetcher.cancel());
    CHECK(fetcher.isCancelled());
}

// ---------------------------------------------------------------------------
// Successful fetches
// ---------------------------------------------------------------------------

TEST_CASE("CurlFetcher retrieves a document", "[curl-fetcher]")
{
    ScratchDir scratch;
    const QByteArray body = R"({"os_list":[{"name":"Raspberry Pi OS"}]})";
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("os_list.json")), body));

    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.urlFor(QStringLiteral("os_list.json")));

    INFO("error: " << result.errorMessage.toStdString());
    REQUIRE(result.finished);
    CHECK_FALSE(result.failed);
    CHECK(result.data == body);
}

TEST_CASE("CurlFetcher retrieves an empty document", "[curl-fetcher]")
{
    ScratchDir scratch;
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("empty.json")), QByteArray()));

    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.urlFor(QStringLiteral("empty.json")));

    // A zero-length body is a successful fetch of nothing, not a failure --
    // the distinction matters to the caller deciding whether to retry.
    REQUIRE(result.finished);
    CHECK(result.data.isEmpty());
}

TEST_CASE("CurlFetcher retrieves a document larger than one buffer", "[curl-fetcher]")
{
    ScratchDir scratch;
    QByteArray body;
    body.reserve(512 * 1024);
    for (int i = 0; i < 512 * 1024; ++i)
        body.append(static_cast<char>('a' + (i % 26)));
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("big.json")), body));

    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.urlFor(QStringLiteral("big.json")));

    // Many write-callback invocations rather than one, so this covers the
    // accumulation path rather than a single-shot copy.
    REQUIRE(result.finished);
    REQUIRE(result.data.size() == body.size());
    CHECK(result.data == body);
}

TEST_CASE("CurlFetcher reports the effective URL", "[curl-fetcher]")
{
    ScratchDir scratch;
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("doc.json")), "{}"));

    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    const QUrl requested = server.urlFor(QStringLiteral("doc.json"));
    const FetchResult result = fetchAndWait(fetcher, requested);

    REQUIRE(result.finished);
    // With no redirect the effective URL is the requested one; callers use
    // this to resolve relative sublist references.
    CHECK(result.effectiveUrl.toString().contains(QStringLiteral("doc.json")));
}

// ---------------------------------------------------------------------------
// Failures
// ---------------------------------------------------------------------------

TEST_CASE("CurlFetcher reports a 404 as an error", "[curl-fetcher]")
{
    ScratchDir scratch;
    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.urlFor(QStringLiteral("missing.json")));

    // The body of a 404 is an HTML error page. Handing that back as a
    // successful fetch would have the caller try to parse it as the OS list.
    CHECK(result.failed);
    CHECK_FALSE(result.finished);
    CHECK_FALSE(result.errorMessage.isEmpty());
}

TEST_CASE("CurlFetcher reports a refused connection", "[curl-fetcher]")
{
    CurlFetcher fetcher;
    // Nothing listens on port 1, so this is refused rather than timing out.
    const FetchResult result = fetchAndWait(fetcher, QUrl("http://127.0.0.1:1/os_list.json"));

    CHECK(result.failed);
    CHECK_FALSE(result.errorMessage.isEmpty());
}

TEST_CASE("CurlFetcher reports an unusable URL", "[curl-fetcher]")
{
    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, QUrl("not-a-url-at-all"));

    // Must fail rather than hang: the UI has no other way to learn the fetch
    // is never coming.
    CHECK(result.failed);
}

TEST_CASE("CurlFetcher refuses a protocol it does not allow", "[curl-fetcher]")
{
    CurlFetcher fetcher;
    // The OS list is fetched over http/https only. A file:// URL arriving
    // from a manifest must not become a local file read.
    const FetchResult result = fetchAndWait(fetcher, QUrl("ftp://127.0.0.1:1/os_list.json"));

    CHECK(result.failed);
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

TEST_CASE("CurlFetcher marks itself cancelled during a fetch", "[curl-fetcher]")
{
    ScratchDir scratch;
    QByteArray body(4 * 1024 * 1024, 'x');
    REQUIRE(writeFile(scratch.filePath(QStringLiteral("large.json")), body));

    LocalHttpServer server(scratch.path());
    REQUIRE_SERVER(server);

    CurlFetcher fetcher;
    QEventLoop loop;
    bool settled = false;

    QObject::connect(&fetcher, &CurlFetcher::finished, &loop, [&](auto &&...) {
        settled = true;
        loop.quit();
    });
    QObject::connect(&fetcher, &CurlFetcher::error, &loop, [&](auto &&...) {
        settled = true;
        loop.quit();
    });

    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(30000);

    fetcher.fetch(server.urlFor(QStringLiteral("large.json")));
    QTimer::singleShot(1, [&]() { fetcher.cancel(); });
    loop.exec();

    // Whether the transfer beat the cancel is a race and not asserted; what
    // must hold is that the flag is set and the object settled rather than
    // leaving a runnable in flight.
    CHECK(fetcher.isCancelled());
    CHECK(settled);
}

// CurlFetcher delivers its results by signal from a worker, so the cases
// above need a QCoreApplication to dispatch them.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

// ---------------------------------------------------------------------------
// What a redirect is allowed to reach
//
// Restricting the initial URL's scheme is not enough on its own: the manifest
// URL can be a perfectly ordinary https one, and the server on the other end
// decides where the redirect goes. Without a restriction on redirect targets,
// whoever controls that server -- or anyone who can spoof it -- turns a
// document fetch into a read of whatever the imager's own process can reach.
// ---------------------------------------------------------------------------

TEST_CASE("A redirect to a local file is not followed", "[curl-fetcher]")
{
    RedirectingHttpServer server(QStringLiteral("file:///etc/passwd"));
    if (!server.isRunning())
        SKIP("could not start the local redirect server");

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.url());

    CHECK(result.failed);
    // Nothing from the local filesystem came back.
    CHECK_FALSE(result.data.contains("root:"));
}

TEST_CASE("A redirect to a non-web protocol is not followed", "[curl-fetcher]")
{
    RedirectingHttpServer server(QStringLiteral("ftp://127.0.0.1:1/os_list.json"));
    if (!server.isRunning())
        SKIP("could not start the local redirect server");

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.url());
    CHECK(result.failed);
}

TEST_CASE("A redirect loop terminates rather than spinning", "[curl-fetcher]")
{
    // A server that redirects to itself must hit the redirect limit and give
    // up, not keep the download thread going indefinitely.
    RedirectingHttpServer server(QStringLiteral("/os_list.json"));
    if (!server.isRunning())
        SKIP("could not start the local redirect server");

    CurlFetcher fetcher;
    const FetchResult result = fetchAndWait(fetcher, server.url());
    CHECK(result.failed);
}
