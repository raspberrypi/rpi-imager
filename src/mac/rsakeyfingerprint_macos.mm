/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QString>
#include <QFile>
#include <QDebug>
#include <QByteArray>
#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>
#include <Foundation/Foundation.h>

// Helper function to parse ASN.1 DER format RSA public key
// Returns modulus (n) and exponent (e) from PKCS#1 RSAPublicKey structure
static bool parseRSAPublicKeyDER(const QByteArray &der, QByteArray &modulus, quint64 &exponent)
{
    // PKCS#1 RSAPublicKey ::= SEQUENCE {
    //     modulus           INTEGER,  -- n
    //     publicExponent    INTEGER   -- e
    // }
    
    const unsigned char *data = reinterpret_cast<const unsigned char*>(der.constData());
    int length = der.size();
    int pos = 0;
    
    // Parse SEQUENCE tag (0x30)
    if (pos >= length || data[pos] != 0x30) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid DER - expected SEQUENCE tag";
        return false;
    }
    pos++;
    
    // Parse SEQUENCE length
    int seqLen = 0;
    if (pos >= length) return false;
    if (data[pos] & 0x80) {
        int numBytes = data[pos] & 0x7F;
        pos++;
        for (int i = 0; i < numBytes && pos < length; i++) {
            seqLen = (seqLen << 8) | data[pos++];
        }
    } else {
        seqLen = data[pos++];
    }
    
    // Parse modulus INTEGER tag (0x02)
    if (pos >= length || data[pos] != 0x02) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid DER - expected INTEGER tag for modulus";
        return false;
    }
    pos++;
    
    // Parse modulus length
    int modLen = 0;
    if (pos >= length) return false;
    if (data[pos] & 0x80) {
        int numBytes = data[pos] & 0x7F;
        pos++;
        for (int i = 0; i < numBytes && pos < length; i++) {
            modLen = (modLen << 8) | data[pos++];
        }
    } else {
        modLen = data[pos++];
    }
    
    // Skip leading zero byte if present (for positive integers with high bit set)
    if (pos < length && data[pos] == 0x00) {
        pos++;
        modLen--;
    }
    
    // Extract modulus
    if (pos + modLen > length) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid DER - modulus length exceeds data";
        return false;
    }
    modulus = QByteArray(reinterpret_cast<const char*>(data + pos), modLen);
    pos += modLen;
    
    // Parse exponent INTEGER tag (0x02)
    if (pos >= length || data[pos] != 0x02) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid DER - expected INTEGER tag for exponent";
        return false;
    }
    pos++;
    
    // Parse exponent length
    int expLen = 0;
    if (pos >= length) return false;
    if (data[pos] & 0x80) {
        int numBytes = data[pos] & 0x7F;
        pos++;
        for (int i = 0; i < numBytes && pos < length; i++) {
            expLen = (expLen << 8) | data[pos++];
        }
    } else {
        expLen = data[pos++];
    }
    
    // Extract exponent (assume it fits in 64 bits)
    if (pos + expLen > length || expLen > 8) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid DER - exponent length invalid";
        return false;
    }
    
    exponent = 0;
    for (int i = 0; i < expLen; i++) {
        exponent = (exponent << 8) | data[pos++];
    }
    
    return true;
}

// Helper function to convert PEM to DER format
static QByteArray pemToDer(const QByteArray &pem)
{
    // PEM format is Base64 encoded DER data between header and footer
    // -----BEGIN ... KEY----- and -----END ... KEY-----
    
    // Find the base64 content between headers
    int startPos = pem.indexOf("-----BEGIN");
    if (startPos < 0) return QByteArray();
    
    startPos = pem.indexOf('\n', startPos);
    if (startPos < 0) return QByteArray();
    startPos++; // Skip newline
    
    int endPos = pem.indexOf("-----END", startPos);
    if (endPos < 0) return QByteArray();
    
    // Extract base64 content (remove whitespace)
    QByteArray base64Data = pem.mid(startPos, endPos - startPos);
    base64Data = base64Data.replace("\n", "").replace("\r", "").replace(" ", "");
    
    // Decode base64 to get DER
    return QByteArray::fromBase64(base64Data);
}

QString getRsaKeyFingerprint(const QString &keyPath)
{
    if (keyPath.isEmpty() || !QFile::exists(keyPath)) {
        return QString();
    }

    // Extract RSA key components (n, e) and format as Raspberry Pi bootloader binary format
    // Reference: https://raw.githubusercontent.com/raspberrypi/rpi-eeprom/refs/heads/master/tools/rpi-bootloader-key-convert
    // Format: 256 bytes modulus (n) little-endian + 8 bytes exponent (e) little-endian = 264 bytes total

    // Read PEM file
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qDebug() << "getRsaKeyFingerprint (macOS): failed to open key file";
        return QString();
    }
    QByteArray pemData = keyFile.readAll();
    keyFile.close();

    // Convert PEM to DER
    QByteArray derData = pemToDer(pemData);
    if (derData.isEmpty()) {
        qDebug() << "getRsaKeyFingerprint (macOS): failed to convert PEM to DER";
        return QString();
    }

    // Import private key using Security.framework
    NSDictionary *options = @{
        (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeRSA,
        (__bridge id)kSecAttrKeyClass: (__bridge id)kSecAttrKeyClassPrivate,
    };
    
    CFDataRef derDataRef = CFDataCreate(kCFAllocatorDefault,
                                         reinterpret_cast<const UInt8*>(derData.constData()),
                                         derData.size());
    if (!derDataRef) {
        qDebug() << "getRsaKeyFingerprint (macOS): failed to create CFData";
        return QString();
    }
    
    CFErrorRef error = nullptr;
    SecKeyRef privateKey = SecKeyCreateWithData(derDataRef, (__bridge CFDictionaryRef)options, &error);
    CFRelease(derDataRef);
    
    if (!privateKey) {
        if (error) {
            NSError *nsError = (__bridge NSError *)error;
            qDebug() << "getRsaKeyFingerprint (macOS): failed to import private key:" 
                     << QString::fromNSString([nsError localizedDescription]);
            CFRelease(error);
        }
        return QString();
    }
    
    // Extract public key from private key
    SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);
    CFRelease(privateKey);
    
    if (!publicKey) {
        qDebug() << "getRsaKeyFingerprint (macOS): failed to extract public key";
        return QString();
    }
    
    // Export public key in DER format
    CFDataRef publicKeyData = SecKeyCopyExternalRepresentation(publicKey, &error);
    CFRelease(publicKey);
    
    if (!publicKeyData) {
        if (error) {
            NSError *nsError = (__bridge NSError *)error;
            qDebug() << "getRsaKeyFingerprint (macOS): failed to export public key:" 
                     << QString::fromNSString([nsError localizedDescription]);
            CFRelease(error);
        }
        return QString();
    }
    
    // Convert to QByteArray and parse DER structure
    QByteArray publicKeyDer(reinterpret_cast<const char*>(CFDataGetBytePtr(publicKeyData)),
                            CFDataGetLength(publicKeyData));
    CFRelease(publicKeyData);
    
    QByteArray modulus;
    quint64 exponent;
    if (!parseRSAPublicKeyDER(publicKeyDer, modulus, exponent)) {
        qDebug() << "getRsaKeyFingerprint (macOS): failed to parse public key DER";
        return QString();
    }
    
    // Validate key size
    if (modulus.size() != 256) {
        qDebug() << "getRsaKeyFingerprint (macOS): invalid key size (must be RSA 2048)";
        qDebug() << "getRsaKeyFingerprint (macOS): actual size:" << modulus.size() << "bytes";
        return QString();
    }
    
    // Format as Raspberry Pi bootloader binary format (little-endian)
    QByteArray bootloaderFormat(264, 0);
    
    // Modulus (n) - 256 bytes little-endian (reverse the big-endian bytes from DER)
    for (int i = 0; i < 256; i++) {
        bootloaderFormat[i] = modulus[255 - i];
    }
    
    // Exponent (e) - 8 bytes little-endian
    for (int i = 0; i < 8; i++) {
        bootloaderFormat[256 + i] = (exponent >> (i * 8)) & 0xFF;
    }
    
    // Compute SHA-256 hash using CommonCrypto
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(reinterpret_cast<const unsigned char*>(bootloaderFormat.constData()), 264, hash);
    
    // Convert to hex string (show first 16 bytes = 32 hex chars)
    QString fingerprint;
    for (int i = 0; i < 16; i++) {
        fingerprint += QString("%1").arg(hash[i], 2, 16, QChar('0'));
        if (i == 7) fingerprint += ":"; // Add separator in middle
    }

    return fingerprint.toUpper();
}

