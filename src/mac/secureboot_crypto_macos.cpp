/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * macOS PAL implementation of SecureBootCrypto.
 *   rsaSignSha256       — Security framework (SecKeyCreateSignature with
 *                          kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256).
 *   extractRsaPubkeyBin — openssl subprocess + shared DER parser
 *                          (macOS ships openssl on PATH).
 */

#include "../secureboot_crypto.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QString>

#include <Security/Security.h>

namespace {

// Read one DER tag-length header at `pos`, leaving `pos` on the contents.
bool derHeader(const QByteArray& der, int& pos, quint8& tag, int& length)
{
    if (pos + 2 > der.size())
        return false;
    tag = static_cast<quint8>(der.at(pos++));
    const quint8 first = static_cast<quint8>(der.at(pos++));
    if (first < 0x80) {
        length = first;
    } else {
        const int count = first & 0x7F;
        if (count == 0 || count > 4 || pos + count > der.size())
            return false;
        length = 0;
        for (int i = 0; i < count; ++i)
            length = (length << 8) | static_cast<quint8>(der.at(pos++));
    }
    return length >= 0 && pos + length <= der.size();
}

// The RSAPrivateKey inside a PKCS#8 PrivateKeyInfo, or an empty result if
// this is not one.
QByteArray pkcs8ToPkcs1(const QByteArray& der)
{
    int pos = 0;
    quint8 tag = 0;
    int length = 0;
    if (!derHeader(der, pos, tag, length) || tag != 0x30)   // SEQUENCE
        return {};
    if (!derHeader(der, pos, tag, length) || tag != 0x02)   // INTEGER version
        return {};
    pos += length;
    if (!derHeader(der, pos, tag, length) || tag != 0x30)   // AlgorithmIdentifier
        return {};
    pos += length;
    if (!derHeader(der, pos, tag, length) || tag != 0x04)   // OCTET STRING
        return {};
    return der.mid(pos, length);
}

// The DER body of a PEM file, as PKCS#1, whichever of the two it was written
// as. Encrypted keys are not handled -- they were not before either.
QByteArray readRsaPrivateKeyDer(const QByteArray& pem, QString* what)
{
    const bool pkcs8 = pem.contains("BEGIN PRIVATE KEY");
    const bool pkcs1 = pem.contains("BEGIN RSA PRIVATE KEY");
    if (!pkcs8 && !pkcs1) {
        if (what)
            *what = QStringLiteral("not an unencrypted RSA private key in PEM form");
        return {};
    }

    QByteArray base64;
    bool inBody = false;
    for (const QByteArray& line : pem.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("-----BEGIN")) {
            inBody = true;
        } else if (trimmed.startsWith("-----END")) {
            break;
        } else if (inBody) {
            base64 += trimmed;
        }
    }

    const QByteArray der = QByteArray::fromBase64(base64);
    if (der.isEmpty()) {
        if (what)
            *what = QStringLiteral("PEM body did not decode");
        return {};
    }
    if (!pkcs8)
        return der;

    const QByteArray unwrapped = pkcs8ToPkcs1(der);
    if (unwrapped.isEmpty() && what)
        *what = QStringLiteral("PKCS#8 wrapper did not parse");
    return unwrapped;
}

} // namespace

namespace SecureBootCrypto {

QByteArray rsaSignSha256(const QByteArray& sha256Digest, const QString& rsaKeyPath)
{
    QFile keyFile(rsaKeyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qDebug() << "SecureBootCrypto/mac: cannot open key" << rsaKeyPath;
        return {};
    }
    QByteArray pemData = keyFile.readAll();
    keyFile.close();

    QString why;
    const QByteArray keyDer = readRsaPrivateKeyDer(pemData, &why);
    if (keyDer.isEmpty()) {
        qDebug() << "SecureBootCrypto/mac: cannot read" << rsaKeyPath << ":" << why;
        return {};
    }

    CFDataRef keyData = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(keyDer.constData()), keyDer.size());
    if (!keyData) {
        qDebug() << "SecureBootCrypto/mac: CFDataCreate(keyData) failed";
        return {};
    }

    const void* attrKeys[] = { kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits };
    const int bits = keyDer.size() >= 1100 ? 4096 : 2048;
    CFNumberRef sizeInBits = CFNumberCreate(nullptr, kCFNumberIntType, &bits);
    const void* attrValues[] = { kSecAttrKeyTypeRSA, kSecAttrKeyClassPrivate, sizeInBits };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, attrKeys, attrValues, 3,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);

    CFErrorRef importError = nullptr;
    SecKeyRef privateKey = SecKeyCreateWithData(keyData, attrs, &importError);
    CFRelease(keyData);
    if (sizeInBits) CFRelease(sizeInBits);
    if (attrs) CFRelease(attrs);
    if (!privateKey) {
        if (importError) {
            CFStringRef desc = CFErrorCopyDescription(importError);
            qDebug() << "SecureBootCrypto/mac: SecKeyCreateWithData failed:"
                     << QString::fromCFString(desc);
            CFRelease(desc);
            CFRelease(importError);
        } else {
            qDebug() << "SecureBootCrypto/mac: SecKeyCreateWithData failed";
        }
        return {};
    }

    CFDataRef dataToSign = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(sha256Digest.constData()), sha256Digest.size());
    if (!dataToSign) {
        qDebug() << "SecureBootCrypto/mac: CFDataCreate(dataToSign) failed";
        CFRelease(privateKey);
        return {};
    }

    // The "Digest" variant of the PKCS1-SHA256 algorithm expects the raw
    // 32-byte hash and applies the PKCS#1 v1.5 + DigestInfo wrap internally
    // — matches what the Windows CryptoAPI path does and what the boot ROM
    // verifier expects.
    CFErrorRef error = nullptr;
    CFDataRef signature = SecKeyCreateSignature(privateKey,
        kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256, dataToSign, &error);
    CFRelease(dataToSign);
    CFRelease(privateKey);
    if (!signature) {
        if (error) {
            CFStringRef desc = CFErrorCopyDescription(error);
            qDebug() << "SecureBootCrypto/mac: SecKeyCreateSignature failed:"
                     << QString::fromCFString(desc);
            CFRelease(desc);
            CFRelease(error);
        }
        return {};
    }

    const UInt8* bytes = CFDataGetBytePtr(signature);
    CFIndex length = CFDataGetLength(signature);
    QByteArray result(reinterpret_cast<const char*>(bytes), static_cast<int>(length));
    CFRelease(signature);
    return result.toHex();
}

QByteArray extractRsaPubkeyBin(const QString& rsaKeyPath)
{
    // Use the openssl CLI (always available on macOS) to fetch a
    // SubjectPublicKeyInfo DER and feed it to the shared parser.
    QProcess proc;
    proc.start("openssl", QStringList()
        << "pkey" << "-in" << rsaKeyPath << "-pubout" << "-outform" << "DER");
    if (!proc.waitForStarted(5000)) {
        qDebug() << "SecureBootCrypto/mac: failed to start openssl";
        return {};
    }
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        qDebug() << "SecureBootCrypto/mac: openssl failed:"
                 << proc.readAllStandardError();
        return {};
    }
    QByteArray der = proc.readAllStandardOutput();
    if (der.isEmpty()) {
        qDebug() << "SecureBootCrypto/mac: empty DER output from openssl";
        return {};
    }
    return parseSubjectPublicKeyInfoDerToNE(der);
}

}  // namespace SecureBootCrypto
