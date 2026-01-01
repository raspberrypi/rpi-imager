/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QString>
#include <QFile>
#include <QDebug>
#include <QByteArray>
#include <QProcess>
#include <QRegularExpression>
#include <QCryptographicHash>

QString getRsaKeyFingerprint(const QString &keyPath)
{
    if (keyPath.isEmpty() || !QFile::exists(keyPath)) {
        return QString();
    }

    // Extract RSA key components (n, e) and format as Raspberry Pi bootloader binary format
    // Reference: https://raw.githubusercontent.com/raspberrypi/rpi-eeprom/refs/heads/master/tools/rpi-bootloader-key-convert
    // Format: 256 bytes modulus (n) little-endian + 8 bytes exponent (e) little-endian = 264 bytes total

    // Use openssl command-line tool to extract modulus (n) and exponent (e)
    QProcess process;
    process.start("openssl", QStringList() 
                  << "rsa" << "-in" << keyPath 
                  << "-text" << "-noout");
    
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        qDebug() << "getRsaKeyFingerprint (Windows): openssl failed:" 
                 << process.readAllStandardError();
        return QString();
    }

    QString textOutput = QString::fromUtf8(process.readAllStandardOutput());
    
    // Parse modulus and exponent from text output
    // Note: modulus spans multiple lines in openssl output
    QRegularExpression modulusRe("modulus:[\\s\\n]*([0-9a-fA-F:\\s\\n]+?)(?:publicExponent|privateExponent)", 
                                  QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression exponentRe("publicExponent:\\s*(\\d+)");
    
    QRegularExpressionMatch modulusMatch = modulusRe.match(textOutput);
    QRegularExpressionMatch exponentMatch = exponentRe.match(textOutput);
    
    if (!modulusMatch.hasMatch() || !exponentMatch.hasMatch()) {
        qDebug() << "getRsaKeyFingerprint (Windows): failed to parse key components";
        return QString();
    }
    
    // Extract modulus (remove colons, newlines, and spaces, then convert from hex)
    QString modulusHex = modulusMatch.captured(1).remove(':').remove(QRegularExpression("\\s"));
    QByteArray modulusBytes = QByteArray::fromHex(modulusHex.toLatin1());
    
    // Extract exponent
    quint64 exponent = exponentMatch.captured(1).toULongLong();
    
    if (modulusBytes.size() != 256) {
        qDebug() << "getRsaKeyFingerprint (Windows): invalid key size (must be RSA 2048)";
        return QString();
    }
    
    // Format as Raspberry Pi bootloader binary format (little-endian)
    QByteArray bootloaderFormat(264, 0);
    
    // Modulus (n) - 256 bytes little-endian (reverse the big-endian bytes)
    for (int i = 0; i < 256; i++) {
        bootloaderFormat[i] = modulusBytes[255 - i];
    }
    
    // Exponent (e) - 8 bytes little-endian
    for (int i = 0; i < 8; i++) {
        bootloaderFormat[256 + i] = (exponent >> (i * 8)) & 0xFF;
    }
    
    // Compute SHA-256 hash
    QByteArray hash = QCryptographicHash::hash(bootloaderFormat, QCryptographicHash::Sha256);
    
    // Convert to hex string (show first 16 bytes = 32 hex chars)
    QString fingerprint;
    for (int i = 0; i < 16 && i < hash.size(); i++) {
        fingerprint += QString("%1").arg(static_cast<unsigned char>(hash[i]), 2, 16, QChar('0'));
        if (i == 7) fingerprint += ":"; // Add separator in middle
    }

    return fingerprint.toUpper();
}

