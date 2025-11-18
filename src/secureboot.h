#ifndef SECUREBOOT_H
#define SECUREBOOT_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QByteArray>
#include <QString>
#include <QMap>

class DeviceWrapperFatPartition;

class SecureBoot
{
public:
    SecureBoot();
    ~SecureBoot();

    /**
     * Extract all files from a FAT32 partition into a memory map
     * @param fat The FAT partition to extract from
     * @return Map of filename -> file contents
     */
    static QMap<QString, QByteArray> extractFatPartitionFiles(DeviceWrapperFatPartition *fat);

    /**
     * Create a FAT32 boot.img file from a map of files
     * @param files Map of filename -> file contents
     * @param outputPath Path where boot.img should be created
     * @return true on success, false on error
     */
    static bool createBootImg(const QMap<QString, QByteArray> &files, const QString &outputPath);

    /**
     * Generate boot.sig signature file for a boot.img
     * @param bootImgPath Path to the boot.img file
     * @param rsaKeyPath Path to RSA 2048-bit private key (PEM format)
     * @param bootSigPath Path where boot.sig should be created
     * @return true on success, false on error
     */
    static bool generateBootSig(const QString &bootImgPath, const QString &rsaKeyPath, const QString &bootSigPath);

    /**
     * Generate SHA-256 hash of a file
     * @param filePath Path to the file
     * @return Hex-encoded SHA-256 hash, or empty on error
     */
    static QByteArray sha256File(const QString &filePath);

    /**
     * Sign data with RSA PKCS#1 v1.5 using a private key
     * @param data Data to sign
     * @param rsaKeyPath Path to RSA private key (PEM format)
     * @return Hex-encoded signature, or empty on error
     */
    static QByteArray rsaSign(const QByteArray &data, const QString &rsaKeyPath);

    /**
     * Get current Unix timestamp
     * @return Current timestamp as seconds since epoch
     */
    static qint64 getCurrentTimestamp();
};

#endif // SECUREBOOT_H


