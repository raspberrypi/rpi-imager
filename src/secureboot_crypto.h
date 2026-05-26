/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Platform-neutral declarations for the RSA crypto operations used by
 * SecureBoot.  Each platform under src/{linux,mac,windows}/ provides its
 * own implementation — Security framework on macOS, CryptoAPI on Windows,
 * openssl-subprocess on Linux — and the shared SecureBoot code calls
 * these free functions without any platform conditionals of its own.
 *
 * Wire-up: each platform's Platform.cmake adds its implementation file
 * to PLATFORM_SOURCES.
 */

#ifndef SECUREBOOT_CRYPTO_H
#define SECUREBOOT_CRYPTO_H

#include <QByteArray>
#include <QString>

namespace SecureBootCrypto {

// RSA-PKCS#1 v1.5 sign the supplied SHA-256 digest (32 bytes, raw — not
// DER-wrapped) using the private key at `rsaKeyPath` (PEM, PKCS#1 or
// PKCS#8).  Returns the hex-encoded signature, or an empty QByteArray on
// failure.  Errors are logged via qDebug() at the platform implementation.
QByteArray rsaSignSha256(const QByteArray& sha256Digest,
                          const QString& rsaKeyPath);

// Read the public key from `rsaKeyPath` (PEM, PKCS#1 or PKCS#8 — works
// with private keys too, like `openssl pkey -pubout`) and return the
// boot-ROM-format 264-byte blob: 256 bytes of modulus N (little-endian)
// followed by 8 bytes of exponent E (little-endian, zero-padded to 8).
// Returns empty on failure.
QByteArray extractRsaPubkeyBin(const QString& rsaKeyPath);

// Parse a DER-encoded SubjectPublicKeyInfo (e.g. `openssl pkey -pubout
// -outform DER` output) into the 264-byte (N little-endian || E
// little-endian) blob the boot ROM expects.  Pure ASN.1 parsing, no
// platform dependencies — used by the macOS and Linux PAL implementations
// of extractRsaPubkeyBin which share the same openssl-subprocess source
// for the DER.  Windows skips DER entirely (CryptoAPI's PUBLICKEYBLOB
// already lays out N+E in the target byte order).
QByteArray parseSubjectPublicKeyInfoDerToNE(const QByteArray& der);

}  // namespace SecureBootCrypto

#endif  // SECUREBOOT_CRYPTO_H
