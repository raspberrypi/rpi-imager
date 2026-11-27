// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free verification of AppImage Type 2 embedded GPG signatures (.sha256_sig).

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace rpi_imager::appimage {

enum class VerifyResult {
    Ok,
    Unsigned,
    Tampered,
    UntrustedKey,
    IoError,
    InternalError,
};

struct SectionInfo {
    std::size_t offset = 0;
    std::size_t length = 0;
};

// Locate an ELF section by name in a Type 2 AppImage on disk.
bool findElfSection(const std::string& path, const char* section_name, SectionInfo& out);

// Verify the embedded signature with the key embedded beside it, then require
// that key's primary fingerprint to be in `trusted_key_fingerprints` (uppercase
// hex, no spaces). An empty list accepts any good signature; unsigned images
// return VerifyResult::Unsigned either way.
VerifyResult verifyEmbeddedSignature(
    const std::string& appimage_path,
    const std::vector<std::string>& trusted_key_fingerprints);

const char* verifyResultMessage(VerifyResult r);

} // namespace rpi_imager::appimage
