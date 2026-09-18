/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A localhost stand-in for the Raspberry Pi Connect management API.
 *
 * It answers every POST with a canned status and body, so a case can choose
 * what the server does without needing the real service. Shared because the
 * registrar is not the only caller: ImageWriter mints organisation auth keys
 * through it, and what it does with the answer is its own decision.
 */
#ifndef RPI_TEST_FAKE_CONNECT_API_H
#define RPI_TEST_FAKE_CONNECT_API_H

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QProcess>
#include <QString>

#include "platform_tools.h"

namespace rpi_test {

class FakeApiServer
{
public:
    FakeApiServer(int status, const QByteArray &body)
        : FakeApiServer(status, body, 1)
    {
    }

    // The body repeated, for the sizes argv cannot carry.
    //
    // Windows caps the whole command line at 32,767 characters, script and
    // all, so the unit passed here has to stay small however large the body
    // is meant to be -- a 64 KB unit does not start the server at all, and
    // the case that wanted eight megabytes skipped itself on every run.
    FakeApiServer(int status, const QByteArray &body, int repeat)
    {
        // Well under the cap, with the script and the other arguments to fit
        // alongside it. A larger body is expressed as a bigger repeat.
        REQUIRE(body.size() <= 4096);

        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "status = int(sys.argv[1])\n"
            "body = sys.argv[2].encode() * int(sys.argv[3])\n"
            "class H(http.server.BaseHTTPRequestHandler):\n"
            "    def do_POST(self):\n"
            "        n = int(self.headers.get('content-length', 0))\n"
            "        self.rfile.read(n)\n"
            "        self.send_response(status)\n"
            "        self.send_header('content-type', 'application/json')\n"
            "        self.send_header('content-length', str(len(body)))\n"
            "        self.end_headers()\n"
            "        self.wfile.write(body)\n"
            "    def log_message(self, *a): pass\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), H)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        _process.start(rpi_test::pythonPath(),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript),
                        QString::number(status), QString::fromUtf8(body),
                        QString::number(repeat)});
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~FakeApiServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    FakeApiServer(const FakeApiServer &) = delete;
    FakeApiServer &operator=(const FakeApiServer &) = delete;

    bool isRunning() const { return _port > 0; }
    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(_port);
    }

private:
    QProcess _process;
    int _port = 0;
};

}  // namespace rpi_test

#define RPI_REQUIRE_FAKE_API(server)                                                               \
    if (!rpi_test::havePython())                                                                   \
        SKIP("python3 is not installed, so no local API server can be started");                   \
    if (!(server).isRunning())                                                                     \
    SKIP("the local API server did not start")

#endif  // RPI_TEST_FAKE_CONNECT_API_H
