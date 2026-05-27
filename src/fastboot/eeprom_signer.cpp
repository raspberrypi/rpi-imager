/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */
#include "eeprom_signer.h"

#include "../secureboot_crypto.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QString>

#include <cstdio>

namespace fastboot {

namespace {

QByteArray sha256Hex(std::span<const uint8_t> input)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QByteArrayView(reinterpret_cast<const char*>(input.data()),
                             static_cast<qsizetype>(input.size())));
    return h.result().toHex();
}

} // namespace

EepromSignResult buildEepromSig(std::span<const uint8_t> input,
                                const EepromSignerConfig& config)
{
    EepromSignResult result;

    // Line 1: lowercase SHA-256 hex of the input.
    const QByteArray digestHex = sha256Hex(input);
    if (digestHex.size() != 64) {
        result.error = "unexpected SHA-256 hex length";
        return result;
    }

    // Line 2: "ts: <epoch>"
    qint64 epoch = config.sourceDateEpoch.has_value()
                 ? *config.sourceDateEpoch
                 : QDateTime::currentSecsSinceEpoch();

    QByteArray out;
    out.append(digestHex);
    out.append('\n');
    out.append("ts: ");
    out.append(QByteArray::number(epoch));
    out.append('\n');

    // Line 3 (optional): "rsa2048: <hex>"
    if (!config.pemKeyPath.empty()) {
        QFileInfo keyFi(QString::fromStdString(config.pemKeyPath));
        if (!keyFi.exists() || !keyFi.isReadable()) {
            result.error = "RSA key not readable: " + config.pemKeyPath;
            return result;
        }

        // SecureBootCrypto::rsaSignSha256 expects the raw 32-byte SHA-256
        // digest (NOT the input bytes themselves; it performs the
        // PKCS#1 DigestInfo wrap and the RSA-2048-PKCS#1-v1.5 signature
        // internally). Returns a hex-encoded 512-char signature for a
        // 2048-bit key, or empty on failure.
        QByteArray rawDigest = QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char*>(input.data()),
                           static_cast<qsizetype>(input.size())),
            QCryptographicHash::Sha256);
        if (rawDigest.size() != 32) {
            result.error = "unexpected raw SHA-256 digest length";
            return result;
        }

        QByteArray sigHex = SecureBootCrypto::rsaSignSha256(
            rawDigest, QString::fromStdString(config.pemKeyPath));
        if (sigHex.isEmpty()) {
            result.error = "RSA signing failed (see qDebug logs for platform detail)";
            return result;
        }
        if (sigHex.size() != 512) {
            result.error = "unexpected signature length: " + std::to_string(sigHex.size())
                         + " hex chars (expected 512 for RSA-2048)";
            return result;
        }
        out.append("rsa2048: ");
        out.append(sigHex);
        out.append('\n');
    }

    result.sigBytes.assign(reinterpret_cast<const uint8_t*>(out.constData()),
                           reinterpret_cast<const uint8_t*>(out.constData()) + out.size());
    result.ok = true;
    return result;
}

} // namespace fastboot
