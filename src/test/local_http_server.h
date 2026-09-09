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
#include <QUrl>

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
            "import http.server, socketserver, sys, threading, urllib.parse\n"
            "class H(http.server.SimpleHTTPRequestHandler):\n"
            "    def log_message(self, *a): pass\n"
            // /redirect?to=<url> answers 302 to whatever is asked for,
            // including schemes this server does not speak. Following a
            // redirect is otherwise untestable, and where it is allowed to
            // lead is a property worth holding down.
            "    def do_GET(self):\n"
            "        p = urllib.parse.urlparse(self.path)\n"
            "        if p.path == '/redirect':\n"
            "            q = urllib.parse.parse_qs(p.query)\n"
            "            self.send_response(302)\n"
            "            self.send_header('Location', q.get('to', [''])[0])\n"
            "            self.send_header('Content-Length', '0')\n"
            "            self.end_headers()\n"
            "            return\n"
            "        super().do_GET()\n"
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

    // A URL on this server that answers 302 pointing at `target`.
    QByteArray redirectTo(const QByteArray &target) const
    {
        return QByteArray("http://127.0.0.1:") + QByteArray::number(_port)
               + "/redirect?to=" + QUrl::toPercentEncoding(QString::fromUtf8(target));
    }

private:
    QProcess _process;
    int _port = 0;
};

// A server that drops the first transfer part-way and honours Range on the
// retry, for the resume path.
//
// DownloadThread reconnects when curl reports a partial transfer, sets
// CURLOPT_RESUME_FROM_LARGE to how far it had got, and adds that offset to
// everything the progress callback reports afterwards. Get any of that wrong
// and a flaky connection either restarts the download from nothing or, worse,
// writes the resumed bytes at the wrong offset -- so the card ends up with a
// hole in it and the write still reports success.
//
// Python's SimpleHTTPRequestHandler ignores Range entirely, so this is a
// handler of its own: it answers the first GET with a full Content-Length and
// then sends fewer bytes than it promised before closing, which is what curl
// reports as CURLE_PARTIAL_FILE, and answers every request after that
// properly, honouring Range with a 206.
class ResumableHttpServer
{
public:
    // dropAfterBytes: how much of each dropped response's body is sent before
    // hanging up. dropCount: how many responses to drop before answering in
    // full (default one).
    ResumableHttpServer(const QString &directory, int dropAfterBytes,
                        int dropCount = 1)
    {
        static const char *kScript =
            "import http.server, socketserver, sys, os\n"
            "root, drop, drops = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])\n"
            "state = {'dropped': 0}\n"
            "class H(http.server.BaseHTTPRequestHandler):\n"
            "    protocol_version = 'HTTP/1.1'\n"
            "    def log_message(self, *a): pass\n"
            "    def do_GET(self):\n"
            "        full = os.path.join(root, self.path.lstrip('/'))\n"
            "        if not os.path.isfile(full):\n"
            "            self.send_response(404); self.send_header('Content-Length','0')\n"
            "            self.end_headers(); return\n"
            "        size = os.path.getsize(full)\n"
            "        start = 0\n"
            "        rng = self.headers.get('Range')\n"
            "        if rng and rng.startswith('bytes='):\n"
            "            start = int(rng.split('=')[1].split('-')[0] or 0)\n"
            "        with open(full, 'rb') as f:\n"
            "            f.seek(start); body = f.read()\n"
            "        if start:\n"
            "            self.send_response(206)\n"
            "            self.send_header('Content-Range',\n"
            "                             'bytes %d-%d/%d' % (start, size - 1, size))\n"
            "        else:\n"
            "            self.send_response(200)\n"
            "        self.send_header('Accept-Ranges', 'bytes')\n"
            "        self.send_header('Content-Length', str(len(body)))\n"
            "        self.end_headers()\n"
            "        if state['dropped'] < drops:\n"
            "            state['dropped'] += 1\n"
            "            self.wfile.write(body[:drop])\n"
            "            self.wfile.flush()\n"
            "            self.close_connection = True\n"
            "            try: self.connection.close()\n"
            "            except Exception: pass\n"
            "            return\n"
            "        self.wfile.write(body)\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), H)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        _process.start(QStringLiteral("/usr/bin/python3"),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript), directory,
                        QString::number(dropAfterBytes),
                        QString::number(dropCount)});
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~ResumableHttpServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    ResumableHttpServer(const ResumableHttpServer &) = delete;
    ResumableHttpServer &operator=(const ResumableHttpServer &) = delete;

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

// A server that answers with headers and then nothing at all.
//
// It promises a Content-Length it never delivers a byte of, so the transfer
// sits open with no data moving. That is the only way to reach the clean
// cancellation path: with data arriving, cancelling stops the write callback
// first and curl reports a write error instead. Here the write callback is
// never called, and the progress callback -- which curl keeps calling while
// the connection is idle -- is the one that has to notice.
//
// The stall is what a user hits when a mirror accepts the connection and then
// goes quiet. libcurl gives up on that after a minute; the question this
// fixture asks is what happens when the user does not wait.
class StallingHttpServer
{
public:
    explicit StallingHttpServer(qint64 announcedBytes)
    {
        static const char *kScript =
            "import http.server, socketserver, sys, time\n"
            "size = int(sys.argv[1])\n"
            "class H(http.server.BaseHTTPRequestHandler):\n"
            "    protocol_version = 'HTTP/1.1'\n"
            "    def log_message(self, *a): pass\n"
            "    def do_GET(self):\n"
            "        self.send_response(200)\n"
            "        self.send_header('Content-Length', str(size))\n"
            "        self.end_headers()\n"
            "        self.wfile.flush()\n"
            "        time.sleep(600)\n"
            "socketserver.ThreadingTCPServer.allow_reuse_address = True\n"
            "socketserver.ThreadingTCPServer.daemon_threads = True\n"
            "s = socketserver.ThreadingTCPServer(('127.0.0.1', 0), H)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";

        _process.start(QStringLiteral("/usr/bin/python3"),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript),
                        QString::number(announcedBytes)});
        if (!_process.waitForStarted(10000))
            return;
        if (_process.waitForReadyRead(10000))
            _port = _process.readLine().trimmed().toInt();
    }

    ~StallingHttpServer()
    {
        _process.kill();
        _process.waitForFinished(5000);
    }

    StallingHttpServer(const StallingHttpServer &) = delete;
    StallingHttpServer &operator=(const StallingHttpServer &) = delete;

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
