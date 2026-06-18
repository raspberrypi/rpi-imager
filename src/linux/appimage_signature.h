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

// Verify the embedded signature against `trusted_key_fingerprints` (uppercase hex,
// no spaces). When the list is empty, only structural checks are performed and
// unsigned images return VerifyResult::Unsigned.
VerifyResult verifyEmbeddedSignature(
    const std::string& appimage_path,
    const std::vector<std::string>& trusted_key_fingerprints);

const char* verifyResultMessage(VerifyResult r);

} // namespace rpi_imager::appimage
