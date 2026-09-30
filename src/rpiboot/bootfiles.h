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

    // Replace (or insert) the contents of a single entry in the in-memory
    // archive.  Used to splice a customer-counter-signed bootcode into the
    // bootfiles.bin we serve to the device.  Returns false if the entry
    // doesn't exist (use append-style writeToFile for new entries instead).
    bool replaceEntry(const std::string& name, std::vector<uint8_t> data);

    // Re-pack the current in-memory entries as a USTAR archive at `path`.
    // Entries keep the order, metadata and directories they arrived with;
    // only bytes passed to replaceEntry() differ.  Returns false on I/O /
    // libarchive error; details in lastError().
    bool writeToFile(const std::string& path);

    // Number of entries extracted
    size_t size() const { return _files.size(); }

    // Access the entire file map (for iteration / debugging)
    const std::map<std::string, std::vector<uint8_t>>& files() const { return _files; }

    const std::string& lastError() const { return _lastError; }

private:
    bool extractFromArchive(::archive* a);

    // What tar records and _files cannot: directories, permissions,
    // ownership, mtimes, arrival order.  Only one entry is meant to change,
    // so the rest must go back unchanged -- rebuilding from names and bytes
    // alone dropped 2711/ and 2712/ and demoted both bootmain to 0644.
    //
    // Numeric so this header need not include libarchive; bootfiles.cpp
    // asserts they match AE_IFREG/AE_IFLNK.
    static constexpr unsigned int kFileTypeRegular = 0100000;  // AE_IFREG
    static constexpr unsigned int kFileTypeSymlink = 0120000;  // AE_IFLNK

    struct EntryMeta {
        std::string name;
        std::string symlinkTarget;      // only when filetype is a symlink
        unsigned int filetype = kFileTypeRegular;
        unsigned int perm = 0644;
        int64_t uid = 0;
        int64_t gid = 0;
        std::string uname;
        std::string gname;
        int64_t mtime = 0;
        bool hasMtime = false;
    };

    std::vector<EntryMeta> _entries;
    std::map<std::string, std::vector<uint8_t>> _files;
    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_BOOTFILES_H
