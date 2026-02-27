/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "firmware_manager.h"
#include "bootcode_loader.h"

#include "../config.h"
#include "../curlnetworkconfig.h"

#include <QDebug>
#include <QStandardPaths>

#include <curl/curl.h>

#include <algorithm>
#include <fstream>

#include <archive.h>
#include <archive_entry.h>

namespace rpiboot {

FirmwareManager::FirmwareManager() = default;

std::filesystem::path FirmwareManager::cacheRoot() const
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return std::filesystem::path(cacheDir.toStdString()) / "rpiboot-firmware";
}

// ── ensureAvailable ─────────────────────────────────────────────────────

std::filesystem::path FirmwareManager::ensureAvailable(SideloadMode mode,
                                                        ChipGeneration chip,
                                                        ProgressCallback progress,
                                                        std::atomic<bool>& cancelled)
{
    auto root = cacheRoot();

    // 1. Discover the latest version via GitHub redirect (may fail silently)
    auto latestVersion = fetchLatestVersion(cancelled);
    if (cancelled.load()) {
        _lastError = "Cancelled";
        return {};
    }

    // Helper: build download URL from a version string
    auto buildUrl = [](const std::string& ver) {
        char buf[512];
        snprintf(buf, sizeof(buf), FALLBACK_FIRMWARE_URL_TEMPLATE, ver.c_str(), ver.c_str());
        return std::string(buf);
    };

    // 2. If we got a version, check cache for it
    if (latestVersion) {
        auto versionDir = root / *latestVersion;
        if (validateCacheForDevice(versionDir, mode, chip)) {
            qDebug() << "FirmwareManager: cache hit for" << latestVersion->c_str();
            return versionDir;
        }

        // Cache miss — download the tarball
        if (progress)
            progress(0, 100, "Downloading rpiboot firmware...");

        std::error_code ec;
        std::filesystem::create_directories(versionDir, ec);
        if (ec) {
            _lastError = "Cannot create firmware cache directory: " + ec.message();
            // Fall through to fallback
        } else {
            if (downloadAndExtract(buildUrl(*latestVersion), versionDir, progress, cancelled)) {
                if (validateCacheForDevice(versionDir, mode, chip)) {
                    cleanOldVersions(root, *latestVersion);
                    return versionDir;
                }
                qDebug() << "FirmwareManager: downloaded firmware does not contain files for this device";
            }
            // Clean up failed/invalid download
            if (!cancelled.load()) {
                std::filesystem::remove_all(versionDir, ec);
            }
        }
    }

    if (cancelled.load()) {
        _lastError = "Cancelled";
        return {};
    }

    // 3. Fallback: try any cached version
    auto cached = findCachedVersion(mode, chip);
    if (cached) {
        qDebug() << "FirmwareManager: falling back to cached version"
                 << cached->filename().c_str();
        return *cached;
    }

    // 4. Last resort: hardcoded fallback URL
    qDebug() << "FirmwareManager: trying hardcoded fallback version"
             << FALLBACK_FIRMWARE_VERSION;

    if (progress)
        progress(0, 100, "Downloading rpiboot firmware...");

    auto fallbackDir = root / FALLBACK_FIRMWARE_VERSION;
    std::error_code ec;
    std::filesystem::create_directories(fallbackDir, ec);
    if (ec) {
        _lastError = "Cannot create firmware cache directory: " + ec.message();
        return {};
    }

    if (downloadAndExtract(buildUrl(FALLBACK_FIRMWARE_VERSION), fallbackDir, progress, cancelled)) {
        if (validateCacheForDevice(fallbackDir, mode, chip))
            return fallbackDir;
    }

    if (!cancelled.load()) {
        std::filesystem::remove_all(fallbackDir, ec);
        if (_lastError.empty())
            _lastError = "All firmware sources failed";
    } else {
        _lastError = "Cancelled";
    }
    return {};
}

// ── fetchLatestVersion ──────────────────────────────────────────────────
//
// HEAD https://github.com/raspberrypi/usbboot/releases/latest
// → 302 Location: https://github.com/.../releases/tag/<version>
//
// We disable redirect following and read the Location header to extract
// the tag name, avoiding the GitHub API rate limit entirely.

std::optional<std::string> FirmwareManager::fetchLatestVersion(std::atomic<bool>& cancelled)
{
    if (cancelled.load())
        return std::nullopt;

    qDebug() << "FirmwareManager: checking latest version via" << RPIBOOT_FIRMWARE_RELEASES_URL;

    CURL *curl = curl_easy_init();
    if (!curl)
        return std::nullopt;

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    CurlNetworkConfig::instance().applyCurlSettings(
        curl, CurlNetworkConfig::FetchProfile::SmallFile, errorBuffer);

    curl_easy_setopt(curl, CURLOPT_URL, RPIBOOT_FIRMWARE_RELEASES_URL);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);           // HEAD request
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);   // Don't follow redirect
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);      // Accept 3xx responses

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK && res != CURLE_HTTP_RETURNED_ERROR) {
        qDebug() << "FirmwareManager: version check failed:"
                 << (errorBuffer[0] ? errorBuffer : curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    // Read the redirect URL from the Location header
    char *redirectUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirectUrl);

    if (!redirectUrl || !*redirectUrl) {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        qDebug() << "FirmwareManager: no redirect URL (HTTP" << httpCode << ")";
        curl_easy_cleanup(curl);
        return std::nullopt;
    }

    std::string location(redirectUrl);
    curl_easy_cleanup(curl);

    // Extract the tag from the URL: .../releases/tag/<version>
    constexpr std::string_view marker = "/releases/tag/";
    auto pos = location.find(marker);
    if (pos == std::string::npos) {
        qDebug() << "FirmwareManager: unexpected redirect URL:" << location.c_str();
        return std::nullopt;
    }

    std::string version = location.substr(pos + marker.size());

    // Reject empty or suspicious version strings (path traversal, etc.)
    if (version.empty() || version.find('/') != std::string::npos ||
        version.find("..") != std::string::npos) {
        qDebug() << "FirmwareManager: invalid version tag:" << version.c_str();
        return std::nullopt;
    }

    qDebug() << "FirmwareManager: latest version is" << version.c_str();
    return version;
}

// ── validateCacheForDevice ──────────────────────────────────────────────

bool FirmwareManager::validateCacheForDevice(const std::filesystem::path& versionDir,
                                              SideloadMode mode,
                                              ChipGeneration chip) const
{
    if (!std::filesystem::exists(versionDir))
        return false;

    // Check bootcode file for this chip
    auto bootcodeFile = versionDir / BootcodeLoader::bootcodeFilename(chip);
    if (!std::filesystem::exists(bootcodeFile))
        return false;

    // Check mode-specific subdirectory
    switch (mode) {
    case SideloadMode::Fastboot:
        return std::filesystem::exists(versionDir / "fastboot");
    case SideloadMode::SecureBootRecovery:
        return std::filesystem::exists(versionDir / "secure-boot-recovery") ||
               std::filesystem::exists(versionDir / "secure-boot-recovery5");
    }
    return false;
}

// ── findCachedVersion ───────────────────────────────────────────────────

std::optional<std::filesystem::path> FirmwareManager::findCachedVersion(SideloadMode mode,
                                                                         ChipGeneration chip) const
{
    auto root = cacheRoot();
    if (!std::filesystem::exists(root))
        return std::nullopt;

    // Collect valid version dirs and sort descending (newest version first)
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory())
            continue;
        if (validateCacheForDevice(entry.path(), mode, chip))
            candidates.push_back(entry.path());
    }

    if (candidates.empty())
        return std::nullopt;

    // Sort by directory name descending — version strings sort lexicographically
    std::sort(candidates.begin(), candidates.end(), std::greater<>());
    return candidates.front();
}

// ── cleanOldVersions ────────────────────────────────────────────────────

void FirmwareManager::cleanOldVersions(const std::filesystem::path& root,
                                         const std::string& keepVersion)
{
    // Collect first — deleting during directory_iterator is UB
    std::vector<std::filesystem::path> toRemove;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (entry.is_directory() && entry.path().filename().string() != keepVersion)
            toRemove.push_back(entry.path());
    }
    for (auto& p : toRemove) {
        qDebug() << "FirmwareManager: removing old cache" << p.filename().c_str();
        std::filesystem::remove_all(p, ec);
    }
}

// ── Curl helpers for tarball download ───────────────────────────────────

struct DownloadBuffer {
    std::vector<char> data;
};

static size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *buf = static_cast<DownloadBuffer*>(userdata);
    size_t totalSize = size * nmemb;
    buf->data.insert(buf->data.end(), ptr, ptr + totalSize);
    return totalSize;
}

struct FirmwareProgressData {
    ProgressCallback* progress;
    std::atomic<bool>* cancelled;
};

static int curlProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                 curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto *data = static_cast<FirmwareProgressData*>(clientp);
    if (data->cancelled->load())
        return 1;  // Abort
    if (data->progress && *data->progress && dltotal > 0) {
        int pct = static_cast<int>(dlnow * 50 / dltotal);  // 0-50% for download
        (*data->progress)(static_cast<uint64_t>(pct), 100, "Downloading rpiboot firmware...");
    }
    return 0;
}

// ── downloadAndExtract ──────────────────────────────────────────────────

bool FirmwareManager::downloadAndExtract(const std::string& url,
                                          const std::filesystem::path& destDir,
                                          ProgressCallback progress,
                                          std::atomic<bool>& cancelled)
{
    qDebug() << "FirmwareManager: downloading firmware from" << url.c_str();

    // 1. Download the tar.gz via direct libcurl (LargeFile profile)
    DownloadBuffer downloadBuffer;

    {
        CURL *curl = curl_easy_init();
        if (!curl) {
            _lastError = "Failed to initialize curl";
            return false;
        }

        char errorBuffer[CURL_ERROR_SIZE] = {0};
        CurlNetworkConfig::instance().applyCurlSettings(
            curl, CurlNetworkConfig::FetchProfile::LargeFile, errorBuffer);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &downloadBuffer);

        FirmwareProgressData progressData{&progress, &cancelled};
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressData);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (cancelled.load()) {
            _lastError = "Download cancelled";
            return false;
        }

        if (res != CURLE_OK) {
            _lastError = "Download failed: ";
            _lastError += errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
            return false;
        }
    }

    if (downloadBuffer.data.empty()) {
        _lastError = "Downloaded firmware archive is empty";
        return false;
    }

    qDebug() << "FirmwareManager: downloaded" << downloadBuffer.data.size() << "bytes";

    if (progress)
        progress(50, 100, "Extracting firmware...");

    // 2. Extract the tar.gz archive using libarchive into destDir
    struct archive *a = archive_read_new();
    archive_read_support_filter_gzip(a);
    archive_read_support_format_tar(a);

    int r = archive_read_open_memory(a, downloadBuffer.data.data(),
                                     downloadBuffer.data.size());
    if (r != ARCHIVE_OK) {
        _lastError = "Failed to open firmware archive: " +
                     std::string(archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    struct archive_entry *entry;
    size_t filesExtracted = 0;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (cancelled.load()) {
            archive_read_free(a);
            _lastError = "Extraction cancelled";
            return false;
        }

        std::string entryPath = archive_entry_pathname(entry);

        // Strip leading "./" if present
        if (entryPath.starts_with("./"))
            entryPath = entryPath.substr(2);

        if (entryPath.empty()) {
            archive_read_data_skip(a);
            continue;
        }

        auto fullPath = destDir / entryPath;

        if (archive_entry_filetype(entry) == AE_IFDIR) {
            std::error_code ec;
            std::filesystem::create_directories(fullPath, ec);
        } else if (archive_entry_filetype(entry) == AE_IFREG) {
            // Ensure parent directory exists
            std::error_code ec;
            std::filesystem::create_directories(fullPath.parent_path(), ec);

            // Read and write file content
            std::ofstream out(fullPath, std::ios::binary);
            if (!out) {
                qDebug() << "FirmwareManager: failed to create" << QString::fromStdString(fullPath.string());
                archive_read_data_skip(a);
                continue;
            }

            const void *buff;
            size_t size;
            la_int64_t offset;
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                out.write(static_cast<const char*>(buff), static_cast<std::streamsize>(size));
            }
            out.close();
            ++filesExtracted;
        } else {
            archive_read_data_skip(a);
        }
    }

    archive_read_free(a);

    qDebug() << "FirmwareManager: extracted" << filesExtracted << "files to"
             << QString::fromStdString(destDir.string());

    if (filesExtracted == 0) {
        _lastError = "Firmware archive contained no files";
        return false;
    }

    if (progress)
        progress(100, 100, "Firmware ready");

    return true;
}

void FirmwareManager::clearCache()
{
    std::error_code ec;
    std::filesystem::remove_all(cacheRoot(), ec);
}

} // namespace rpiboot
