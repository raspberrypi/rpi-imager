/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <algorithm>
#include "bootfiles.h"

#include <archive.h>
#include <archive_entry.h>

#include <fstream>

namespace rpiboot {

bool Bootfiles::extractFromMemory(const std::vector<uint8_t>& tarData)
{
    _files.clear();

    if (tarData.empty()) {
        _lastError = "Empty archive data";
        return false;
    }

    ::archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_raw(a);
    archive_read_support_filter_none(a);

    int rc = archive_read_open_memory(a, tarData.data(), tarData.size());
    if (rc != ARCHIVE_OK) {
        _lastError = std::string("Failed to open tar from memory: ") + archive_error_string(a);
        archive_read_free(a);
        return false;
    }

    bool ok = extractFromArchive(a);
    archive_read_free(a);
    return ok;
}

bool Bootfiles::extractFromFile(const std::string& path)
{
    _files.clear();

    ::archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_raw(a);
    archive_read_support_filter_none(a);

    int rc = archive_read_open_filename(a, path.c_str(), 16384);
    if (rc != ARCHIVE_OK) {
        _lastError = std::string("Failed to open tar file: ") + archive_error_string(a);
        archive_read_free(a);
        return false;
    }

    bool ok = extractFromArchive(a);
    archive_read_free(a);
    return ok;
}

const std::vector<uint8_t>* Bootfiles::find(const std::string& name,
                                              std::string_view chipPrefix) const
{
    auto it = _files.find(name);
    if (it != _files.end())
        return &it->second;

    // Try stripping a leading "./" which tar may add
    if (name.size() > 2 && name[0] == '.' && name[1] == '/') {
        it = _files.find(name.substr(2));
        if (it != _files.end())
            return &it->second;
    }

    // Try with a leading "./" added
    it = _files.find("./" + name);
    if (it != _files.end())
        return &it->second;

    // Chip-specific subdirectory lookup: the TAR may store files in
    // directories like "2712/mcb.bin" but the device requests the bare
    // filename "mcb.bin".  Try "<chipPrefix>/<name>" directly.
    if (!chipPrefix.empty() && name.find('/') == std::string::npos) {
        std::string prefixed = std::string(chipPrefix) + "/" + name;
        it = _files.find(prefixed);
        if (it != _files.end())
            return &it->second;
    }

    return nullptr;
}

bool Bootfiles::replaceEntry(const std::string& name, std::vector<uint8_t> data)
{
    auto it = _files.find(name);
    if (it == _files.end()) {
        // Try with leading "./" too, mirroring find()
        it = _files.find("./" + name);
        if (it == _files.end()) {
            _lastError = "replaceEntry: entry not found: " + name;
            return false;
        }
    }
    it->second = std::move(data);
    return true;
}

bool Bootfiles::writeToFile(const std::string& path)
{
    ::archive* a = archive_write_new();
    // USTAR is the most portable / minimal tar format; it's what the
    // upstream `tar -vcf` produces by default on most Linux distros and
    // what the BCM2712 bootloader is happy to parse.
    archive_write_set_format_ustar(a);
    if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) {
        _lastError = std::string("writeToFile: ") + archive_error_string(a);
        archive_write_free(a);
        return false;
    }

    for (const auto& [name, data] : _files) {
        ::archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name.c_str());
        archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        // Match the file mode rpi-eeprom firmware ships with (0644).
        archive_entry_set_perm(e, 0644);

        if (archive_write_header(a, e) != ARCHIVE_OK) {
            _lastError = std::string("writeToFile header for ") + name + ": "
                       + archive_error_string(a);
            archive_entry_free(e);
            archive_write_close(a);
            archive_write_free(a);
            return false;
        }

        if (!data.empty()) {
            la_ssize_t written = archive_write_data(a, data.data(), data.size());
            if (written < 0 ||
                static_cast<size_t>(written) != data.size()) {
                _lastError = std::string("writeToFile data for ") + name + ": "
                           + (written < 0 ? archive_error_string(a) : "short write");
                archive_entry_free(e);
                archive_write_close(a);
                archive_write_free(a);
                return false;
            }
        }

        archive_entry_free(e);
    }

    if (archive_write_close(a) != ARCHIVE_OK) {
        _lastError = std::string("writeToFile close: ") + archive_error_string(a);
        archive_write_free(a);
        return false;
    }
    archive_write_free(a);
    return true;
}

bool Bootfiles::extractFromArchive(::archive* a)
{
    ::archive_entry* entry;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        // Skip directories
        if (archive_entry_filetype(entry) != AE_IFREG)
            continue;

        std::string name = archive_entry_pathname(entry);
        int64_t entrySize = archive_entry_size(entry);

        if (entrySize < 0) {
            // Skip entries with unknown size
            archive_read_data_skip(a);
            continue;
        }

        // Grown as the data arrives rather than sized from the header.
        //
        // entrySize is the size the archive claims for the entry, and it was
        // used to allocate before a byte had been read -- so a bootfiles.bin
        // whose header said four gigabytes asked for four gigabytes, whatever
        // the entry actually held. The file arrives with a downloaded firmware
        // release, and a truncated or damaged one is enough. Nothing is lost
        // by waiting: the old code trimmed to what it had read anyway.
        std::vector<uint8_t> data;

        if (entrySize > 0) {
            // Read in chunks to handle large files without requiring
            // the entire file in a single read call
            constexpr size_t kChunk = 256 * 1024;
            size_t offset = 0;
            size_t remaining = static_cast<size_t>(entrySize);

            while (remaining > 0) {
                const size_t want = std::min(remaining, kChunk);
                data.resize(offset + want);

                la_ssize_t bytesRead = archive_read_data(a, data.data() + offset, want);
                if (bytesRead < 0) {
                    _lastError = std::string("Error reading archive entry '") + name + "': " + archive_error_string(a);
                    return false;
                }
                if (bytesRead == 0)
                    break;

                offset += static_cast<size_t>(bytesRead);
                remaining -= static_cast<size_t>(bytesRead);
            }

            // Trim if we got fewer bytes than expected
            data.resize(offset);
        }

        // Strip leading "./" from the entry name for cleaner lookups
        if (name.size() > 2 && name[0] == '.' && name[1] == '/')
            name = name.substr(2);

        _files[std::move(name)] = std::move(data);
    }

    return true;
}

} // namespace rpiboot
