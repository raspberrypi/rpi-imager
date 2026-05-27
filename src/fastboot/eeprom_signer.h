/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * In-process builder for bootconf.sig (and structurally compatible
 * .sig files for other EEPROM artifacts).
 *
 * rpi-eeprom-digest emits a small ASCII file of the form
 *
 *   <sha256-hex>\n
 *   ts: <epoch-seconds>\n
 *   [rsa2048: <signature-hex>\n]      ; only on signed-eeprom boards
 *
 * We produce the same bytes here using QCryptographicHash for the
 * digest and SecureBootCrypto::rsaSignSha256 (the platform-native
 * RSA signer already used by the secure-boot bootcode path) for the
 * signature. No subprocess to rpi-eeprom-digest --- this works on
 * Windows where that tool is not available.
 */
#ifndef FASTBOOT_EEPROM_SIGNER_H
#define FASTBOOT_EEPROM_SIGNER_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fastboot {

struct EepromSignerConfig {
    // Path to an RSA-2048 private key in PEM format. Leave empty to
    // emit an unsigned digest (sha256 + ts only) suitable for boards
    // where signed-eeprom=no.
    std::string pemKeyPath;

    // Override the timestamp written into the sig (seconds since
    // epoch). Empty means the signer uses the current UTC time.
    // Setting this makes the output deterministic for tests and for
    // matching rpi-eeprom-digest output produced under SOURCE_DATE_EPOCH.
    std::optional<int64_t> sourceDateEpoch;
};

struct EepromSignResult {
    bool ok = false;
    std::vector<uint8_t> sigBytes; // exact contents of the resulting .sig file
    std::string error;             // human-readable failure reason
};

// Build a .sig file over `input` (typically the bootconf.txt body).
// The result is suitable for embedding via
// pieeprom::Image::writeBootConfSig().
EepromSignResult buildEepromSig(std::span<const uint8_t> input,
                                const EepromSignerConfig& config);

} // namespace fastboot

#endif // FASTBOOT_EEPROM_SIGNER_H
