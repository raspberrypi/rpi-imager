/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * In-memory tar extraction for bootfiles.bin.
 *
 * The sideload firmware packages its boot files as a tar
 * archive (bootfiles.bin).  This class uses libarchive (already bundled)
 * to extract files on demand into an in-memory map, which is then used
 * by the file server when the device requests individual files.
 */

#ifndef RPIBOOT_BOOTFILES_H
#define RPIBOOT_BOOTFILES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Forward-declare libarchive's archive type at global scope
// (avoids implicit declaration inside rpiboot namespace)
struct archive;

namespace rpiboot {

class Bootfiles {
public:
    // Extract all entries from a tar archive stored in memory.
    // Returns true on success.
    bool extractFromMemory(const std::vector<uint8_t>& tarData);

    // Extract from a tar file on disk.
    bool extractFromFile(const std::string& path);

    // Look up a file by name (path within the archive).
    // Returns a pointer to the data, or nullptr if not found.
    // If chipPrefix is non-empty and the exact name isn't found,
    // tries "<chipPrefix>/<name>" to resolve chip-specific subdirectories
    // (e.g. chipPrefix="2712" resolves "mcb.bin" → "2712/mcb.bin").
    const std::vector<uint8_t>* find(const std::string& name,
                                      std::string_view chipPrefix = {}) const;

    // Number of entries extracted
    size_t size() const { return _files.size(); }

    // Access the entire file map (for iteration / debugging)
    const std::map<std::string, std::vector<uint8_t>>& files() const { return _files; }

    const std::string& lastError() const { return _lastError; }

private:
    bool extractFromArchive(::archive* a);

    std::map<std::string, std::vector<uint8_t>> _files;
    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_BOOTFILES_H
