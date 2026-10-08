/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Raspberry Pi Connect device-code sign-in, for embedded Imager.
 *
 * The usual sign-in opens Connect in a browser and comes back through the
 * rpi-imager:// scheme; embedded Imager runs on a Pi with neither. Instead it
 * asks Connect for a code (POST /client/device), shows it as a QR code and a
 * short code for a phone, and polls POST /client/token until someone signs in
 * and approves it there. What comes back is an rpdev_ access token, which is
 * written into the image as rpi-connect's state.json.
 */

#ifndef CONNECT_DEVICE_SIGNIN_H
#define CONNECT_DEVICE_SIGNIN_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace rpi_connect {

struct HttpReply {
    long status = 0;      // -1 when the request never got an answer
    QByteArray body;
    QString transportError;
};

struct DeviceAuthorisation {
    bool ok = false;
    QString deviceCode;
    QString userCode;
    QString verificationUri;   // verification_uri_complete: the QR code's target
    int expiresInSecs = 0;
    int intervalSecs = 0;
    QString error;             // "unauthorized", "network" or "malformed"
};

DeviceAuthorisation parseDeviceAuthorisation(const HttpReply &reply);

enum class TokenOutcome {
    Pending,       // not approved yet: ask again after the interval
    SlowDown,      // asking too often: ask again, less often
    SignedIn,
    Expired,       // the code ran out: start again
    BadCode,       // the server does not know the code: start again
    Unauthorized,  // the client ID was refused
    Failed,        // no answer, or an answer that cannot be read
};

struct TokenReply {
    TokenOutcome outcome = TokenOutcome::Failed;
    QString deviceId;
    QString accessToken;
};

TokenReply classifyTokenReply(const HttpReply &reply);

// The short code as people read it: DECAFBAD as DECA-FBAD.
QString displayUserCode(const QString &userCode);

} // namespace rpi_connect

class ConnectDeviceSignIn : public QObject
{
    Q_OBJECT
public:
    // Posts a form body to a URL and blocks for the answer. Run on a worker
    // thread, so the UI does not stop for it; a test hands in its own.
    using Poster = std::function<rpi_connect::HttpReply(const QString &url, const QByteArray &body)>;

    // An empty baseUrl means RPI_IMAGER_CONNECT_URL, else CONNECT_API_URL.
    explicit ConnectDeviceSignIn(const QString &clientId,
                                 const QString &baseUrl = QString(),
                                 Poster poster = Poster(),
                                 QObject *parent = nullptr);
    ~ConnectDeviceSignIn() override;

    // Ask for a code; codeReady() or failed() follows. Starting again
    // abandons whatever came before.
    void start();
    // Stop polling. Nothing further is emitted for this sign-in.
    void cancel();
    bool isActive() const { return _active; }

    // The server's interval is honoured but never taken below this, so a
    // reply of 0 cannot become a busy loop. A test sets it to 0.
    void setMinimumPollIntervalMs(int ms) { _minPollMs = ms; }

    const QString &baseUrl() const { return _baseUrl; }

signals:
    void codeReady(const QString &userCode, const QString &verificationUri, int expiresInSecs);
    void signedIn(const QString &deviceId, const QString &accessToken);
    // "expired", "badcode", "unauthorized", "network" or "malformed".
    void failed(const QString &reason);

private:
    void post(const QString &path, const QByteArray &body,
              const std::function<void(const rpi_connect::HttpReply &)> &then);
    void pollSoon();
    void poll();
    void finish();

    QString _clientId;
    QString _baseUrl;
    Poster _poster;
    QTimer _pollTimer;
    QElapsedTimer _sinceCode;
    QString _deviceCode;
    int _expiresInSecs = 0;
    int _intervalSecs = 5;
    int _minPollMs = 1000;
    bool _active = false;
    quint64 _generation = 0;   // which start() a reply belongs to
};

#endif // CONNECT_DEVICE_SIGNIN_H
