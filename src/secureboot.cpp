/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "secureboot.h"
#include "devicewrapperfatpartition.h"
#include "acceleratedcryptographichash.h"
#include "bootimgcreator.h"

#include <QFile>
#include <QDebug>
#include <QDateTime>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QSet>
#include <ctime>

#ifdef __APPLE__
#include <Security/Security.h>
#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonCrypto.h>
#endif

#if defined(__linux__) || defined(_WIN32)
// On Linux use GnuTLS, on Windows use Schannel
// We'll use OpenSSL if available or external tools as fallback
#include <QCryptographicHash>
#endif

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
#ifdef __APPLE__
    // On macOS, use Security framework (modern API with SecKeyCreateSignature)
    // Read the PEM key file
    QFile keyFile(rsaKeyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qDebug() << "SecureBoot::rsaSign: failed to open key file" << rsaKeyPath;
        return QByteArray();
    }
    QByteArray pemData = keyFile.readAll();
    keyFile.close();

    // Import the key using Security framework
    CFDataRef keyData = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(pemData.constData()), pemData.size());
    if (!keyData) {
        qDebug() << "SecureBoot::rsaSign: failed to create CFData from key";
        return QByteArray();
    }

    // Set import parameters
    SecExternalFormat format = kSecFormatPEMSequence;
    SecExternalItemType itemType = kSecItemTypePrivateKey;
    SecItemImportExportKeyParameters params;
    memset(&params, 0, sizeof(params));
    params.version = SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION;
    params.passphrase = nullptr; // Assuming no passphrase
    
    CFArrayRef items = nullptr;
    OSStatus status = SecItemImport(keyData, nullptr, &format, &itemType, 0, &params, nullptr, &items);
    CFRelease(keyData);
    
    if (status != errSecSuccess || !items || CFArrayGetCount(items) == 0) {
        qDebug() << "SecureBoot::rsaSign: failed to import private key, status:" << status;
        if (items) CFRelease(items);
        return QByteArray();
    }

    SecKeyRef privateKey = (SecKeyRef)CFArrayGetValueAtIndex(items, 0);
    CFRetain(privateKey); // Retain before releasing the array
    CFRelease(items);

    // Create CFData from the data to sign
    CFDataRef dataToSign = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(data.constData()), data.size());
    if (!dataToSign) {
        qDebug() << "SecureBoot::rsaSign: failed to create CFData from data";
        CFRelease(privateKey);
        return QByteArray();
    }

    // Sign the digest using SecKeyCreateSignature (modern API)
    // Use kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256 since we're passing a pre-computed SHA-256 digest
    // (The "Digest" variant expects pre-hashed 32-byte data)
    CFErrorRef error = nullptr;
    CFDataRef signature = SecKeyCreateSignature(privateKey,
                                                kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256,
                                                dataToSign,
                                                &error);
    
    CFRelease(dataToSign);
    CFRelease(privateKey);
    
    if (!signature) {
        if (error) {
            CFStringRef errorDesc = CFErrorCopyDescription(error);
            qDebug() << "SecureBoot::rsaSign: failed to sign data:" 
                     << QString::fromCFString(errorDesc);
            CFRelease(errorDesc);
            CFRelease(error);
        }
        return QByteArray();
    }

    // Convert CFDataRef to QByteArray
    const UInt8* bytes = CFDataGetBytePtr(signature);
    CFIndex length = CFDataGetLength(signature);
    QByteArray result(reinterpret_cast<const char*>(bytes), length);
    CFRelease(signature);
    
    return result.toHex();
#else
    // On Linux/Windows, use openssl rsautl to sign the pre-computed digest
    // We need to wrap the 32-byte SHA-256 digest in PKCS#1 DigestInfo DER structure
    QByteArray derWrapped;
    // DigestInfo for SHA-256: SEQUENCE { AlgorithmIdentifier, OCTET STRING }
    // 30 31 = SEQUENCE, length 49 bytes
    // 30 0d = SEQUENCE, length 13 bytes (AlgorithmIdentifier)
    // 06 09 = OID, length 9 bytes
    // 60 86 48 01 65 03 04 02 01 = SHA-256 OID (2.16.840.1.101.3.4.2.1)
    // 05 00 = NULL
    // 04 20 = OCTET STRING, length 32 bytes
    derWrapped.append("\x30\x31\x30\x0d\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01\x05\x00\x04\x20", 19);
    derWrapped.append(data); // Append the 32-byte SHA-256 digest
    
    if (derWrapped.size() != 51) {
        qDebug() << "SecureBoot::rsaSign: invalid DER-wrapped digest size:" << derWrapped.size();
        return QByteArray();
    }
    
    QProcess opensslProc;
    opensslProc.start("openssl", QStringList() 
        << "rsautl" << "-sign" << "-inkey" << rsaKeyPath << "-pkcs");
    
    if (!opensslProc.waitForStarted()) {
        qDebug() << "SecureBoot::rsaSign: failed to start openssl";
        return QByteArray();
    }

    opensslProc.write(derWrapped);
    opensslProc.closeWriteChannel();

    if (!opensslProc.waitForFinished(30000)) {
        qDebug() << "SecureBoot::rsaSign: openssl timeout";
        return QByteArray();
    }

    if (opensslProc.exitCode() != 0) {
        qDebug() << "SecureBoot::rsaSign: openssl failed:" << opensslProc.readAllStandardError();
        return QByteArray();
    }

    QByteArray signature = opensslProc.readAllStandardOutput();
    // Return hex-encoded signature
    return signature.toHex();
#endif
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

