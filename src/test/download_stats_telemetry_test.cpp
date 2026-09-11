// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// DownloadStatsTelemetry reports which image was written to a Raspberry Pi
// endpoint. Two things about it matter more than the reporting itself: it must
// not send anything when the user has telemetry switched off, and it must
// never be able to fail a write -- it is fire-and-forget, running after the
// card is already done.
//
// Both are testable without the real endpoint. The opt-out is a QSettings
// value, and the POST goes wherever it is pointed, so a throwaway server on
// 127.0.0.1 stands in for the real one. Nothing here contacts Raspberry Pi.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "downloadstatstelemetry.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-tel-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    QString path() const { return _path; }

private:
    QString _path;
};

bool havePython() { return QFileInfo::exists(QStringLiteral("/usr/bin/python3")); }

// A localhost server that accepts POSTs and records how many it received.
class LocalPostServer
{
public:
    // withBody makes the endpoint answer 200 with content rather than 204.
    // The client has nowhere to put a response body and must throw it away;
    // with a 204 the discard callback is never reached at all.
    explicit LocalPostServer(const QString &countFile, bool withBody = false)
    {
        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "count_path = sys.argv[1]\n"
            "class H(http.server.BaseHTTPRequestHandler):\n"
            "    def do_POST(self):\n"
            "        n = int(self.headers.get('content-length', 0))\n"
            "        self.rfile.read(n)\n"
            "        with open(count_path, 'a') as f:\n"
            "            f.write('x')\n"
            "        if len(sys.argv) > 2 and sys.argv[2] == 'body':\n"
            "            payload = b'thank you for the statistics'\n"
            "            self.send_response(200)\n"
            "            self.send_header('content-length', str(len(payload)))\n"
            "            self.end_headers()\n"
            "            self.wfile.write(payload)\n"
            "            return\n"
            "        self.send_response(204)\n"
            "        self.end_headers()\n"
            "    def do_GET(self):\n"
            "        self.send_response(204)\n"
            "        self.end_headers()\n"
            "    def log_message(self, *a): pass\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), H)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        QStringList args{QStringLiteral("-c"), QString::fromUtf8(kScript), countFile};
        if (withBody)
            args << QStringLiteral("body");
        _process.start(QStringLiteral("/usr/bin/python3"), args);
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~LocalPostServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    LocalPostServer(const LocalPostServer &) = delete;
    LocalPostServer &operator=(const LocalPostServer &) = delete;

    bool isRunning() const { return _port > 0; }
    QByteArray url() const
    {
        return QByteArray("http://127.0.0.1:") + QByteArray::number(_port) + "/stats";
    }

private:
    QProcess _process;
    int _port = 0;
};

int postCount(const QString &countFile)
{
    QFile f(countFile);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const int n = static_cast<int>(f.readAll().size());
    f.close();
    return n;
}

void setTelemetryEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("telemetry"), enabled);
    settings.sync();
}

} // namespace

#define REQUIRE_SERVER(server)                                                                     \
    if (!havePython())                                                                             \
        SKIP("python3 is not installed, so no local server can be started");                       \
    if (!(server).isRunning())                                                                     \
    SKIP("the local server did not start")

// ---------------------------------------------------------------------------
// Opting out
// ---------------------------------------------------------------------------

TEST_CASE("Telemetry sends nothing when the user has opted out", "[telemetry]")
{
    ScratchDir scratch;
    const QString countFile = QDir(scratch.path()).filePath(QStringLiteral("count"));

    LocalPostServer server(countFile);
    REQUIRE_SERVER(server);

    setTelemetryEnabled(false);

    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS", false, QStringLiteral("en"), nullptr,
                                     server.url());
    telemetry.start();
    REQUIRE(telemetry.wait(30000));

    // The opt-out is the whole contract. One request here is a privacy bug,
    // not a test failure to be relaxed.
    CHECK(postCount(countFile) == 0);
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

TEST_CASE("Telemetry posts when enabled", "[telemetry]")
{
    ScratchDir scratch;
    const QString countFile = QDir(scratch.path()).filePath(QStringLiteral("count"));

    LocalPostServer server(countFile);
    REQUIRE_SERVER(server);

    setTelemetryEnabled(true);

    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS (64-bit)", false,
                                     QStringLiteral("en_GB"), nullptr, server.url());
    telemetry.start();
    REQUIRE(telemetry.wait(60000));

    // Receipt is asserted now that the endpoint is injectable. It was not
    // before, and the reason given was that arrival "proved unreliable" --
    // it was not unreliable, the POST was going to the production endpoint
    // and the stub was never contacted at all.
    CHECK(postCount(countFile) == 1);

    setTelemetryEnabled(false);
}

TEST_CASE("Telemetry discards a response body rather than choking on it", "[telemetry]")
{
    ScratchDir scratch;
    const QString countFile = QDir(scratch.path()).filePath(QStringLiteral("count"));

    // The real endpoint answers 204, so nothing exercises the discard
    // callback. An endpoint that answers with content is entirely allowed to,
    // and the client has nowhere to put it.
    LocalPostServer server(countFile, /*withBody=*/true);
    REQUIRE_SERVER(server);

    setTelemetryEnabled(true);

    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS (64-bit)", false,
                                     QStringLiteral("en_GB"), nullptr, server.url());
    telemetry.start();
    // Finishing at all is the assertion: a client that mishandled the body
    // would hang on the read or die on the write.
    CHECK(telemetry.wait(60000));

    setTelemetryEnabled(false);
}

TEST_CASE("Telemetry posts for an embedded run", "[telemetry]")
{
    ScratchDir scratch;
    const QString countFile = QDir(scratch.path()).filePath(QStringLiteral("count"));

    LocalPostServer server(countFile);
    REQUIRE_SERVER(server);

    setTelemetryEnabled(true);

    // The embedded flag changes what is reported, so it is a separate path
    // through the payload assembly rather than a variation on the above.
    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS Lite", true, QStringLiteral("de"), nullptr,
                                     server.url());
    telemetry.start();
    REQUIRE(telemetry.wait(60000));

    CHECK(postCount(countFile) == 1);

    setTelemetryEnabled(false);
}

// ---------------------------------------------------------------------------
// Failure must stay harmless
// ---------------------------------------------------------------------------

TEST_CASE("Telemetry survives an endpoint that is not there", "[telemetry]")
{
    setTelemetryEnabled(true);

    // Nothing listens on port 1. This runs after the card is already written,
    // so a failure here must be swallowed -- it must not hang, throw, or
    // otherwise turn a completed write into a reported failure.
    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS", false, QStringLiteral("en"), nullptr,
                                     "http://127.0.0.1:1/stats");
    telemetry.start();
    CHECK(telemetry.wait(120000));

    setTelemetryEnabled(false);
}

TEST_CASE("Telemetry survives a malformed endpoint", "[telemetry]")
{
    setTelemetryEnabled(true);

    DownloadStatsTelemetry telemetry("http://example.invalid/image.img", "raspios",
                                     "Raspberry Pi OS", false, QStringLiteral("en"), nullptr,
                                     "not-a-url");
    telemetry.start();
    CHECK(telemetry.wait(60000));

    setTelemetryEnabled(false);
}

TEST_CASE("Telemetry survives empty metadata", "[telemetry]")
{
    ScratchDir scratch;
    const QString countFile = QDir(scratch.path()).filePath(QStringLiteral("count"));

    LocalPostServer server(countFile);
    REQUIRE_SERVER(server);

    setTelemetryEnabled(true);

    // Every field the caller might not have: an unnamed image from an
    // unnamed category with no language set.
    DownloadStatsTelemetry telemetry(QByteArray(), QByteArray(), QByteArray(), false, QString(),
                                     nullptr, server.url());
    telemetry.start();
    CHECK(telemetry.wait(60000));

    setTelemetryEnabled(false);
}

// The telemetry setting lives in QSettings, so this target scopes it to a
// throwaway file rather than reading or writing the developer's real
// preference -- which would mean a test could switch their telemetry on.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rpi-imager-tests"));
    // Scoped to this process, not just to test mode. catch_discover_tests
    // runs every TEST_CASE as its own process, so `ctest -j4` has several
    // of these alive at once -- and a settings file shared between them is
    // shared mutable state. The secure-boot cases each write a different
    // secureboot_rsa_key, so one process would see another's: the case
    // that expects no key found a valid one, the write it expected to be
    // refused went ahead, and the failure looked like a timing flake.
    QCoreApplication::setApplicationName(
        QStringLiteral("download_stats_telemetry_test-%1").arg(QCoreApplication::applicationPid()));
    QStandardPaths::setTestModeEnabled(true);
    const int rc = Catch::Session().run(argc, argv);

    // One settings file per process would otherwise pile up.
    QFile::remove(QSettings().fileName());
    return rc;
}
