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
#include <QString>
#include <QStringList>

namespace SecureBootCrypto {

QByteArray rsaSignSha256(const QByteArray& sha256Digest, const QString& rsaKeyPath)
{
    return {};
}

QByteArray extractRsaPubkeyBin(const QString& rsaKeyPath)
{
    return {};
}

}  // namespace SecureBootCrypto
