/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <algorithm>
#include "bootfiles.h"

#include <archive.h>
#include <archive_entry.h>

#include <fstream>

namespace {

// Whether a pathname fits USTAR's name[100], or a prefix[155] split on '/'.
bool fitsUstarName(const std::string& name)
{
    if (name.size() <= 100)
        return true;
    if (name.size() > 256)
        return false;
    // Split so the tail fits name[100] and the head prefix[155].
    for (size_t i = 0; i + 1 < name.size(); ++i) {
        if (name[i] != '/')
            continue;
        if (i <= 155 && name.size() - i - 1 <= 100)
            return true;
    }
    return false;
}

// archive_error_string() answers null where libarchive set no message, which
// several of its failures do not. Appending that to a std::string is
// undefined, so every report below goes through here.
std::string archiveError(struct archive* a)
{
    const char* why = archive_error_string(a);
    return why ? why : "libarchive gave no reason";
}

} // namespace

namespace rpiboot {

bool Bootfiles::extractFromMemory(const std::vector<uint8_t>& tarData)
{
    _files.clear();
    _entries.clear();

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
        _lastError = std::string("Failed to open tar from memory: ") + archiveError(a);
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
    _entries.clear();

    ::archive* a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_format_raw(a);
    archive_read_support_filter_none(a);

    int rc = archive_read_open_filename(a, path.c_str(), 16384);
    if (rc != ARCHIVE_OK) {
        _lastError = std::string("Failed to open tar file: ") + archiveError(a);
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
    // Checked before anything is opened, so a name we cannot store leaves no
    // file behind; GNU would otherwise write a @LongLink entry no ROM reads.
    for (const auto& meta : _entries) {
        if (!fitsUstarName(meta.name)) {
            _lastError = "writeToFile: pathname too long for tar: " + meta.name;
            return false;
        }
    }

    ::archive* a = archive_write_new();
    // GNU tar, not USTAR: what upstream's `tar -vcf` writes.  The magic
    // differs, changing every checksum.
    archive_write_set_format_gnutar(a);
    if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) {
        _lastError = std::string("writeToFile: ") + archiveError(a);
        archive_write_free(a);
        return false;
    }

    static const std::vector<uint8_t> kNoData;

    for (const auto& meta : _entries) {
        const std::string& name = meta.name;
        const auto it = _files.find(name);
        const std::vector<uint8_t>& data =
            it != _files.end() ? it->second : kNoData;

        ::archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name.c_str());
        archive_entry_set_filetype(e, meta.filetype);
        archive_entry_set_perm(e, meta.perm);
        archive_entry_set_uid(e, meta.uid);
        archive_entry_set_gid(e, meta.gid);
        if (!meta.uname.empty())
            archive_entry_set_uname(e, meta.uname.c_str());
        if (!meta.gname.empty())
            archive_entry_set_gname(e, meta.gname.c_str());
        if (meta.hasMtime)
            archive_entry_set_mtime(e, meta.mtime, 0);
        if (meta.filetype == AE_IFLNK && !meta.symlinkTarget.empty())
            archive_entry_set_symlink(e, meta.symlinkTarget.c_str());
        // Only a regular entry carries a payload; a directory declaring one
        // makes libarchive expect bytes that never come.
        const bool isRegular = (meta.filetype == AE_IFREG);
        archive_entry_set_size(e, isRegular
                                      ? static_cast<la_int64_t>(data.size())
                                      : 0);

        if (archive_write_header(a, e) != ARCHIVE_OK) {
            _lastError = std::string("writeToFile header for ") + name + ": "
                       + archiveError(a);
            archive_entry_free(e);
            archive_write_close(a);
            archive_write_free(a);
            return false;
        }

        if (isRegular && !data.empty()) {
            la_ssize_t written = archive_write_data(a, data.data(), data.size());
            if (written < 0 ||
                static_cast<size_t>(written) != data.size()) {
                _lastError = std::string("writeToFile data for ") + name + ": "
                           + (written < 0 ? archiveError(a) : "short write");
                archive_entry_free(e);
                archive_write_close(a);
                archive_write_free(a);
                return false;
            }
        }

        archive_entry_free(e);
    }

    if (archive_write_close(a) != ARCHIVE_OK) {
        _lastError = std::string("writeToFile close: ") + archiveError(a);
        archive_write_free(a);
        return false;
    }
    archive_write_free(a);
    return true;
}

bool Bootfiles::extractFromArchive(::archive* a)
{
    // Numeric in the header so it need not include libarchive.
    static_assert(kFileTypeRegular == AE_IFREG, "AE_IFREG mismatch");
    static_assert(kFileTypeSymlink == AE_IFLNK, "AE_IFLNK mismatch");

    ::archive_entry* entry;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const unsigned int filetype =
            static_cast<unsigned int>(archive_entry_filetype(entry));
        const char* rawName = archive_entry_pathname(entry);
        if (!rawName)
            continue;

        // Strip leading "./" from the entry name for cleaner lookups
        std::string name = rawName;
        if (name.size() > 2 && name[0] == '.' && name[1] == '/')
            name = name.substr(2);

        // Record what tar said about every entry, directories included.
        EntryMeta meta;
        meta.name = name;
        meta.filetype = filetype;
        meta.perm = static_cast<unsigned int>(archive_entry_perm(entry));
        meta.uid = archive_entry_uid(entry);
        meta.gid = archive_entry_gid(entry);
        if (const char* u = archive_entry_uname(entry))
            meta.uname = u;
        if (const char* g = archive_entry_gname(entry))
            meta.gname = g;
        if (archive_entry_mtime_is_set(entry)) {
            meta.mtime = archive_entry_mtime(entry);
            meta.hasMtime = true;
        }
        if (filetype == AE_IFLNK) {
            if (const char* t = archive_entry_symlink(entry))
                meta.symlinkTarget = t;
        }

        if (filetype != AE_IFREG) {
            _entries.push_back(std::move(meta));
            archive_read_data_skip(a);
            continue;
        }

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
                    _lastError = std::string("Error reading archive entry '") + name + "': " + archiveError(a);
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

        _entries.push_back(std::move(meta));
        _files[std::move(name)] = std::move(data);
    }

    return true;
}

} // namespace rpiboot
