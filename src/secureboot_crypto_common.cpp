/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The crypto helpers that are the same everywhere.
 *
 * Signing and key generation are the platform's to do -- CNG on Windows,
 * Security on macOS, openssl elsewhere -- but reading a DER public key and
 * getting the DER out of a PEM are just bytes, and every platform needs
 * both. They used to live in secureboot.cpp, which drags in the FAT driver,
 * the boot image builder and the hash backend with it; anything wanting to
 * parse a key had to link all of that or go without.
 */

#include "secureboot_crypto.h"
#include "asn1_length.h"

using rpi_imager::asn1ParseLength;

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>

// The DER inside a public-key PEM.
//
// This is what `openssl rsa -pubin -in <pem> -outform DER` prints: the PEM
// body is that DER, base64'd, so decoding the body is the whole conversion.
// Worth doing here rather than by subprocess, because the SHA-256 of these
// bytes is fused into a device permanently and openssl is not on every
// machine that runs this -- Windows ships none at all.
QByteArray SecureBootCrypto::publicKeyPemToDer(const QString& publicKeyPath)
{
    QFile f(publicKeyPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "SecureBootCrypto::publicKeyPemToDer: cannot open" << publicKeyPath;
        return {};
    }
    const QByteArray pem = f.readAll();
    f.close();

    static const QByteArray kBegin = QByteArrayLiteral("-----BEGIN PUBLIC KEY-----");
    static const QByteArray kEnd = QByteArrayLiteral("-----END PUBLIC KEY-----");
    const int begin = pem.indexOf(kBegin);
    if (begin < 0) {
        qDebug() << "SecureBootCrypto::publicKeyPemToDer:" << publicKeyPath
                 << "is not a public key PEM";
        return {};
    }
    const int bodyStart = begin + kBegin.size();
    const int end = pem.indexOf(kEnd, bodyStart);
    if (end < 0) {
        qDebug() << "SecureBootCrypto::publicKeyPemToDer: unterminated PEM in"
                 << publicKeyPath;
        return {};
    }

    // Base64Encoding refuses anything that is not base64, so a PEM with
    // rubbish between the armour yields nothing rather than a short DER that
    // would hash to a plausible-looking value.
    const auto decoded = QByteArray::fromBase64Encoding(
        pem.mid(bodyStart, end - bodyStart).simplified().replace(' ', ""),
        QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) {
        qDebug() << "SecureBootCrypto::publicKeyPemToDer: body of" << publicKeyPath
                 << "is not base64";
        return {};
    }

    // Parsed before it is handed back, not merely decoded. Every character of
    // "not a key" is a base64 character, so armour with a few words inside it
    // decodes happily to a handful of bytes -- and the caller hashes those
    // into the value it fuses. A hash of five bytes of nothing looks exactly
    // like a hash of a key until the board refuses to boot.
    if (SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(*decoded).size() != 264) {
        qDebug() << "SecureBootCrypto::publicKeyPemToDer:" << publicKeyPath
                 << "does not hold an RSA-2048 public key";
        return {};
    }
    return *decoded;
}

QByteArray SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE(const QByteArray& der)
{
    if (der.isEmpty()) {
        qDebug() << "SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE: empty DER";
        return {};
    }

    // SubjectPublicKeyInfo := SEQUENCE { AlgorithmIdentifier, BIT STRING }
    // where the BIT STRING wraps an RSAPublicKey := SEQUENCE { N, E }.
    const auto *d = reinterpret_cast<const uint8_t*>(der.constData());
    const int dlen = der.size();
    int i = 0;

    auto fail = [](const char *why) -> QByteArray {
        qDebug() << "SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE: DER parse:" << why;
        return {};
    };

    if (i >= dlen || d[i++] != 0x30) return fail("expected SEQUENCE");
    if (asn1ParseLength(d, dlen, &i) < 0) return fail("outer length");

    // Skip AlgorithmIdentifier
    if (i >= dlen || d[i++] != 0x30) return fail("expected algo SEQUENCE");
    int algoLen = asn1ParseLength(d, dlen, &i);
    if (algoLen < 0 || i + algoLen > dlen) return fail("algo length");
    i += algoLen;

    // BIT STRING wraps the RSAPublicKey
    if (i >= dlen || d[i++] != 0x03) return fail("expected BIT STRING");
    if (asn1ParseLength(d, dlen, &i) < 0) return fail("bit-string length");
    if (i >= dlen || d[i++] != 0x00) return fail("expected 0 unused bits");

    // RSAPublicKey SEQUENCE { N, E }
    if (i >= dlen || d[i++] != 0x30) return fail("expected RSAPublicKey SEQUENCE");
    if (asn1ParseLength(d, dlen, &i) < 0) return fail("rsa-pubkey length");

    // INTEGER N
    if (i >= dlen || d[i++] != 0x02) return fail("expected INTEGER N");
    int nLen = asn1ParseLength(d, dlen, &i);
    if (nLen < 0 || i + nLen > dlen) return fail("N length");
    // Strip the leading 0x00 sign byte that ASN.1 prepends to keep N positive.
    if (nLen > 0 && d[i] == 0x00) { ++i; --nLen; }
    if (nLen != 256) {
        qDebug() << "SecureBootCrypto::parseSubjectPublicKeyInfoDerToNE: expected 2048-bit key, got"
                 << (nLen * 8) << "bits";
        return {};
    }
    QByteArray nBE(reinterpret_cast<const char*>(d + i), nLen);
    i += nLen;

    // INTEGER E
    if (i >= dlen || d[i++] != 0x02) return fail("expected INTEGER E");
    int eLen = asn1ParseLength(d, dlen, &i);
    if (eLen < 0 || eLen > 8 || i + eLen > dlen) return fail("E length");
    QByteArray eBE(reinterpret_cast<const char*>(d + i), eLen);

    // The bootloader expects raw N (256 bytes, little-endian) followed by
    // raw E (8 bytes, little-endian).  Both are big-endian in DER.
    QByteArray result;
    result.reserve(264);
    for (int j = nBE.size() - 1; j >= 0; --j)
        result.append(nBE[j]);
    while (result.size() < 256)
        result.append(char(0));

    QByteArray eLE;
    for (int j = eBE.size() - 1; j >= 0; --j)
        eLE.append(eBE[j]);
    while (eLE.size() < 8)
        eLE.append(char(0));
    eLE.resize(8);
    result.append(eLE);

    return result;
}
