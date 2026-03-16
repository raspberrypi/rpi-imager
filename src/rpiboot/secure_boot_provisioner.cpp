/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "secure_boot_provisioner.h"
#include "rpiboot_protocol.h"
#include "../secureboot.h"
#include "../acceleratedcryptographichash.h"

#include <QFile>
#include <QProcess>
#include <QDebug>

#include <cstring>

namespace rpiboot {

bool SecureBootProvisioner::generateKeyPair(const std::filesystem::path& privateKeyPath,
                                              const std::filesystem::path& publicKeyPath)
{
    QString privPath = QString::fromStdString(privateKeyPath.string());
    QString pubPath = QString::fromStdString(publicKeyPath.string());

    // Step 1: Generate RSA-2048 private key
    //   openssl genrsa -out <private.pem> 2048
    {
        QProcess proc;
        proc.start("openssl", {"genrsa", "-out", privPath, "2048"});
        if (!proc.waitForStarted(5000)) {
            qDebug() << "SecureBootProvisioner: failed to start openssl for key generation";
            return false;
        }
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            qDebug() << "SecureBootProvisioner: openssl genrsa failed:"
                     << proc.readAllStandardError();
            return false;
        }
    }

    // Step 2: Extract public key from private key
    //   openssl rsa -in <private.pem> -outform PEM -pubout -out <public.pem>
    {
        QProcess proc;
        proc.start("openssl", {"rsa", "-in", privPath, "-outform", "PEM",
                                "-pubout", "-out", pubPath});
        if (!proc.waitForStarted(5000)) {
            qDebug() << "SecureBootProvisioner: failed to start openssl for public key extraction";
            // Clean up the private key we just created
            QFile::remove(privPath);
            return false;
        }
        if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
            qDebug() << "SecureBootProvisioner: openssl rsa failed:"
                     << proc.readAllStandardError();
            QFile::remove(privPath);
            return false;
        }
    }

    // Verify both files were created
    if (!std::filesystem::exists(privateKeyPath) || !std::filesystem::exists(publicKeyPath)) {
        qDebug() << "SecureBootProvisioner: key files not created";
        return false;
    }

    qDebug() << "SecureBootProvisioner: generated RSA-2048 key pair at"
             << privPath << "and" << pubPath;
    return true;
}

std::optional<std::array<uint8_t, 32>> SecureBootProvisioner::calculateOtpKeyHash(
    const std::filesystem::path& publicKeyPath)
{
    // The OTP key hash is SHA-256 of the DER-encoded public key (SubjectPublicKeyInfo).
    // Extract the raw public key in DER format using openssl:
    //   openssl rsa -pubin -in <public.pem> -outform DER
    QString pubPath = QString::fromStdString(publicKeyPath.string());

    QProcess proc;
    proc.start("openssl", {"rsa", "-pubin", "-in", pubPath, "-outform", "DER"});
    if (!proc.waitForStarted(5000)) {
        qDebug() << "SecureBootProvisioner: failed to start openssl for public key DER extraction";
        return std::nullopt;
    }
    proc.closeWriteChannel();
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        qDebug() << "SecureBootProvisioner: openssl DER extraction failed:"
                 << proc.readAllStandardError();
        return std::nullopt;
    }

    QByteArray derData = proc.readAllStandardOutput();
    if (derData.isEmpty()) {
        qDebug() << "SecureBootProvisioner: empty DER output from openssl";
        return std::nullopt;
    }

    // Hash the full DER-encoded public key with SHA-256
    AcceleratedCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(derData);
    QByteArray hashResult = hash.result();

    if (hashResult.size() != 32) {
        qDebug() << "SecureBootProvisioner: unexpected hash size:" << hashResult.size();
        return std::nullopt;
    }

    std::array<uint8_t, 32> result;
    std::memcpy(result.data(), hashResult.constData(), 32);

    qDebug() << "SecureBootProvisioner: OTP key hash:" << hashResult.toHex();
    return result;
}

// The Pi EEPROM image has a fixed structure: a 4KB config block starting at
// a known offset, followed by the firmware code.  The config section is
// padded with 0xFF.  The last 4KB block contains the SHA-256 digest.
//
// boot.conf is stored as ASCII text inside the EEPROM config block.
// rpi-eeprom-config writes config at the EEPROM's config offset and pads
// the remainder of the 4KB block with 0xFF.
//
// For secure boot recovery, we need to:
// 1. Read the base EEPROM image (pieeprom.original.bin)
// 2. Prepare a boot.conf with the required settings
// 3. Embed boot.conf into the EEPROM image
// 4. Sign the image and produce pieeprom.sig

static constexpr size_t EEPROM_CONFIG_SIZE = 4096;

// Find the config section in a pieeprom image.
// The config section is identified by being a 4KB block of mostly ASCII text.
// For CM4 (BCM2711), the config typically starts at offset 0 of the second
// 4KB block.  Rather than hardcoding, we look for the "[all]" section marker
// or known config keys.
static std::pair<size_t, size_t> findConfigSection(const QByteArray& eeprom)
{
    // The config is in the first 4KB-aligned block that contains "[all]"
    // or common EEPROM config keys.  Fall back to a known default offset.
    for (size_t off = 0; off + EEPROM_CONFIG_SIZE <= static_cast<size_t>(eeprom.size());
         off += EEPROM_CONFIG_SIZE) {
        QByteArray block = eeprom.mid(static_cast<qsizetype>(off),
                                       static_cast<qsizetype>(EEPROM_CONFIG_SIZE));
        if (block.contains("[all]") || block.contains("BOOT_UART") ||
            block.contains("SIGNED_BOOT")) {
            return {off, EEPROM_CONFIG_SIZE};
        }
    }
    // Default: second 4KB block (offset 4096) for BCM2711
    return {4096, EEPROM_CONFIG_SIZE};
}

bool SecureBootProvisioner::prepareRecoveryFirmware(ChipGeneration gen,
                                                      const std::filesystem::path& baseFirmwareDir,
                                                      const std::filesystem::path& privateKeyPath,
                                                      const std::filesystem::path& outputDir,
                                                      bool lockJtag)
{
    // Select the correct recovery directory
    std::string recoverySubdir = (gen == ChipGeneration::BCM2712)
        ? "secure-boot-recovery5" : "secure-boot-recovery";

    auto srcDir = baseFirmwareDir / recoverySubdir;
    if (!std::filesystem::exists(srcDir)) {
        _lastError = "Recovery firmware not found: " + srcDir.string();
        return false;
    }

    // Create output directory
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        _lastError = "Cannot create output directory: " + ec.message();
        return false;
    }

    // Copy base recovery firmware
    std::filesystem::copy(srcDir, outputDir,
                           std::filesystem::copy_options::recursive |
                           std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        _lastError = "Failed to copy recovery firmware: " + ec.message();
        return false;
    }

    // 1. Read the base EEPROM image
    auto eepromOrigPath = outputDir / "pieeprom.original.bin";
    auto eepromOutPath = outputDir / "pieeprom.bin";
    auto eepromSigPath = outputDir / "pieeprom.sig";

    // Fall back to pieeprom.bin if .original.bin doesn't exist
    if (!std::filesystem::exists(eepromOrigPath)) {
        eepromOrigPath = outputDir / "pieeprom.bin";
    }

    QFile eepromFile(QString::fromStdString(eepromOrigPath.string()));
    if (!eepromFile.open(QIODevice::ReadOnly)) {
        _lastError = "Cannot read EEPROM image: " + eepromOrigPath.string();
        return false;
    }
    QByteArray eepromData = eepromFile.readAll();
    eepromFile.close();

    if (eepromData.isEmpty()) {
        _lastError = "EEPROM image is empty: " + eepromOrigPath.string();
        return false;
    }

    // 2. Build the boot.conf content
    QByteArray bootConf;
    bootConf.append("[all]\n");
    bootConf.append("SIGNED_BOOT=1\n");
    bootConf.append("program_pubkey=1\n");
    if (lockJtag) {
        bootConf.append("program_jtag_lock=1\n");
    }

    // 3. Embed boot.conf into the EEPROM image
    auto [configOffset, configSize] = findConfigSection(eepromData);

    if (static_cast<size_t>(bootConf.size()) > configSize) {
        _lastError = "boot.conf too large for EEPROM config section";
        return false;
    }

    // Clear the config section and write new config
    std::memset(eepromData.data() + configOffset, 0xFF, configSize);
    std::memcpy(eepromData.data() + configOffset, bootConf.constData(),
                static_cast<size_t>(bootConf.size()));

    // 4. Write the modified EEPROM image
    QFile eepromOut(QString::fromStdString(eepromOutPath.string()));
    if (!eepromOut.open(QIODevice::WriteOnly)) {
        _lastError = "Cannot write EEPROM image: " + eepromOutPath.string();
        return false;
    }
    eepromOut.write(eepromData);
    eepromOut.close();

    // 5. Sign the EEPROM image — produce pieeprom.sig
    //    SHA-256 hash of the EEPROM binary, then RSA sign the hash
    QString eepromOutQStr = QString::fromStdString(eepromOutPath.string());
    QString keyQStr = QString::fromStdString(privateKeyPath.string());
    QString sigQStr = QString::fromStdString(eepromSigPath.string());

    QByteArray hashHex = SecureBoot::sha256File(eepromOutQStr);
    if (hashHex.isEmpty()) {
        _lastError = "Failed to hash EEPROM image";
        return false;
    }

    QByteArray hashBytes = QByteArray::fromHex(hashHex);
    QByteArray signature = SecureBoot::rsaSign(hashBytes, keyQStr);
    if (signature.isEmpty()) {
        _lastError = "Failed to sign EEPROM image";
        return false;
    }

    // Write signature file in the same format as rpi-eeprom-digest
    QFile sigFile(sigQStr);
    if (!sigFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        _lastError = "Cannot create EEPROM signature file: " + eepromSigPath.string();
        return false;
    }
    sigFile.write(hashHex);
    sigFile.write("\n");
    sigFile.write("ts: ");
    sigFile.write(QByteArray::number(SecureBoot::getCurrentTimestamp()));
    sigFile.write("\n");
    sigFile.write("rsa2048: ");
    sigFile.write(signature);
    sigFile.write("\n");
    sigFile.close();

    // 6. Copy the public key into the recovery directory (the bootloader needs it)
    auto pubKeyDest = outputDir / "pubkey.pem";
    // Extract public key from private key
    {
        QProcess proc;
        proc.start("openssl", {"rsa", "-in", QString::fromStdString(privateKeyPath.string()),
                                "-outform", "PEM", "-pubout", "-out",
                                QString::fromStdString(pubKeyDest.string())});
        if (!proc.waitForStarted(5000) || !proc.waitForFinished(30000) || proc.exitCode() != 0) {
            qDebug() << "SecureBootProvisioner: warning: could not extract public key to recovery dir";
            // Non-fatal — some recovery firmware versions don't require it in-tree
        }
    }

    qDebug() << "SecureBootProvisioner: recovery firmware prepared in"
             << QString::fromStdString(outputDir.string());
    return true;
}

bool SecureBootProvisioner::provision(IUsbTransport& transport,
                                       ChipGeneration gen,
                                       const std::filesystem::path& recoveryDir,
                                       ProgressCallback progress,
                                       std::atomic<bool>& cancelled)
{
    RpibootProtocol protocol;
    bool ok = protocol.execute(transport, gen, SideloadMode::SecureBootRecovery,
                                recoveryDir, progress, cancelled);
    if (!ok) {
        _lastError = "Provisioning failed: " + protocol.lastError();
        return false;
    }

    return true;
}

bool SecureBootProvisioner::signBootImage(const std::filesystem::path& bootImg,
                                            const std::filesystem::path& privateKeyPath,
                                            const std::filesystem::path& outputSig)
{
    QString imgPath = QString::fromStdString(bootImg.string());
    QString keyPath = QString::fromStdString(privateKeyPath.string());
    QString sigPath = QString::fromStdString(outputSig.string());

    return SecureBoot::generateBootSig(imgPath, keyPath, sigPath);
}

} // namespace rpiboot
