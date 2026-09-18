/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Linux PAL implementation of SecureBootCrypto.  Both operations shell
 * out to `openssl` — reliably on PATH on Linux distros.  rsaSignSha256
 * does its own PKCS#1 DigestInfo DER wrap (since `openssl rsautl -sign
 * -pkcs` performs raw RSA without inserting the DigestInfo header that
 * the boot ROM verifier requires); extractRsaPubkeyBin fetches the
 * SubjectPublicKeyInfo DER via `openssl pkey -pubout` and feeds it to
 * the shared parser.
 */

#include "../secureboot_crypto.h"

#include <QByteArray>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QString>
#include <QStringList>

namespace SecureBootCrypto {

QByteArray rsaSignSha256(const QByteArray& sha256Digest, const QString& rsaKeyPath)
{
    if (sha256Digest.size() != 32) {
        qDebug() << "SecureBootCrypto/linux: expected 32-byte SHA-256 input, got"
                 << sha256Digest.size();
        return {};
    }

    // PKCS#1 DigestInfo for SHA-256, then the 32-byte hash.
    //   30 31                            SEQUENCE (49)
    //     30 0d                          SEQUENCE (13)  AlgorithmIdentifier
    //       06 09 60 86 48 01 65 03 04 02 01   OID 2.16.840.1.101.3.4.2.1 (SHA-256)
    //       05 00                        NULL
    //     04 20 <32 bytes>               OCTET STRING (32)  digest
    QByteArray derWrapped;
    derWrapped.append("\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01\x05\x00\x04\x20", 19);
    derWrapped.append(sha256Digest);
    if (derWrapped.size() != 51) {
        qDebug() << "SecureBootCrypto/linux: bad DigestInfo size:" << derWrapped.size();
        return {};
    }

    QProcess proc;
    proc.start("openssl", QStringList()
        << "rsautl" << "-sign" << "-inkey" << rsaKeyPath << "-pkcs");
    if (!proc.waitForStarted()) {
        qDebug() << "SecureBootCrypto/linux: failed to start openssl";
        return {};
    }
    proc.write(derWrapped);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(30000)) {
        qDebug() << "SecureBootCrypto/linux: openssl timeout";
        return {};
    }
    if (proc.exitCode() != 0) {
        qDebug() << "SecureBootCrypto/linux: openssl failed:"
                 << proc.readAllStandardError();
        return {};
    }
    return proc.readAllStandardOutput().toHex();
}

QByteArray extractRsaPubkeyBin(const QString& rsaKeyPath)
{
    QProcess proc;
    proc.start("openssl", QStringList()
        << "pkey" << "-in" << rsaKeyPath << "-pubout" << "-outform" << "DER");
    if (!proc.waitForStarted(5000)) {
        qDebug() << "SecureBootCrypto/linux: failed to start openssl (pkey)";
        return {};
    }
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        qDebug() << "SecureBootCrypto/linux: openssl (pkey) failed:"
                 << proc.readAllStandardError();
        return {};
    }
    QByteArray der = proc.readAllStandardOutput();
    if (der.isEmpty()) {
        qDebug() << "SecureBootCrypto/linux: empty DER output from openssl";
        return {};
    }
    return parseSubjectPublicKeyInfoDerToNE(der);
}


// Two openssl runs: one to make the private key, one to derive the public
// half from it. Moved here from the provisioner, which called openssl
// directly and so had no way to differ per platform -- Windows has none.
bool generateRsaKeyPair(const QString& privateKeyPath, const QString& publicKeyPath)
{
    {
        QProcess proc;
        proc.start(QStringLiteral("openssl"),
                   {QStringLiteral("genrsa"), QStringLiteral("-out"),
                    privateKeyPath, QStringLiteral("2048")});
        if (!proc.waitForStarted(5000)) {
            qDebug() << "SecureBootCrypto: failed to start openssl for key generation";
            return false;
        }
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            qDebug() << "SecureBootCrypto: openssl genrsa failed:"
                     << proc.readAllStandardError();
            return false;
        }
    }

    {
        QProcess proc;
        proc.start(QStringLiteral("openssl"),
                   {QStringLiteral("rsa"), QStringLiteral("-in"), privateKeyPath,
                    QStringLiteral("-outform"), QStringLiteral("PEM"),
                    QStringLiteral("-pubout"), QStringLiteral("-out"), publicKeyPath});
        if (!proc.waitForStarted(5000)) {
            qDebug() << "SecureBootCrypto: failed to start openssl for the public half";
            // Never leave half a pair behind: the private key is a real RSA
            // key, and Imager's own file chooser would offer it.
            QFile::remove(privateKeyPath);
            return false;
        }
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            qDebug() << "SecureBootCrypto: openssl rsa failed:"
                     << proc.readAllStandardError();
            QFile::remove(privateKeyPath);
            return false;
        }
    }

    // Asked for rather than assumed: openssl can exit zero having written
    // nothing, and a caller told the pair exists is about to fuse the hash
    // of a file that is not there.
    if (!QFile::exists(privateKeyPath) || !QFile::exists(publicKeyPath)) {
        qDebug() << "SecureBootCrypto: openssl reported success but wrote no key";
        QFile::remove(privateKeyPath);
        QFile::remove(publicKeyPath);
        return false;
    }
    return true;
}

}  // namespace SecureBootCrypto
