/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "connect_device_registrar.h"
#include "fastboot/fastboot_protocol.h"
#include "rpiboot/usb_transport.h"
#include "curlnetworkconfig.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include <curl/curl.h>

#include <string>
#include <string_view>

namespace {

constexpr const char *DEFAULT_CONNECT_BASE_URL =
    "https://api.connect.raspberrypi.com";

// Scan INFO lines for a PEM public key block and return it as a
// newline-terminated string, exactly as the API expects.
QString extractPemPublicKey(const std::vector<std::string> &infoLines)
{
    const std::string_view BEGIN_MARK = "-----BEGIN PUBLIC KEY-----";
    const std::string_view END_MARK = "-----END PUBLIC KEY-----";

    QStringList pem;
    bool inBlock = false;
    for (const auto &raw : infoLines) {
        // Some firmware variants return a leading space after "INFO".
        std::string line = raw;
        if (!line.empty() && line.front() == ' ')
            line.erase(0, 1);

        auto beginPos = line.find(BEGIN_MARK);
        if (beginPos != std::string::npos) {
            inBlock = true;
            pem.clear();
            pem.append(QString::fromStdString(line.substr(beginPos)));
            // A single-line response containing the whole PEM block
            // is possible in theory; handle it here too.
            if (line.find(END_MARK) != std::string::npos)
                break;
            continue;
        }
        if (!inBlock)
            continue;
        pem.append(QString::fromStdString(line));
        if (line.find(END_MARK) != std::string::npos)
            break;
    }

    if (pem.isEmpty())
        return {};

    return pem.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// Signatures come back as a single INFO line of the form
// "connect-signature:<hex>".  Strip anything up to and including the
// prefix.
QString extractSignature(const std::vector<std::string> &infoLines)
{
    const std::string_view PREFIX = "connect-signature:";
    for (const auto &raw : infoLines) {
        auto pos = raw.find(PREFIX);
        if (pos == std::string::npos)
            continue;
        std::string sig = raw.substr(pos + PREFIX.size());
        // Trim whitespace / CR / LF
        while (!sig.empty() &&
               (sig.back() == '\r' || sig.back() == '\n' || sig.back() == ' '))
            sig.pop_back();
        return QString::fromStdString(sig);
    }
    return {};
}

size_t curlWriteToByteArray(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<QByteArray *>(userdata);
    const size_t n = size * nmemb;
    out->append(ptr, static_cast<int>(n));
    return n;
}

} // namespace

ConnectDeviceRegistrar::ConnectDeviceRegistrar(const QString &apiKey,
                                                 const QString &descriptionPrefix,
                                                 const QString &baseUrl)
    : _apiKey(apiKey.trimmed())
    , _descriptionPrefix(descriptionPrefix.trimmed())
    , _baseUrl(baseUrl.isEmpty() ? QString::fromLatin1(DEFAULT_CONNECT_BASE_URL)
                                 : baseUrl)
{
}

QByteArray ConnectDeviceRegistrar::sha256Hex(const QByteArray &data) const
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

ConnectDeviceRegistrar::Result ConnectDeviceRegistrar::registerDevice(
    fastboot::FastbootProtocol &fb,
    rpiboot::IUsbTransport &transport,
    const QString &boardDescription,
    const QString &serial)
{
    Result r;

    if (!isEnabled()) {
        r.errorMessage = QStringLiteral("Connect API key not configured");
        return r;
    }

    qDebug() << "Connect: retrieving device public key...";
    auto pkResp = fb.sendCommandCapture(transport, "getvar:public-key", 10000);
    QString pem = extractPemPublicKey(pkResp.infoLines);
    if (pem.isEmpty()) {
        // Some firmware versions return the PEM block in the OKAY
        // message itself if small enough.
        if (pkResp.terminal.type == fastboot::Response::Okay &&
            pkResp.terminal.message.find("BEGIN PUBLIC KEY") != std::string::npos) {
            std::vector<std::string> one = {pkResp.terminal.message};
            pem = extractPemPublicKey(one);
        }
    }
    if (pem.isEmpty()) {
        r.errorMessage = QStringLiteral(
            "Could not retrieve device public key via fastboot getvar");
        return r;
    }

    // Build JSON body: {"public_key": "<PEM>", "description": "<prefix> <board> <serial>"}
    QString description;
    if (!_descriptionPrefix.isEmpty())
        description = _descriptionPrefix + QLatin1Char(' ');
    description += boardDescription;
    if (!serial.isEmpty())
        description += QLatin1Char(' ') + serial;

    QJsonObject obj;
    obj.insert(QStringLiteral("public_key"), pem);
    obj.insert(QStringLiteral("description"), description);
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    const QString url = _baseUrl + QStringLiteral("/organisation/device-identities");

    // Build signature payload matching Connect's SignatureVerification.
    // Order and header casing must match exactly what we send to the
    // server in the final HTTP request.
    const QByteArray bodyHash = sha256Hex(body);
    QByteArray sigPayload;
    sigPayload.reserve(256);
    sigPayload.append("POST\n");
    sigPayload.append(url.toUtf8());
    sigPayload.append('\n');
    sigPayload.append("Authorization: Bearer ");
    sigPayload.append(_apiKey.toUtf8());
    sigPayload.append('\n');
    sigPayload.append("Content-Type: application/json\n");
    sigPayload.append(bodyHash);

    const QByteArray payloadHash = sha256Hex(sigPayload);

    qDebug() << "Connect: requesting device signature over payload hash";
    std::string signCmd = "oem fwcrypto sign-hash ";
    signCmd.append(payloadHash.constData(), payloadHash.size());
    auto signResp = fb.sendCommandCapture(transport, signCmd, 15000);
    if (signResp.terminal.type != fastboot::Response::Okay) {
        r.errorMessage = QStringLiteral(
            "Device-side signing failed: %1")
            .arg(QString::fromStdString(signResp.terminal.message));
        return r;
    }

    QString signature = extractSignature(signResp.infoLines);
    if (signature.isEmpty()) {
        // Signature may also arrive in the terminal OKAY message.
        std::vector<std::string> one = {signResp.terminal.message};
        signature = extractSignature(one);
    }
    if (signature.isEmpty()) {
        r.errorMessage = QStringLiteral(
            "Device did not return a connect-signature");
        return r;
    }

    qDebug() << "Connect: POSTing device identity to" << url;
    HttpResult http = httpPost(url, body,
                                QByteArrayLiteral("Bearer ") + _apiKey.toUtf8(),
                                signature.toLatin1());

    if (http.httpCode < 0) {
        r.errorMessage = QStringLiteral("Network error: %1").arg(http.curlError);
        return r;
    }

    if (http.httpCode != 201) {
        r.errorMessage = QStringLiteral(
            "Connect API registration failed (HTTP %1): %2")
            .arg(http.httpCode)
            .arg(QString::fromUtf8(http.body));
        return r;
    }

    // Parse device ID from the response body
    QJsonParseError jerr{};
    QJsonDocument doc = QJsonDocument::fromJson(http.body, &jerr);
    if (doc.isObject()) {
        const QJsonValue idVal = doc.object().value(QStringLiteral("id"));
        if (idVal.isString())
            r.deviceId = idVal.toString();
    }

    r.ok = true;
    return r;
}

ConnectDeviceRegistrar::AuthKeyResult ConnectDeviceRegistrar::requestAuthKey(
    const QString &description,
    int ttlDays)
{
    AuthKeyResult r;

    if (!isEnabled()) {
        r.errorMessage = QStringLiteral("Connect API key not configured");
        return r;
    }

    const QString url = _baseUrl + QStringLiteral("/organisation/auth-keys");

    // application/x-www-form-urlencoded body.  curl_easy_escape would
    // require a CURL handle and we want to keep this independent of
    // httpPost's curl setup, so use QUrlQuery which provides RFC 3986
    // percent-encoding.
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("description"), description);
    if (ttlDays > 0)
        form.addQueryItem(QStringLiteral("ttl_days"), QString::number(ttlDays));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    qDebug() << "Connect: POSTing auth-key request to" << url;

    CURL *c = curl_easy_init();
    if (!c) {
        r.errorMessage = QStringLiteral("curl_easy_init failed");
        return r;
    }

    CurlNetworkConfig::instance().applyCurlSettings(
        c, CurlNetworkConfig::FetchProfile::FireAndForget);

    // The shared configuration turns FAILONERROR on, which is right for a
    // download -- a 404 there is nothing but a failure. Here it threw away
    // the part that matters: curl returned CURLE_HTTP_RETURNED_ERROR before
    // the status could be looked at, so every refusal read "Network error:
    // The requested URL returned error: 401", and the body carrying the
    // API's own explanation was discarded with it. The code below is written
    // to read the status and the body, and could never reach either.
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);

    QByteArray responseBody;
    struct curl_slist *headers = nullptr;
    const QByteArray authHeader =
        QByteArrayLiteral("Authorization: Bearer ") + _apiKey.toUtf8();
    headers = curl_slist_append(headers, authHeader.constData());
    headers = curl_slist_append(
        headers, "Content-Type: application/x-www-form-urlencoded");

    const QByteArray urlUtf8 = url.toUtf8();
    curl_easy_setopt(c, CURLOPT_URL, urlUtf8.constData());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.constData());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteToByteArray);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    char errbuf[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    long httpCode = 0;
    CURLcode ret = curl_easy_perform(c);
    if (ret != CURLE_OK) {
        r.errorMessage = QStringLiteral("Network error: %1")
            .arg(QString::fromLatin1(errbuf[0] ? errbuf : curl_easy_strerror(ret)));
    } else {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (!r.errorMessage.isEmpty())
        return r;

    if (httpCode != 201) {
        // 401 → bad management API token.  422 → validation failure
        // with the reason in {"message": "..."} (e.g. "Validation
        // failed: Description can't be blank").  Surface both so
        // the UI can show actionable feedback.
        QString serverMessage;
        QJsonParseError jerr{};
        QJsonDocument doc = QJsonDocument::fromJson(responseBody, &jerr);
        if (doc.isObject()) {
            const QJsonValue m = doc.object().value(QStringLiteral("message"));
            if (m.isString())
                serverMessage = m.toString();
        }

        if (httpCode == 401) {
            r.errorMessage = QStringLiteral(
                "Raspberry Pi Connect rejected the organisation API key (HTTP 401). "
                "Check the key in App Options.");
        } else if (httpCode == 422 && !serverMessage.isEmpty()) {
            r.errorMessage = serverMessage;
        } else {
            const QString detail = serverMessage.isEmpty()
                ? QString::fromUtf8(responseBody)
                : serverMessage;
            r.errorMessage = QStringLiteral(
                "Connect API auth-key request failed (HTTP %1): %2")
                .arg(httpCode).arg(detail);
        }
        return r;
    }

    QJsonParseError jerr{};
    QJsonDocument doc = QJsonDocument::fromJson(responseBody, &jerr);
    if (!doc.isObject()) {
        r.errorMessage = QStringLiteral(
            "Connect API returned non-object body: %1").arg(QString::fromUtf8(responseBody));
        return r;
    }
    const QJsonObject obj = doc.object();
    r.id = obj.value(QStringLiteral("id")).toString();
    r.secret = obj.value(QStringLiteral("secret")).toString();
    if (r.secret.isEmpty()) {
        r.errorMessage = QStringLiteral(
            "Connect API response missing 'secret' field");
        return r;
    }

    // 'description', 'device_name', 'tags' and 'expires_at' are also
    // returned but only expires_at is operationally interesting (the
    // image must reach a device before the key expires).
    qDebug().noquote() << "Connect: minted auth-key id=" << r.id
                       << "expires_at=" << obj.value(QStringLiteral("expires_at")).toString();

    r.ok = true;
    return r;
}

ConnectDeviceRegistrar::HttpResult ConnectDeviceRegistrar::httpPost(
    const QString &url,
    const QByteArray &body,
    const QByteArray &bearerToken,
    const QByteArray &signatureHeader) const
{
    HttpResult result;

    CURL *c = curl_easy_init();
    if (!c) {
        result.httpCode = -1;
        result.curlError = QStringLiteral("curl_easy_init failed");
        return result;
    }

    // Apply shared proxy / IPv4 / CA-bundle configuration.  Use the
    // FireAndForget profile — registration is a short one-shot request.
    CurlNetworkConfig::instance().applyCurlSettings(
        c, CurlNetworkConfig::FetchProfile::FireAndForget);

    // The shared configuration turns FAILONERROR on, which is right for a
    // download -- a 404 there is nothing but a failure. Here it threw away
    // the part that matters: curl returned CURLE_HTTP_RETURNED_ERROR before
    // the status could be looked at, so every refusal read "Network error:
    // The requested URL returned error: 401", and the body carrying the
    // API's own explanation was discarded with it. The code below is written
    // to read the status and the body, and could never reach either.
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);

    struct curl_slist *headers = nullptr;
    const QByteArray authHeader = QByteArrayLiteral("Authorization: ") + bearerToken;
    headers = curl_slist_append(headers, authHeader.constData());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    // Suppress curl's default Accept header — the server signs the
    // headers it receives so any extra header breaks verification.
    headers = curl_slist_append(headers, "Accept:");
    const QByteArray sigHeader =
        QByteArrayLiteral("X-Connect-Identity-Signature: ") + signatureHeader;
    headers = curl_slist_append(headers, sigHeader.constData());

    const QByteArray urlUtf8 = url.toUtf8();
    curl_easy_setopt(c, CURLOPT_URL, urlUtf8.constData());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.constData());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteToByteArray);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    char errbuf[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode ret = curl_easy_perform(c);
    if (ret != CURLE_OK) {
        result.httpCode = -1;
        result.curlError = QString::fromLatin1(errbuf[0]
            ? errbuf
            : curl_easy_strerror(ret));
    } else {
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        result.httpCode = code;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return result;
}
