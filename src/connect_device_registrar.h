/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Registers a Raspberry Pi Connect Device Identity for a device
 * currently attached in fastboot mode.
 *
 * The device's firmware ECDSA key never leaves the hardware: the
 * host computes SHA-256 digests, the device signs them via
 * "oem fwcrypto sign-hash".  The public key is retrieved via
 * "getvar public-key".
 *
 * Mirrors the behaviour of rpi-sb-provisioner's
 * connect_register_device() shell routine.
 */

#ifndef CONNECT_DEVICE_REGISTRAR_H
#define CONNECT_DEVICE_REGISTRAR_H

#include <QByteArray>
#include <QString>

namespace fastboot { class FastbootProtocol; }
namespace rpiboot { class IUsbTransport; }

class ConnectDeviceRegistrar
{
public:
    struct Result {
        bool ok = false;
        QString deviceId;      // Connect device identity ID on success
        QString errorMessage;  // Set when ok == false
    };

    // Creates a registrar.  Empty apiKey => registration will be skipped
    // (the caller should check isEnabled() before calling).
    ConnectDeviceRegistrar(const QString &apiKey,
                           const QString &descriptionPrefix,
                           const QString &baseUrl = QString());

    bool isEnabled() const { return !_apiKey.isEmpty(); }

    // Where this instance will post. Readable so a caller -- or a test --
    // can tell a redirected registrar from one talking to production
    // without having to watch the wire.
    const QString &baseUrl() const { return _baseUrl; }

    // Perform the full registration flow against the given fastboot
    // device.  Reads the public key, asks the device to sign the
    // request, POSTs to the Connect management API, and parses the
    // returned device identity ID.
    //
    // boardDescription / serial are appended to any descriptionPrefix
    // to build the "description" field sent to the API (matches the
    // shell provisioner's format: "<prefix> <board> <serial>").
    Result registerDevice(fastboot::FastbootProtocol &fb,
                          rpiboot::IUsbTransport &transport,
                          const QString &boardDescription,
                          const QString &serial);

    struct AuthKeyResult {
        bool ok = false;
        QString id;            // UUIDv4 of the auth key
        QString secret;        // "rpoak_<base58>" — write to the device image
        QString errorMessage;  // Set when ok == false
    };

    // Mint a Raspberry Pi Connect organisation auth key by POSTing
    // to /organisation/auth-keys.  Used when the target storage is
    // not a fastboot device, so we cannot register a per-device
    // identity.  ttlDays defaults to 1 day on the server side; pass
    // <= 0 to omit the field and accept the server default.
    AuthKeyResult requestAuthKey(const QString &description, int ttlDays = 1);

private:
    QByteArray sha256Hex(const QByteArray &data) const;

    // HTTP POST returning {httpCode, responseBody}.  On transport
    // failure returns httpCode = -1 and error message in body.
    struct HttpResult {
        long httpCode = 0;
        QByteArray body;
        QString curlError;
    };
    HttpResult httpPost(const QString &url,
                        const QByteArray &body,
                        const QByteArray &bearerToken,
                        const QByteArray &signatureHeader) const;

    QString _apiKey;
    QString _descriptionPrefix;
    QString _baseUrl;
};

#endif // CONNECT_DEVICE_REGISTRAR_H
