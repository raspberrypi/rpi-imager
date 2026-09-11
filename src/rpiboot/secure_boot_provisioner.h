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

    // Prepare a secure-boot-recovery firmware directory for re-provisioning
    // an already-fused CM5 (or CM4).  Operates in-place in `recoveryDir`,
    // which must already contain `pieeprom.original.bin` and (for BCM2712)
    // `recovery.original.bin`.  Produces:
    //   - `pieeprom.bin`     — pieeprom.original.bin with bootconf.txt
    //                          replaced (SIGNED_BOOT=1, ENABLE_SELF_UPDATE=0
    //                          plus chip-default options), bootconf.sig
    //                          embedded, customer pubkey embedded, and (when
    //                          counterSignFirmware) a customer-counter-signed
    //                          bootcode.
    //   - `pieeprom.sig`     — sha256 + ts + rsa2048, signed with the
    //                          customer key. The recovery binary verifies
    //                          this before flashing on a secure-boot device,
    //                          so the rsa2048 line is required; bootconf.sig
    //                          embedded inside pieeprom.bin is a separate
    //                          proof, not a replacement for it.
    static bool prepareSignedRecovery(ChipGeneration gen,
                                       const std::filesystem::path& recoveryDir,
                                       const std::filesystem::path& privateKeyPath,
                                       bool counterSignFirmware,
                                       std::string& errOut);

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
