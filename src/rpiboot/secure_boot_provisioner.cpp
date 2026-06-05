/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "secure_boot_provisioner.h"
#include "rpiboot_protocol.h"
#include "bootloader_image.h"
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

// Default bootconf.txt for re-provisioning.  Mirrors
// usbboot/secure-boot-recovery5/boot.conf with SIGNED_BOOT=1 added so the
// bootloader will load only signed boot.img on subsequent boots.
static QByteArray defaultBootConf2712()
{
    QByteArray b;
    b.append("[all]\n");
    b.append("BOOT_UART=1\n");
    b.append("POWER_OFF_ON_HALT=1\n");
    b.append("BOOT_ORDER=0xf2461\n");
    b.append("SIGNED_BOOT=1\n");
    b.append("ENABLE_SELF_UPDATE=0\n");
    return b;
}

static QByteArray defaultBootConf2711()
{
    QByteArray b;
    b.append("[all]\n");
    b.append("BOOT_UART=1\n");
    b.append("WAKE_ON_GPIO=1\n");
    b.append("POWER_OFF_ON_HALT=0\n");
    b.append("HDMI_DELAY=0\n");
    b.append("SIGNED_BOOT=1\n");
    b.append("ENABLE_SELF_UPDATE=0\n");
    return b;
}

// Generate the pieeprom.sig file alongside a signed pieeprom.bin.  Format
// matches rpi-eeprom-digest run *with* -k:
//   <sha256-hex>\n
//   ts: <epoch>\n
//   rsa2048: <signature-hex>\n
// The RSA signature is PKCS#1 v1.5 / SHA-256 over the whole pieeprom.bin,
// signed with the customer key.  This is REQUIRED for secure boot: the
// recovery image validates pieeprom.sig's rsa2048 line against the fused
// customer key before committing the EEPROM.  Omitting it (sha256 + ts
// only) makes a fused device reject the update — observed as the bootloader
// "invalid signature" flash sequence during the rpiboot phase, before any
// reboot.  Mirrors rpi-sb-provisioner's update_eeprom, which always passes
// `-k <key>` (get_eeprom_digest_sign_args) when signing is available.
static bool writePieepromSig(const std::filesystem::path& bootImgPath,
                              const std::filesystem::path& sigPath,
                              const QString& rsaKeyPath,
                              std::string& errOut)
{
    QString imgQStr = QString::fromStdString(bootImgPath.string());
    QByteArray hashHex = SecureBoot::sha256File(imgQStr);
    if (hashHex.isEmpty()) {
        errOut = "Failed to hash signed pieeprom.bin";
        return false;
    }

    // RSA-sign the raw 32-byte digest (rsaSign wraps it in the PKCS#1
    // DigestInfo internally), matching `openssl dgst -sha256 -sign`.
    const QByteArray rawDigest = QByteArray::fromHex(hashHex);
    const QByteArray sigHex = SecureBoot::rsaSign(rawDigest, rsaKeyPath);
    if (sigHex.isEmpty()) {
        errOut = "Failed to RSA-sign pieeprom.sig with " + rsaKeyPath.toStdString();
        return false;
    }

    QFile f(QString::fromStdString(sigPath.string()));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        errOut = "Cannot write " + sigPath.string();
        return false;
    }
    f.write(hashHex);
    f.write("\n");
    f.write("ts: ");
    f.write(QByteArray::number(SecureBoot::getCurrentTimestamp()));
    f.write("\n");
    f.write("rsa2048: ");
    f.write(sigHex);
    f.write("\n");
    f.close();
    return true;
}

bool SecureBootProvisioner::prepareSignedRecovery(ChipGeneration gen,
                                                    const std::filesystem::path& recoveryDir,
                                                    const std::filesystem::path& privateKeyPath,
                                                    bool counterSignFirmware,
                                                    std::string& errOut)
{
    if (!std::filesystem::exists(recoveryDir)) {
        errOut = "Recovery firmware directory not found: " + recoveryDir.string();
        return false;
    }
    if (!std::filesystem::exists(privateKeyPath)) {
        errOut = "Private key not found: " + privateKeyPath.string();
        return false;
    }
    if (gen != ChipGeneration::BCM2711 && gen != ChipGeneration::BCM2712) {
        errOut = "Secure-boot signing is only supported for BCM2711/BCM2712";
        return false;
    }

    const auto pieepromOriginal = recoveryDir / "pieeprom.original.bin";
    const auto pieepromOut      = recoveryDir / "pieeprom.bin";
    const auto pieepromSig      = recoveryDir / "pieeprom.sig";
    if (!std::filesystem::exists(pieepromOriginal)) {
        errOut = "pieeprom.original.bin not found in " + recoveryDir.string();
        return false;
    }

    QString keyQStr = QString::fromStdString(privateKeyPath.string());

    // 1. Load pieeprom.original.bin into the TLV editor.
    BootloaderImage img;
    if (!img.load(QString::fromStdString(pieepromOriginal.string()))) {
        errOut = "Failed to parse pieeprom.original.bin: " + img.lastError().toStdString();
        return false;
    }

    // 2. Build bootconf.txt and its RSA signature blob.
    const QByteArray bootConf = (gen == ChipGeneration::BCM2712)
        ? defaultBootConf2712() : defaultBootConf2711();
    const QByteArray bootConfSig = SecureBoot::generateConfigSig(bootConf, keyQStr);
    if (bootConfSig.isEmpty()) {
        errOut = "Failed to sign bootconf.txt with " + privateKeyPath.string();
        return false;
    }

    // 3. Extract the bootloader's public key in the 264-byte format the
    //    EEPROM consumes (256-byte N || 8-byte E, both little-endian).
    const QByteArray pubkeyBin = SecureBoot::extractRsaPubkeyBin(keyQStr);
    if (pubkeyBin.size() != 264) {
        errOut = "Failed to extract RSA pubkey from " + privateKeyPath.string();
        return false;
    }

    // 4. (Optional) Counter-sign the bootcode embedded in pieeprom for
    //    chips with secure-boot already fused.  On the BCM2712 the boot
    //    ROM additionally verifies the second-stage bootcode against the
    //    customer key once the OTP hash is non-zero.
    if (counterSignFirmware && gen == ChipGeneration::BCM2712) {
        QByteArray bootcode = img.getFile(QStringLiteral("bootcode.bin"));
        if (bootcode.isEmpty()) {
            errOut = "Failed to extract bootcode.bin from pieeprom.original.bin";
            return false;
        }
        QByteArray signedBootcode = SecureBoot::signBootcode2712(bootcode, keyQStr);
        if (signedBootcode.isEmpty()) {
            errOut = "Failed to counter-sign bootcode.bin";
            return false;
        }
        if (!img.updateBootcode(signedBootcode)) {
            errOut = "Failed to embed signed bootcode in pieeprom.bin: "
                   + img.lastError().toStdString();
            return false;
        }

        // AB-capable EEPROM images carry a second bootcode blob, "bootsys",
        // in the first partition.  The boot ROM verifies it against the
        // customer key exactly like bootcode.bin, so it must be counter-
        // signed identically (rpi-sign-bootcode -c 2712 -n 16 -v 0).
        // Omitting this leaves bootsys unsigned and the ROM rejects the
        // image.  Mirrors usbboot's update-pieeprom.sh and
        // rpi-sb-provisioner #306; non-AB images have no bootsys.
        if (img.isABImage()) {
            QByteArray bootsys = img.getFile(QStringLiteral("bootsys"));
            if (bootsys.isEmpty()) {
                errOut = "AB-capable pieeprom.original.bin has no bootsys content";
                return false;
            }
            QByteArray signedBootsys = SecureBoot::signBootcode2712(bootsys, keyQStr);
            if (signedBootsys.isEmpty()) {
                errOut = "Failed to counter-sign bootsys";
                return false;
            }
            if (!img.updateBootsys(signedBootsys)) {
                errOut = "Failed to embed signed bootsys in pieeprom.bin: "
                       + img.lastError().toStdString();
                return false;
            }
        }
    }

    // 5. Splice bootconf.txt, bootconf.sig and pubkey.bin into the EEPROM.
    if (!img.updateFile(QStringLiteral("bootconf.txt"), bootConf)) {
        errOut = "Failed to update bootconf.txt: " + img.lastError().toStdString();
        return false;
    }
    if (!img.updateFile(QStringLiteral("bootconf.sig"), bootConfSig)) {
        errOut = "Failed to update bootconf.sig: " + img.lastError().toStdString();
        return false;
    }
    if (!img.updateFile(QStringLiteral("pubkey.bin"), pubkeyBin)) {
        errOut = "Failed to update pubkey.bin: " + img.lastError().toStdString();
        return false;
    }

    if (!img.save(QString::fromStdString(pieepromOut.string()))) {
        errOut = "Failed to write " + pieepromOut.string();
        return false;
    }

    // 6. Generate pieeprom.sig (sha256 + ts + rsa2048, signed with the
    //    customer key) — the recovery verifies this before flashing on a
    //    secure-boot device.
    if (!writePieepromSig(pieepromOut, pieepromSig, keyQStr, errOut))
        return false;

    // Note: counter-signing the second-stage bootcode that gets uploaded to
    // the ROM (versionDir/bootcode5.bin) is handled by FirmwareManager — it
    // owns the top-level cache directory that BootcodeLoader reads from,
    // and the same logic applies for both Fastboot and SecureBootRecovery
    // modes on BCM2712.  BCM2711 doesn't require that step.

    qDebug() << "SecureBootProvisioner: signed pieeprom.bin written to"
             << QString::fromStdString(pieepromOut.string())
             << "(counter-sign firmware:" << counterSignFirmware << ")";
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
