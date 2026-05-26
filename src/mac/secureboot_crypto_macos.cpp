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

    CFDataRef keyData = CFDataCreate(nullptr,
        reinterpret_cast<const UInt8*>(pemData.constData()), pemData.size());
    if (!keyData) {
        qDebug() << "SecureBootCrypto/mac: CFDataCreate(keyData) failed";
        return {};
    }

    SecExternalFormat format = kSecFormatPEMSequence;
    SecExternalItemType itemType = kSecItemTypePrivateKey;
    SecItemImportExportKeyParameters params{};
    params.version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION;
    params.passphrase = nullptr;

    CFArrayRef items = nullptr;
    OSStatus status = SecItemImport(keyData, nullptr, &format, &itemType, 0,
                                     &params, nullptr, &items);
    CFRelease(keyData);
    if (status != errSecSuccess || !items || CFArrayGetCount(items) == 0) {
        qDebug() << "SecureBootCrypto/mac: SecItemImport failed, status=" << status;
        if (items) CFRelease(items);
        return {};
    }

    SecKeyRef privateKey = (SecKeyRef)CFArrayGetValueAtIndex(items, 0);
    CFRetain(privateKey);
    CFRelease(items);

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
