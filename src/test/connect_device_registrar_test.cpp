// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// ConnectDeviceRegistrar enrols a board into Raspberry Pi Connect: it reads
// the device's public key over fastboot, has the device sign a challenge,
// POSTs the result to the management API and records the identity it gets
// back. It also mints organisation auth keys for targets that are not
// fastboot devices.
//
// It looked like it needed hardware and the live API. It needs neither: the
// device side is an IUsbTransport, for which the tree already ships
// mock_usb_transport.h with fault injection built in, and the API side takes
// a baseUrl, which can point at a throwaway server on 127.0.0.1.
//
// Nothing here contacts Raspberry Pi Connect, and no real credential is used.
// The failure cases matter most: this handles an API key, and reporting
// success on a registration that did not happen leaves a fleet device the
// operator believes is enrolled.

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "connect_device_registrar.h"
#include "fastboot/fastboot_protocol.h"
#include "rpiboot/test/mock_usb_transport.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>

using rpiboot::testing::MockUsbTransport;

namespace {

bool havePython() { return QFileInfo::exists(QStringLiteral("/usr/bin/python3")); }

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-cdr-%1")
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

// A localhost stand-in for the Connect management API.
//
// It answers every POST with a canned status and body, so a case can choose
// what the server does without needing the real service.
class FakeApiServer
{
public:
    FakeApiServer(int status, const QByteArray &body)
    {
        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "status = int(sys.argv[1])\n"
            "body = sys.argv[2].encode()\n"
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

        _process.start(QStringLiteral("/usr/bin/python3"),
                       {QStringLiteral("-c"), QString::fromUtf8(kScript),
                        QString::number(status), QString::fromUtf8(body)});
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

// Build a mock device that answers the two fastboot commands the flow issues:
// getvar:public-key and oem fwcrypto sign-hash.
std::vector<uint8_t> okResponse(const std::string &payload)
{
    const std::string s = "OKAY" + payload;
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::vector<uint8_t> failResponse(const std::string &reason)
{
    const std::string s = "FAIL" + reason;
    return std::vector<uint8_t>(s.begin(), s.end());
}

// A real PEM public key. The registrar parses one out of the device's
// getvar:public-key answer -- either from the INFO lines or, for firmware
// that fits it, from the OKAY message itself -- so an arbitrary blob is
// rejected and the flow stops before it ever reaches the API.
const char *kDevicePublicKeyPem =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAy8Dbv8prpJ/0kKhlGeJY\n"
    "ozo2t60EG8L0561g13R29LvMR5hyvGZlGJpmn65+A4xHXInJYiPuKzrKUnApeLZ+\n"
    "vw1HocOAZtWK0z3r26uA8kQYOKX9Qt/DbCdvsF9wF8gRK0ptx9M6R13NvBxvVQAp\n"
    "fc9jB9nTzphOgM4JiEYvlV8FLhg9yZovMYd6Wwf3aoXK891VQxTr/kQYoq1Yp+68\n"
    "i6T4nNq7NWC+UNVjQHxNQMQMzU6lWCX8zyg3yH88OAQkUXIXKfQ+NkvYQ1cxaMoV\n"
    "PpY72+eVthKzpMeyHkBn7ciumk5qgLTEJAfWZpe4f4eFZj/Rc8Y8Jj2IS5kVPjUy\n"
    "wQIDAQAB\n"
    "-----END PUBLIC KEY-----\n";

void queueHappyDevice(MockUsbTransport &mock)
{
    // getvar:public-key answers with the PEM in the OKAY message, then the
    // device returns its signature. The signature is looked up by the
    // "connect-signature:" prefix -- in the INFO lines, or in the terminal
    // OKAY message as here -- so a bare blob is not recognised and the flow
    // stops before it reaches the API.
    mock.queueBulkReadResponse(okResponse(kDevicePublicKeyPem));
    mock.queueBulkReadResponse(
        okResponse("connect-signature:" + std::string(512, 'a')));
}

} // namespace

#define REQUIRE_SERVER(server)                                                                     \
    if (!havePython())                                                                             \
        SKIP("python3 is not installed, so no local API server can be started");                   \
    if (!(server).isRunning())                                                                     \
    SKIP("the local API server did not start")

// ---------------------------------------------------------------------------
// Enablement
// ---------------------------------------------------------------------------

TEST_CASE("Registrar is disabled without an API key", "[connect]")
{
    ConnectDeviceRegistrar registrar(QString(), QStringLiteral("test"));

    // Callers check this before doing anything. Reporting enabled with no
    // credential would have every registration fail at the API instead of
    // being skipped cleanly.
    CHECK_FALSE(registrar.isEnabled());
}

TEST_CASE("Registrar is enabled with an API key", "[connect]")
{
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("test"));

    CHECK(registrar.isEnabled());
}

// ---------------------------------------------------------------------------
// Auth keys
// ---------------------------------------------------------------------------

TEST_CASE("Registrar mints an auth key", "[connect]")
{
    // Both creation endpoints answer 201 Created, not 200: these mint a
    // resource rather than fetch one, and the client checks for exactly 201.
    FakeApiServer server(
        201, R"({"id":"11111111-2222-3333-4444-555555555555","secret":"rpoak_abc123"})");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.requestAuthKey(QStringLiteral("a test device"), 7);

    INFO("error: " << result.errorMessage.toStdString());
    CHECK(result.ok);
    CHECK(result.id == QStringLiteral("11111111-2222-3333-4444-555555555555"));
    CHECK(result.secret == QStringLiteral("rpoak_abc123"));
}

TEST_CASE("Registrar mints an auth key with the server default TTL", "[connect]")
{
    FakeApiServer server(201, R"({"id":"abc","secret":"rpoak_x"})");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    // ttlDays <= 0 omits the field so the server chooses; a client that sent
    // 0 instead would mint a key that expires immediately.
    const auto result = registrar.requestAuthKey(QStringLiteral("no ttl"), 0);

    CHECK(result.ok);
}

TEST_CASE("Registrar reports an API error when minting a key", "[connect]")
{
    FakeApiServer server(403, R"({"error":"forbidden"})");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.requestAuthKey(QStringLiteral("rejected"), 1);

    // A rejected key must not come back as a usable one.
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.errorMessage.isEmpty());
    CHECK(result.secret.isEmpty());
}

TEST_CASE("Registrar reports a malformed API response", "[connect]")
{
    FakeApiServer server(201, "this is not json");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.requestAuthKey(QStringLiteral("garbage"), 1);

    // A 200 with a body that cannot be parsed is still a failure; treating it
    // as success would write an empty secret into the image.
    CHECK_FALSE(result.ok);
    CHECK(result.secret.isEmpty());
}

TEST_CASE("Registrar reports a response missing the secret", "[connect]")
{
    FakeApiServer server(201, R"({"id":"abc"})");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.requestAuthKey(QStringLiteral("no secret"), 1);

    // Well-formed JSON without the field that matters.
    CHECK_FALSE(result.ok);
    CHECK(result.secret.isEmpty());
}

TEST_CASE("Registrar rejects a 200 where creation was expected", "[connect]")
{
    FakeApiServer server(200, R"({"id":"abc","secret":"rpoak_x"})");
    REQUIRE_SERVER(server);

    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.requestAuthKey(QStringLiteral("wrong status"), 1);

    // 201 Created is required. A 200 with a plausible body most likely means
    // something other than the Connect API answered -- a captive portal or a
    // proxy -- and accepting it would write that response's contents into the
    // image as a credential.
    CHECK_FALSE(result.ok);
    CHECK(result.secret.isEmpty());
}

TEST_CASE("Registrar reports an unreachable API", "[connect]")
{
    // Nothing listens on port 1.
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"),
                                     QStringLiteral("http://127.0.0.1:1"));

    const auto result = registrar.requestAuthKey(QStringLiteral("offline"), 1);

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.errorMessage.isEmpty());
}

// ---------------------------------------------------------------------------
// Device registration
// ---------------------------------------------------------------------------

TEST_CASE("Registrar registers a device", "[connect]")
{
    FakeApiServer server(201, R"({"id":"device-identity-0001"})");
    REQUIRE_SERVER(server);

    MockUsbTransport mock;
    mock.setOpen(true);
    queueHappyDevice(mock);

    fastboot::FastbootProtocol fb;
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.registerDevice(fb, mock, QStringLiteral("Raspberry Pi 5"),
                                                 QStringLiteral("10000000abcdef01"));

    INFO("error: " << result.errorMessage.toStdString());
    CHECK(result.ok);
    CHECK(result.deviceId == QStringLiteral("device-identity-0001"));

    // The key must have been read from the device rather than invented: two
    // commands go out, getvar:public-key and the sign request.
    CHECK(mock.capturedBulkWrites().size() >= 2);
}

TEST_CASE("Registrar reports a device that will not answer", "[connect]")
{
    FakeApiServer server(201, R"({"id":"unused"})");
    REQUIRE_SERVER(server);

    MockUsbTransport mock;
    mock.setOpen(true);
    // The device is there but every bulk write fails: a cable or a board on
    // its way out.
    mock.failNextBulkWrites(10);

    fastboot::FastbootProtocol fb;
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.registerDevice(fb, mock, QStringLiteral("Raspberry Pi 5"),
                                                 QStringLiteral("10000000abcdef01"));

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.errorMessage.isEmpty());
    CHECK(result.deviceId.isEmpty());
}

TEST_CASE("Registrar reports a device that refuses the key request", "[connect]")
{
    FakeApiServer server(201, R"({"id":"unused"})");
    REQUIRE_SERVER(server);

    MockUsbTransport mock;
    mock.setOpen(true);
    // A board without secure-boot firmware answers FAIL rather than a key.
    mock.queueBulkReadResponse(failResponse("unknown variable"));

    fastboot::FastbootProtocol fb;
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.registerDevice(fb, mock, QStringLiteral("Raspberry Pi 4"),
                                                 QStringLiteral("deadbeef"));

    CHECK_FALSE(result.ok);
    CHECK(result.deviceId.isEmpty());
}

TEST_CASE("Registrar reports an API rejection during registration", "[connect]")
{
    FakeApiServer server(401, R"({"error":"unauthorised"})");
    REQUIRE_SERVER(server);

    MockUsbTransport mock;
    mock.setOpen(true);
    queueHappyDevice(mock);

    fastboot::FastbootProtocol fb;
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_wrong_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.registerDevice(fb, mock, QStringLiteral("Raspberry Pi 5"),
                                                 QStringLiteral("10000000abcdef01"));

    // A device that was read but not accepted must not be reported enrolled.
    CHECK_FALSE(result.ok);
    CHECK(result.deviceId.isEmpty());
}

TEST_CASE("Registrar reports a closed transport", "[connect]")
{
    FakeApiServer server(201, R"({"id":"unused"})");
    REQUIRE_SERVER(server);

    MockUsbTransport mock;
    // The device went away between enumeration and registration.
    mock.setOpen(false);

    fastboot::FastbootProtocol fb;
    ConnectDeviceRegistrar registrar(QStringLiteral("rpck_not_a_real_key"),
                                     QStringLiteral("imager"), server.baseUrl());

    const auto result = registrar.registerDevice(fb, mock, QStringLiteral("Raspberry Pi 5"),
                                                 QStringLiteral("serial"));

    CHECK_FALSE(result.ok);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
