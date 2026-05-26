/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Windows PAL implementation of SecureBootCrypto.  No subprocess, no PATH
 * dependency on a system openssl.exe — matches the macOS Security
 * framework path in design.  Uses the legacy CryptoAPI for PEM parsing
 * and pubkey export (its PKCS#1/PKCS#8 handling and PUBLICKEYBLOB layout
 * are exactly what we need) and CNG (BCrypt) for the actual signing
 * (CryptSignHash refuses to drive an imported ephemeral key — it expects
 * a key spec in a named CSP container).  Link deps Crypt32 + Bcrypt are
 * declared in src/windows/Platform.cmake.
 *
 *   rsaSignSha256       — CryptoAPI:  PEM → DER → CryptImportKey →
 *                          CryptExportKey(PRIVATEKEYBLOB).
 *                         CNG:        BCryptImportKeyPair(
 *                          LEGACY_RSAPRIVATE_BLOB) →
 *                          BCryptSignHash(BCRYPT_PAD_PKCS1, SHA-256).
 *                         Output is big-endian — no byte-swap needed.
 *   extractRsaPubkeyBin — CryptoAPI:  same import path, then
 *                          CryptExportKey(PUBLICKEYBLOB) and splice 256
 *                          bytes of modulus + 4 bytes of exponent (+ 4
 *                          zero pad) straight out of the blob.  Both
 *                          fields are already in the little-endian byte
 *                          order the boot ROM wants.
 */

#include "../secureboot_crypto.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>

#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>

namespace {

// RAII guards for CryptoAPI handles — every entry-point has many failure
// paths and we want clean release on each one.
struct CryptProvHandle {
    HCRYPTPROV h = 0;
    ~CryptProvHandle() { if (h) CryptReleaseContext(h, 0); }
};
struct CryptKeyHandle {
    HCRYPTKEY h = 0;
    ~CryptKeyHandle() { if (h) CryptDestroyKey(h); }
};
// CryptDecodeObjectEx with CRYPT_DECODE_ALLOC_FLAG allocates via LocalAlloc.
struct LocalFreePtr {
    BYTE* p = nullptr;
    ~LocalFreePtr() { if (p) LocalFree(p); }
};

// CNG handles for the signing step — CryptoAPI's CryptSignHash can't drive
// an ephemeral imported key (it expects a key spec in a named container),
// so we hand the exported blob to BCrypt instead.
struct BCryptAlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    ~BCryptAlgHandle() { if (h) BCryptCloseAlgorithmProvider(h, 0); }
};
struct BCryptKeyHandle {
    BCRYPT_KEY_HANDLE h = nullptr;
    ~BCryptKeyHandle() { if (h) BCryptDestroyKey(h); }
};

// Parse a PEM-encoded RSA private key file into a CryptoAPI key handle.
// Supports both PKCS#8 ("BEGIN PRIVATE KEY", openssl's modern default)
// and PKCS#1 ("BEGIN RSA PRIVATE KEY", legacy openssl format).  Tries
// PKCS#8 first then falls back to PKCS#1.
bool loadPemRsaPrivateKey(HCRYPTPROV hProv, const QString& pemPath,
                           HCRYPTKEY& hKeyOut)
{
    QFile f(pemPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "SecureBootCrypto/win: cannot open PEM" << pemPath;
        return false;
    }
    QByteArray pem = f.readAll();
    f.close();

    // PEM → DER: strip "-----BEGIN ... -----" header/footer and base64-decode
    // the body.  CRYPT_STRING_BASE64HEADER does both in one call.
    DWORD derLen = 0;
    if (!CryptStringToBinaryA(pem.constData(), pem.size(),
                                CRYPT_STRING_BASE64HEADER, nullptr, &derLen,
                                nullptr, nullptr)) {
        qDebug() << "SecureBootCrypto/win: PEM decode (size) failed, GLE="
                 << GetLastError();
        return false;
    }
    QByteArray der(static_cast<int>(derLen), Qt::Uninitialized);
    if (!CryptStringToBinaryA(pem.constData(), pem.size(),
                                CRYPT_STRING_BASE64HEADER,
                                reinterpret_cast<BYTE*>(der.data()), &derLen,
                                nullptr, nullptr)) {
        qDebug() << "SecureBootCrypto/win: PEM decode failed, GLE=" << GetLastError();
        return false;
    }

    auto tryImportRsaBlob = [&](const BYTE* keyBlob, DWORD keyBlobLen) {
        return CryptImportKey(hProv, keyBlob, keyBlobLen, 0,
                              CRYPT_EXPORTABLE, &hKeyOut) != 0;
    };

    // PKCS#8 path (PrivateKeyInfo wrapping the RSA key).
    {
        LocalFreePtr pkiPtr;
        DWORD pkiLen = 0;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 PKCS_PRIVATE_KEY_INFO,
                                 reinterpret_cast<const BYTE*>(der.constData()),
                                 der.size(), CRYPT_DECODE_ALLOC_FLAG,
                                 nullptr, &pkiPtr.p, &pkiLen)) {
            auto* pPKI = reinterpret_cast<PCRYPT_PRIVATE_KEY_INFO>(pkiPtr.p);
            LocalFreePtr rsaBlobPtr;
            DWORD rsaBlobLen = 0;
            if (CryptDecodeObjectEx(X509_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY,
                                     pPKI->PrivateKey.pbData,
                                     pPKI->PrivateKey.cbData,
                                     CRYPT_DECODE_ALLOC_FLAG, nullptr,
                                     &rsaBlobPtr.p, &rsaBlobLen)) {
                if (tryImportRsaBlob(rsaBlobPtr.p, rsaBlobLen))
                    return true;
            }
        }
    }

    // PKCS#1 path (bare RSAPrivateKey).
    {
        LocalFreePtr rsaBlobPtr;
        DWORD rsaBlobLen = 0;
        if (CryptDecodeObjectEx(X509_ASN_ENCODING, PKCS_RSA_PRIVATE_KEY,
                                 reinterpret_cast<const BYTE*>(der.constData()),
                                 der.size(), CRYPT_DECODE_ALLOC_FLAG, nullptr,
                                 &rsaBlobPtr.p, &rsaBlobLen)) {
            if (tryImportRsaBlob(rsaBlobPtr.p, rsaBlobLen))
                return true;
        }
    }

    qDebug() << "SecureBootCrypto/win: PEM is neither PKCS#8 nor PKCS#1 RSA private key";
    return false;
}

}  // namespace

namespace SecureBootCrypto {

QByteArray rsaSignSha256(const QByteArray& sha256Digest, const QString& rsaKeyPath)
{
    if (sha256Digest.size() != 32) {
        qDebug() << "SecureBootCrypto/win: expected 32-byte SHA-256 input, got"
                 << sha256Digest.size();
        return {};
    }

    // Stage 1: parse the PEM and import via CryptoAPI.  CryptoAPI's
    // CryptStringToBinary + CryptDecodeObjectEx + CryptImportKey handle
    // PKCS#1 and PKCS#8 PEM envelopes cleanly; CNG by itself doesn't.
    CryptProvHandle prov;
    if (!CryptAcquireContextW(&prov.h, nullptr, nullptr, PROV_RSA_AES,
                                CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        qDebug() << "SecureBootCrypto/win: CryptAcquireContext failed, GLE="
                 << GetLastError();
        return {};
    }
    CryptKeyHandle key;
    if (!loadPemRsaPrivateKey(prov.h, rsaKeyPath, key.h)) {
        return {};
    }

    // Stage 2: export the imported key as a CryptoAPI PRIVATEKEYBLOB and
    // hand it to CNG via LEGACY_RSAPRIVATE_BLOB (the official CryptoAPI↔CNG
    // bridge — same binary layout, no conversion needed).
    //
    // Why CNG?  CryptSignHash(AT_SIGNATURE) only works for keys living in a
    // named CSP container; ephemeral keys imported under CRYPT_VERIFYCONTEXT
    // fail with NTE_BAD_KEYSET (0x80090016).  BCryptSignHash operates on a
    // key handle directly, no container required, and emits big-endian
    // signatures so we don't need the byte-reverse the CryptoAPI path did.
    DWORD privBlobLen = 0;
    if (!CryptExportKey(key.h, 0, PRIVATEKEYBLOB, 0, nullptr, &privBlobLen)) {
        qDebug() << "SecureBootCrypto/win: CryptExportKey(PRIVATEKEYBLOB) size failed, GLE="
                 << GetLastError();
        return {};
    }
    QByteArray privBlob(static_cast<int>(privBlobLen), Qt::Uninitialized);
    if (!CryptExportKey(key.h, 0, PRIVATEKEYBLOB, 0,
                         reinterpret_cast<BYTE*>(privBlob.data()), &privBlobLen)) {
        qDebug() << "SecureBootCrypto/win: CryptExportKey(PRIVATEKEYBLOB) failed, GLE="
                 << GetLastError();
        return {};
    }

    BCryptAlgHandle alg;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_RSA_ALGORITHM,
                                                nullptr, 0);
    if (!BCRYPT_SUCCESS(st)) {
        qDebug() << "SecureBootCrypto/win: BCryptOpenAlgorithmProvider(RSA) failed, NTSTATUS=0x"
                 << QString::number(static_cast<quint32>(st), 16);
        return {};
    }

    BCryptKeyHandle bcryptKey;
    st = BCryptImportKeyPair(alg.h, nullptr, LEGACY_RSAPRIVATE_BLOB,
                              &bcryptKey.h,
                              reinterpret_cast<PUCHAR>(privBlob.data()),
                              privBlob.size(), 0);
    if (!BCRYPT_SUCCESS(st)) {
        qDebug() << "SecureBootCrypto/win: BCryptImportKeyPair(LEGACY_RSAPRIVATE_BLOB) failed, NTSTATUS=0x"
                 << QString::number(static_cast<quint32>(st), 16);
        return {};
    }

    // Stage 3: sign the raw SHA-256 hash with PKCS#1 v1.5 padding for
    // SHA-256.  BCrypt applies the DigestInfo wrap + padding internally —
    // same semantics as macOS's kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256.
    BCRYPT_PKCS1_PADDING_INFO padInfo{};
    padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    auto* digestPtr = reinterpret_cast<PUCHAR>(
        const_cast<char*>(sha256Digest.constData()));
    ULONG sigLen = 0;
    st = BCryptSignHash(bcryptKey.h, &padInfo, digestPtr, sha256Digest.size(),
                         nullptr, 0, &sigLen, BCRYPT_PAD_PKCS1);
    if (!BCRYPT_SUCCESS(st)) {
        qDebug() << "SecureBootCrypto/win: BCryptSignHash size query failed, NTSTATUS=0x"
                 << QString::number(static_cast<quint32>(st), 16);
        return {};
    }
    QByteArray sig(static_cast<int>(sigLen), Qt::Uninitialized);
    st = BCryptSignHash(bcryptKey.h, &padInfo, digestPtr, sha256Digest.size(),
                         reinterpret_cast<PUCHAR>(sig.data()), sig.size(),
                         &sigLen, BCRYPT_PAD_PKCS1);
    if (!BCRYPT_SUCCESS(st)) {
        qDebug() << "SecureBootCrypto/win: BCryptSignHash failed, NTSTATUS=0x"
                 << QString::number(static_cast<quint32>(st), 16);
        return {};
    }
    return sig.toHex();
}

QByteArray extractRsaPubkeyBin(const QString& rsaKeyPath)
{
    CryptProvHandle prov;
    if (!CryptAcquireContextW(&prov.h, nullptr, nullptr, PROV_RSA_AES,
                                CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        qDebug() << "SecureBootCrypto/win: CryptAcquireContext failed, GLE="
                 << GetLastError();
        return {};
    }
    CryptKeyHandle key;
    if (!loadPemRsaPrivateKey(prov.h, rsaKeyPath, key.h)) {
        return {};
    }
    DWORD blobLen = 0;
    if (!CryptExportKey(key.h, 0, PUBLICKEYBLOB, 0, nullptr, &blobLen)) {
        qDebug() << "SecureBootCrypto/win: CryptExportKey size failed, GLE="
                 << GetLastError();
        return {};
    }
    QByteArray blob(static_cast<int>(blobLen), Qt::Uninitialized);
    if (!CryptExportKey(key.h, 0, PUBLICKEYBLOB, 0,
                         reinterpret_cast<BYTE*>(blob.data()), &blobLen)) {
        qDebug() << "SecureBootCrypto/win: CryptExportKey failed, GLE="
                 << GetLastError();
        return {};
    }
    // PUBLICKEYBLOB layout (MSDN):
    //   [0..8)    BLOBHEADER (PUBLICKEYSTRUC)
    //   [8..12)   magic ("RSA1" = 0x31415352)
    //   [12..16)  bitlen
    //   [16..20)  pubexp (uint32_t, little-endian on disk)
    //   [20..)    modulus (bitlen/8 bytes, little-endian)
    // The boot-ROM format wants N little-endian then E little-endian (8 bytes
    // padded) — same byte order, so we can splice directly out of the blob.
    if (blob.size() < 20) {
        qDebug() << "SecureBootCrypto/win: PUBLICKEYBLOB too small,"
                 << blob.size() << "bytes";
        return {};
    }
    const auto* raw = reinterpret_cast<const uint8_t*>(blob.constData());
    const uint32_t bitlen =
          static_cast<uint32_t>(raw[12])
        | (static_cast<uint32_t>(raw[13]) << 8)
        | (static_cast<uint32_t>(raw[14]) << 16)
        | (static_cast<uint32_t>(raw[15]) << 24);
    if (bitlen != 2048) {
        qDebug() << "SecureBootCrypto/win: expected 2048-bit key, got"
                 << bitlen << "bits";
        return {};
    }
    if (blob.size() < int(20 + 256)) {
        qDebug() << "SecureBootCrypto/win: PUBLICKEYBLOB truncated,"
                 << blob.size() << "bytes (need" << (20 + 256) << ")";
        return {};
    }
    QByteArray result;
    result.reserve(264);
    result.append(reinterpret_cast<const char*>(raw + 20), 256);  // N (LE)
    result.append(reinterpret_cast<const char*>(raw + 16), 4);    // E (LE u32)
    result.append(4, char(0));                                     // pad to 8
    return result;
}

}  // namespace SecureBootCrypto
