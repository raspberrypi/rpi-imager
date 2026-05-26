/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "secureboot.h"
#include "secureboot_crypto.h"
#include "devicewrapperfatpartition.h"
#include "acceleratedcryptographichash.h"
#include "bootimgcreator.h"

#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QSet>
#include <QCryptographicHash>
#include <ctime>

SecureBoot::SecureBoot()
{
}

SecureBoot::~SecureBoot()
{
}

QMap<QString, QByteArray> SecureBoot::extractFatPartitionFiles(DeviceWrapperFatPartition *fat)
{
    QMap<QString, QByteArray> files;
    
    if (!fat) {
        qDebug() << "SecureBoot::extractFatPartitionFiles: null FAT partition";
        return files;
    }

    // Get list of ALL files recursively (including subdirectories)
    QStringList allFiles = fat->listAllFilesRecursive();
    qDebug() << "SecureBoot: found" << allFiles.size() << "files (including subdirectories)";
    
    // Extract all files
    for (const QString &filename : allFiles) {
        try {
            QByteArray content = fat->readFile(filename);
            if (!content.isEmpty()) {
                files[filename] = content;
                qDebug() << "SecureBoot: extracted" << filename << "(" << content.size() << "bytes)";
            } else {
                // Check if file is actually zero-length or if there's a read error
                if (fat->fileExists(filename)) {
                    qDebug() << "SecureBoot: WARNING - file" << filename << "is zero-length, skipping";
                } else {
                    qDebug() << "SecureBoot: WARNING - file" << filename << "could not be read";
                }
            }
        } catch (const std::exception &e) {
            qDebug() << "SecureBoot: ERROR reading" << filename << ":" << e.what();
        }
    }

    qDebug() << "SecureBoot: extracted" << files.size() << "total files from FAT partition";
    
    // Calculate total size
    qint64 totalSize = 0;
    for (const QByteArray &content : files) {
        totalSize += content.size();
    }
    qDebug() << "SecureBoot: total extracted size:" << totalSize << "bytes (" 
             << (totalSize / 1024 / 1024) << "MB)";
    
    return files;
}

bool SecureBoot::createBootImg(const QMap<QString, QByteArray> &files, const QString &outputPath)
{
    if (files.isEmpty()) {
        qDebug() << "SecureBoot::createBootImg: no files to pack";
        return false;
    }

    // Calculate required size for FAT32 image (with overhead)
    qint64 totalSize = 0;
    for (const QByteArray &content : files) {
        totalSize += content.size();
    }
    // Add 50% overhead for FAT32 structures and round up to nearest MB
    qint64 imgSize = ((totalSize * 3 / 2) / (1024 * 1024) + 1) * 1024 * 1024;
    
    // FAT32 has a minimum size requirement of 33MB
    const qint64 FAT32_MIN_SIZE = 33 * 1024 * 1024; // 33MB minimum for FAT32
    if (imgSize < FAT32_MIN_SIZE) {
        imgSize = FAT32_MIN_SIZE;
    }

    qDebug() << "SecureBoot: creating boot.img of size" << imgSize << "bytes for" << files.size() << "files";
    
    // Delegate to platform-specific helper
    return BootImgCreator::createBootImg(files, outputPath, imgSize);
}

QByteArray SecureBoot::sha256File(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "SecureBoot::sha256File: failed to open" << filePath;
        return QByteArray();
    }

    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    
    const qint64 bufferSize = 1024 * 1024; // 1MB buffer
    while (!file.atEnd()) {
        QByteArray chunk = file.read(bufferSize);
        hash.addData(chunk);
    }
    file.close();

    return hash.result().toHex();
}

QByteArray SecureBoot::rsaSign(const QByteArray &data, const QString &rsaKeyPath)
{
    // Delegate to the PAL — each platform under src/{linux,mac,windows}/
    // ships its own implementation (Security framework, openssl subprocess,
    // CryptoAPI respectively).  See src/secureboot_crypto.h.
    return SecureBootCrypto::rsaSignSha256(data, rsaKeyPath);
}

bool SecureBoot::generateBootSig(const QString &bootImgPath, const QString &rsaKeyPath, const QString &bootSigPath)
{
    qDebug() << "SecureBoot: generating boot.sig for" << bootImgPath;

    // Calculate SHA-256 hash of boot.img (returns hex-encoded string)
    QByteArray hashHex = sha256File(bootImgPath);
    if (hashHex.isEmpty()) {
        qDebug() << "SecureBoot::generateBootSig: failed to hash boot.img";
        return false;
    }

    qDebug() << "SecureBoot: boot.img SHA-256:" << hashHex;

    // Convert hex hash to raw bytes for signing
    QByteArray hashBytes = QByteArray::fromHex(hashHex);
    if (hashBytes.size() != 32) {
        qDebug() << "SecureBoot::generateBootSig: invalid hash size:" << hashBytes.size();
        return false;
    }

    // Get current timestamp
    qint64 timestamp = getCurrentTimestamp();
    qDebug() << "SecureBoot: timestamp:" << timestamp;

    // Sign the raw hash bytes with RSA key
    // The signature is computed over the raw 32-byte SHA-256 hash
    QByteArray signature = rsaSign(hashBytes, rsaKeyPath);
    if (signature.isEmpty()) {
        qDebug() << "SecureBoot::generateBootSig: failed to sign boot.img";
        return false;
    }

    qDebug() << "SecureBoot: RSA signature:" << signature;

    // Create boot.sig file
    QFile sigFile(bootSigPath);
    if (!sigFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "SecureBoot::generateBootSig: failed to create" << bootSigPath;
        return false;
    }

    // Write boot.sig in the format expected by Raspberry Pi secure boot:
    // SHA-256 hash (hex)
    // ts: timestamp
    // rsa2048: signature (hex)
    sigFile.write(hashHex);
    sigFile.write("\n");
    sigFile.write("ts: ");
    sigFile.write(QByteArray::number(timestamp));
    sigFile.write("\n");
    sigFile.write("rsa2048: ");
    sigFile.write(signature);
    sigFile.write("\n");
    sigFile.close();

    qDebug() << "SecureBoot: boot.sig created successfully at" << bootSigPath;
    return true;
}

qint64 SecureBoot::getCurrentTimestamp()
{
    return QDateTime::currentSecsSinceEpoch();
}

QByteArray SecureBoot::generateConfigSig(const QByteArray &configText, const QString &rsaKeyPath)
{
    // SHA-256 over the raw config text (matches rpi-eeprom-digest behaviour).
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(configText);
    QByteArray digest = hash.result();
    if (digest.size() != 32) {
        qDebug() << "SecureBoot::generateConfigSig: SHA-256 produced" << digest.size() << "bytes";
        return {};
    }

    // RSA sign the raw 32-byte digest (rsaSign internally wraps it in DigestInfo).
    QByteArray sigHex = rsaSign(digest, rsaKeyPath);
    if (sigHex.isEmpty()) {
        qDebug() << "SecureBoot::generateConfigSig: rsaSign failed";
        return {};
    }

    QByteArray sig;
    sig.append(digest.toHex());
    sig.append('\n');
    sig.append("ts: ");
    sig.append(QByteArray::number(getCurrentTimestamp()));
    sig.append('\n');
    sig.append("rsa2048: ");
    sig.append(sigHex);
    sig.append('\n');
    return sig;
}

// Parse one ASN.1 length field at *off, advance *off past it, return length
// or -1 on error.  Supports short form and long-form ≤ 4 bytes (covers all
// realistic RSA-2048 keys).
static int asn1ParseLength(const uint8_t *d, int len, int *off)
{
    if (*off >= len) return -1;
    uint8_t b = d[(*off)++];
    if (b < 0x80) return b;
    int n = b & 0x7f;
    if (n == 0 || n > 4 || *off + n > len) return -1;
    int result = 0;
    for (int i = 0; i < n; ++i)
        result = (result << 8) | d[(*off)++];
    return result;
}

QByteArray SecureBoot::extractRsaPubkeyBin(const QString &rsaKeyPath)
{
    // PAL implementation per platform — see src/secureboot_crypto.h.
    return SecureBootCrypto::extractRsaPubkeyBin(rsaKeyPath);
}

// Pure ASN.1 parsing of a SubjectPublicKeyInfo DER blob into the boot-ROM
// 264-byte format.  Called by the mac/linux PAL implementations of
// SecureBootCrypto::extractRsaPubkeyBin after they fetch the DER via
// openssl.  Windows skips this entirely (CryptoAPI gives us the bytes in
// the right order already).
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

QByteArray SecureBoot::signBootcode2712(const QByteArray &bootcode,
                                          const QString &rsaKeyPath,
                                          int keynum, int version)
{
    if (bootcode.isEmpty()) {
        qDebug() << "SecureBoot::signBootcode2712: empty bootcode";
        return {};
    }
    if ((keynum < 0 || keynum > 4) && keynum != 16) {
        qDebug() << "SecureBoot::signBootcode2712: bad keynum" << keynum;
        return {};
    }
    if (version < 0 || version > 32) {
        qDebug() << "SecureBoot::signBootcode2712: bad version" << version;
        return {};
    }

    auto appendU32LE = [](QByteArray &out, uint32_t v) {
        out.append(char(v & 0xff));
        out.append(char((v >> 8) & 0xff));
        out.append(char((v >> 16) & 0xff));
        out.append(char((v >> 24) & 0xff));
    };

    QByteArray result;
    result.reserve(bootcode.size() + 4 + 4 + 4 + 256 + 264);
    result.append(bootcode);
    appendU32LE(result, uint32_t(bootcode.size()));
    appendU32LE(result, uint32_t(keynum));
    appendU32LE(result, uint32_t(version));

    // SHA-256 over [bootcode | length | keynum | version], then RSA-sign.
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(result);
    QByteArray digest = hash.result();
    if (digest.size() != 32) {
        qDebug() << "SecureBoot::signBootcode2712: bad digest size" << digest.size();
        return {};
    }

    QByteArray sigHex = rsaSign(digest, rsaKeyPath);
    QByteArray sig = QByteArray::fromHex(sigHex);
    if (sig.size() != 256) {
        qDebug() << "SecureBoot::signBootcode2712: bad signature size" << sig.size();
        return {};
    }
    result.append(sig);

    QByteArray pubkey = extractRsaPubkeyBin(rsaKeyPath);
    if (pubkey.size() != 264) {
        qDebug() << "SecureBoot::signBootcode2712: bad pubkey.bin size" << pubkey.size();
        return {};
    }
    result.append(pubkey);

    return result;
}

