/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Orchestrates OTP provisioning for Raspberry Pi secure boot.
 * Handles key generation, EEPROM signing, and recovery firmware
 * preparation for programming the public key hash into device OTP.
 *
 * WARNING: OTP programming is permanent and irreversible.
 */

#ifndef RPIBOOT_SECURE_BOOT_PROVISIONER_H
#define RPIBOOT_SECURE_BOOT_PROVISIONER_H

#include "rpiboot_types.h"
#include "usb_transport.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <optional>
#include <string>

namespace rpiboot {

class SecureBootProvisioner {
public:
    // Generate an RSA-2048 key pair for secure boot signing.
    // The private key is written in PEM format.
    static bool generateKeyPair(const std::filesystem::path& privateKeyPath,
                                 const std::filesystem::path& publicKeyPath);

    // Calculate the SHA-256 hash of the public key that will be
    // programmed into device OTP.
    static std::optional<std::array<uint8_t, 32>> calculateOtpKeyHash(
        const std::filesystem::path& publicKeyPath);

    // Prepare a secure-boot-recovery firmware directory:
    //   - Copy base recovery firmware
    //   - Sign EEPROM with the provided private key
    //   - Set boot.conf with program_pubkey=1, SIGNED_BOOT=1
    //
    // The outputDir will contain everything needed for RpibootProtocol
    // to sideload in SecureBootRecovery mode.
    bool prepareRecoveryFirmware(ChipGeneration gen,
                                  const std::filesystem::path& baseFirmwareDir,
                                  const std::filesystem::path& privateKeyPath,
                                  const std::filesystem::path& outputDir,
                                  bool lockJtag = false);

    // Execute OTP provisioning via the rpiboot protocol.
    // This sideloads the recovery firmware which programs the key hash.
    bool provision(IUsbTransport& transport,
                    ChipGeneration gen,
                    const std::filesystem::path& recoveryDir,
                    ProgressCallback progress,
                    std::atomic<bool>& cancelled);

    // Sign a boot.img for use with a device that has secure boot locked.
    // Produces boot.sig alongside the image.
    static bool signBootImage(const std::filesystem::path& bootImg,
                               const std::filesystem::path& privateKeyPath,
                               const std::filesystem::path& outputSig);

    const std::string& lastError() const { return _lastError; }

private:
    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_SECURE_BOOT_PROVISIONER_H
