/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

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

const std::vector<uint8_t>* Bootfiles::find(const std::string& name) const
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

    return nullptr;
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

        std::vector<uint8_t> data(static_cast<size_t>(entrySize));

        if (entrySize > 0) {
            // Read in chunks to handle large files without requiring
            // the entire file in a single read call
            size_t offset = 0;
            size_t remaining = static_cast<size_t>(entrySize);

            while (remaining > 0) {
                la_ssize_t bytesRead = archive_read_data(a, data.data() + offset, remaining);
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
