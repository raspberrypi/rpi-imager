/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "connect_device_signin.h"
#include "config.h"
#include "curlnetworkconfig.h"

#include <QDebug>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QtConcurrent/qtconcurrentrun.h>

#include <curl/curl.h>

#include <algorithm>

namespace rpi_connect {

namespace {

QJsonObject jsonObject(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    return doc.isObject() ? doc.object() : QJsonObject();
}

} // namespace

DeviceAuthorisation parseDeviceAuthorisation(const HttpReply &reply)
{
    DeviceAuthorisation a;
    if (reply.status < 0) {
        a.error = QStringLiteral("network");
        return a;
    }
    if (reply.status == 401) {
        a.error = QStringLiteral("unauthorized");
        return a;
    }
    const QJsonObject o = jsonObject(reply.body);
    a.deviceCode = o.value(QStringLiteral("device_code")).toString();
    a.userCode = o.value(QStringLiteral("user_code")).toString();
    a.verificationUri = o.value(QStringLiteral("verification_uri_complete")).toString();
    a.expiresInSecs = o.value(QStringLiteral("expires_in")).toInt();
    a.intervalSecs = o.value(QStringLiteral("interval")).toInt();
    // The QR code sends a phone to verificationUri, so it has to be a page
    // on the web and nothing else.
    a.ok = reply.status == 200 && !a.deviceCode.isEmpty() && !a.userCode.isEmpty()
        && a.verificationUri.startsWith(QStringLiteral("https://"))
        && a.expiresInSecs > 0 && a.intervalSecs >= 0;
    if (!a.ok)
        a.error = QStringLiteral("malformed");
    return a;
}

TokenReply classifyTokenReply(const HttpReply &reply)
{
    TokenReply t;
    if (reply.status == 401) {
        t.outcome = TokenOutcome::Unauthorized;
        return t;
    }
    const QJsonObject o = jsonObject(reply.body);
    if (reply.status == 200) {
        t.deviceId = o.value(QStringLiteral("device_id")).toString();
        t.accessToken = o.value(QStringLiteral("access_token")).toString();
        if (t.accessToken.startsWith(QStringLiteral("rpdev_")) && t.accessToken.size() > 6)
            t.outcome = TokenOutcome::SignedIn;
        return t;
    }
    if (reply.status == 400) {
        const QString error = o.value(QStringLiteral("error")).toString();
        if (error == QLatin1String("authorization_pending"))
            t.outcome = TokenOutcome::Pending;
        else if (error == QLatin1String("slow_down"))
            t.outcome = TokenOutcome::SlowDown;
        else if (error == QLatin1String("expired_token"))
            t.outcome = TokenOutcome::Expired;
        else if (error == QLatin1String("bad_verification_code"))
            t.outcome = TokenOutcome::BadCode;
    }
    return t;
}

QString displayUserCode(const QString &userCode)
{
    if (userCode.size() == 8 && !userCode.contains(QLatin1Char('-')))
        return userCode.left(4) + QLatin1Char('-') + userCode.mid(4);
    return userCode;
}

namespace {

constexpr qsizetype kMaxReplyBytes = 64 * 1024;

size_t appendCapped(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<QByteArray *>(userdata);
    const size_t n = size * nmemb;
    if (out->size() + static_cast<qsizetype>(n) > kMaxReplyBytes)
        return 0;   // short write: curl gives up
    out->append(ptr, static_cast<qsizetype>(n));
    return n;
}

HttpReply curlFormPost(const QString &url, const QByteArray &body)
{
    HttpReply r;
    CURL *c = curl_easy_init();
    if (!c) {
        r.status = -1;
        r.transportError = QStringLiteral("curl_easy_init failed");
        return r;
    }
    CurlNetworkConfig::instance().applyCurlSettings(
        c, CurlNetworkConfig::FetchProfile::FireAndForget);
    // The status and body are the answer, 4xx included.
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "Accept: application/json");

    const QByteArray urlUtf8 = url.toUtf8();
    curl_easy_setopt(c, CURLOPT_URL, urlUtf8.constData());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.constData());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendCapped);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    char errbuf[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode ret = curl_easy_perform(c);
    if (ret != CURLE_OK) {
        r.status = -1;
        r.transportError = QString::fromLatin1(errbuf[0] ? errbuf : curl_easy_strerror(ret));
    } else {
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        r.status = code;
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return r;
}

QString resolveBaseUrl(const QString &baseUrl)
{
    if (!baseUrl.isEmpty())
        return baseUrl;
    const QByteArray fromEnv = qgetenv("RPI_IMAGER_CONNECT_URL");
    if (!fromEnv.isEmpty())
        return QString::fromUtf8(fromEnv);
    return QString::fromLatin1(CONNECT_API_URL);
}

} // namespace

} // namespace rpi_connect

using namespace rpi_connect;

ConnectDeviceSignIn::ConnectDeviceSignIn(const QString &clientId, const QString &baseUrl,
                                         Poster poster, QObject *parent)
    : QObject(parent),
      _clientId(clientId),
      _baseUrl(rpi_connect::resolveBaseUrl(baseUrl)),
      _poster(poster ? std::move(poster) : Poster(&rpi_connect::curlFormPost))
{
    _pollTimer.setSingleShot(true);
    connect(&_pollTimer, &QTimer::timeout, this, &ConnectDeviceSignIn::poll);
}

ConnectDeviceSignIn::~ConnectDeviceSignIn() = default;

void ConnectDeviceSignIn::start()
{
    finish();
    _active = true;
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), _clientId);
    post(QStringLiteral("/client/device"), form.query(QUrl::FullyEncoded).toUtf8(),
         [this](const HttpReply &reply) {
        const DeviceAuthorisation a = parseDeviceAuthorisation(reply);
        if (!a.ok) {
            qWarning() << "Connect device sign-in: no code:" << a.error
                       << reply.status << reply.transportError;
            finish();
            emit failed(a.error);
            return;
        }
        _deviceCode = a.deviceCode;
        _expiresInSecs = a.expiresInSecs;
        _intervalSecs = a.intervalSecs;
        _sinceCode.start();
        emit codeReady(a.userCode, a.verificationUri, a.expiresInSecs);
        pollSoon();
    });
}

void ConnectDeviceSignIn::cancel()
{
    finish();
}

void ConnectDeviceSignIn::finish()
{
    _active = false;
    _pollTimer.stop();
    _deviceCode.clear();
    ++_generation;
}

void ConnectDeviceSignIn::post(const QString &path, const QByteArray &body,
                               const std::function<void(const HttpReply &)> &then)
{
    const quint64 generation = _generation;
    auto *watcher = new QFutureWatcher<HttpReply>(this);
    connect(watcher, &QFutureWatcher<HttpReply>::finished, this,
            [this, watcher, generation, then]() {
        const HttpReply reply = watcher->result();
        watcher->deleteLater();
        // A reply to a sign-in since cancelled or restarted.
        if (!_active || generation != _generation)
            return;
        then(reply);
    });
    watcher->setFuture(QtConcurrent::run(_poster, _baseUrl + path, body));
}

void ConnectDeviceSignIn::pollSoon()
{
    _pollTimer.start(std::max(_intervalSecs * 1000, _minPollMs));
}

void ConnectDeviceSignIn::poll()
{
    if (!_active)
        return;
    if (_sinceCode.elapsed() >= qint64(_expiresInSecs) * 1000) {
        finish();
        emit failed(QStringLiteral("expired"));
        return;
    }
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), _clientId);
    form.addQueryItem(QStringLiteral("device_code"), _deviceCode);
    post(QStringLiteral("/client/token"), form.query(QUrl::FullyEncoded).toUtf8(),
         [this](const HttpReply &reply) {
        const TokenReply t = classifyTokenReply(reply);
        switch (t.outcome) {
        case TokenOutcome::Pending:
            pollSoon();
            return;
        case TokenOutcome::SlowDown:
            _intervalSecs += 5;
            pollSoon();
            return;
        case TokenOutcome::SignedIn: {
            const QString deviceId = t.deviceId, token = t.accessToken;
            finish();
            emit signedIn(deviceId, token);
            return;
        }
        case TokenOutcome::Expired:
            finish();
            emit failed(QStringLiteral("expired"));
            return;
        case TokenOutcome::BadCode:
            finish();
            emit failed(QStringLiteral("badcode"));
            return;
        case TokenOutcome::Unauthorized:
            finish();
            emit failed(QStringLiteral("unauthorized"));
            return;
        case TokenOutcome::Failed:
            // A dropped connection is no reason to throw the code away:
            // ask again, and let the expiry decide. Anything that did answer
            // and cannot be read is not going to get better.
            if (reply.status < 0) {
                pollSoon();
                return;
            }
            qWarning() << "Connect device sign-in: unreadable token reply" << reply.status;
            finish();
            emit failed(QStringLiteral("malformed"));
            return;
        }
    });
}
