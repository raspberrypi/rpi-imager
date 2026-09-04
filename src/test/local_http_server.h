/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * A throwaway HTTP server on loopback, for the tests that need a real
 * connection rather than a file:// URL: status handling, headers, the resume
 * logic and the retry path all sit behind one.
 *
 * It is a few lines of Python bound to port 0, so it never collides with
 * anything else on the machine and never listens beyond loopback.
 */
#ifndef RPI_TEST_LOCAL_HTTP_SERVER_H
#define RPI_TEST_LOCAL_HTTP_SERVER_H

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFileInfo>
#include <QProcess>
#include <QString>

namespace rpi_test {

// A localhost HTTP server serving one directory, torn down with the object.
class LocalHttpServer
{
public:
    explicit LocalHttpServer(const QString &directory)
    {
        // Bind port 0 and print the port it was given, so nothing has to
        // guess a free one.
        static const char *kScript =
            "import http.server, socketserver, sys, threading\n"
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

        // The port arrives on stdout as soon as the socket is bound.
        if (_process.waitForReadyRead(10000)) {
            const QByteArray line = _process.readLine().trimmed();
            _port = line.toInt();
        }
    }

    ~LocalHttpServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    LocalHttpServer(const LocalHttpServer &) = delete;
    LocalHttpServer &operator=(const LocalHttpServer &) = delete;

    bool isRunning() const { return _port > 0; }

    QByteArray urlFor(const QString &name) const
    {
        return QByteArray("http://127.0.0.1:") + QByteArray::number(_port) + "/" +
               name.toUtf8();
    }

private:
    QProcess _process;
    int _port = 0;
};

bool inline havePython() { return QFileInfo::exists(QStringLiteral("/usr/bin/python3")); }

} // namespace rpi_test

#define REQUIRE_HTTP_SERVER(server)                                                                \
    if (!rpi_test::havePython())                                                                             \
        SKIP("python3 is not installed, so no local HTTP server can be started");                  \
    if (!(server).isRunning())                                                                     \
    SKIP("the local HTTP server did not start")

#endif // RPI_TEST_LOCAL_HTTP_SERVER_H
