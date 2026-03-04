/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "firmware_manager.h"
#include "bootcode_loader.h"
#include "bootfiles.h"

#include "../curlnetworkconfig.h"

#include <QDebug>
#include <QStandardPaths>

#include <curl/curl.h>

#include <fstream>

namespace rpiboot {

FirmwareManager::FirmwareManager() = default;

std::filesystem::path FirmwareManager::cacheRoot() const
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return std::filesystem::path(cacheDir.toStdString()) / "rpiboot-firmware";
}

// ── buildManifest ───────────────────────────────────────────────────────

std::vector<FirmwareManager::ManifestEntry> FirmwareManager::buildManifest(
    SideloadMode mode, ChipGeneration chip) const
{
    std::vector<ManifestEntry> entries;
    const std::string usbboot(USBBOOT_RAW_BASE);
    const std::string provisioner(PROVISIONER_RAW_BASE);

    // Bootcode file — always required
    if (chip == ChipGeneration::BCM2711) {
        entries.push_back({usbboot + "firmware/2711/bootcode4.bin", "bootcode4.bin"});
    } else if (chip == ChipGeneration::BCM2712) {
        // For SecureBootRecovery, recovery5/bootcode5.bin IS the correct
        // binary — it boots the device into EEPROM-update mode.
        //
        // For Fastboot, the correct bootcode5.bin lives INSIDE the
        // firmware/bootfiles.bin TAR archive (symlinked as
        // mass-storage-gadget64/bootfiles.bin).  recovery5/bootcode5.bin is
        // the EEPROM update binary and will NOT transition the device to
        // mass-storage/file-server mode.  It is extracted by
        // extractBootcodeFromBootfiles() in ensureAvailable().
        if (mode == SideloadMode::SecureBootRecovery) {
            entries.push_back({usbboot + "recovery5/bootcode5.bin", "bootcode5.bin"});
        }
    }

    switch (mode) {
    case SideloadMode::Fastboot:
        entries.push_back({provisioner + "host-support/fastboot-gadget.img",
                           "fastboot/boot.img"});
        // mass-storage-gadget64/bootfiles.bin is a git symlink pointing to
        // ../firmware/bootfiles.bin — GitHub raw serves the symlink target
        // text, not the binary.  Download the real file directly.
        entries.push_back({usbboot + "firmware/bootfiles.bin",
                           "fastboot/bootfiles.bin"});
        entries.push_back({usbboot + "mass-storage-gadget64/config.txt",
                           "fastboot/config.txt"});
        break;

    case SideloadMode::SecureBootRecovery:
        if (chip == ChipGeneration::BCM2711) {
            const std::string sub = "secure-boot-recovery/";
            entries.push_back({usbboot + sub + "boot.conf",            sub + "boot.conf"});
            entries.push_back({usbboot + sub + "bootcode4.bin",         sub + "bootcode4.bin"});
            entries.push_back({usbboot + sub + "config.txt",            sub + "config.txt"});
            entries.push_back({usbboot + sub + "pieeprom.original.bin", sub + "pieeprom.original.bin"});
        } else if (chip == ChipGeneration::BCM2712) {
            const std::string sub = "secure-boot-recovery5/";
            entries.push_back({usbboot + sub + "boot.conf",              sub + "boot.conf"});
            entries.push_back({usbboot + sub + "config.txt",              sub + "config.txt"});
            entries.push_back({usbboot + sub + "pieeprom.original.bin",   sub + "pieeprom.original.bin"});
            entries.push_back({usbboot + sub + "recovery.original.bin",   sub + "recovery.original.bin"});
        }
        break;
    }

    return entries;
}

// ── ensureAvailable ─────────────────────────────────────────────────────

std::filesystem::path FirmwareManager::ensureAvailable(SideloadMode mode,
                                                        ChipGeneration chip,
                                                        ProgressCallback progress,
                                                        std::atomic<bool>& cancelled)
{
    auto root = cacheRoot();
    auto versionDir = root / "master";

    // 1. Build the file manifest
    auto manifest = buildManifest(mode, chip);
    if (manifest.empty()) {
        _lastError = "No firmware files defined for this device";
        return {};
    }

    // For BCM2712+Fastboot, bootcode5.bin must be extracted from the
    // bootfiles.bin TAR on every run to avoid stale/wrong cached binaries
    // (e.g. a previously cached recovery5/bootcode5.bin).
    const bool needsBootcodeExtraction =
        (chip == ChipGeneration::BCM2712 && mode == SideloadMode::Fastboot);

    // 2. Cache hit — all manifest files already exist (and no stale .tmp files)
    bool allExist = true;
    for (const auto& entry : manifest) {
        auto filePath = versionDir / entry.localPath;
        if (!std::filesystem::exists(filePath)) {
            allExist = false;
            // Clean up any leftover .tmp file from an interrupted download
            std::error_code ec;
            std::filesystem::remove(std::filesystem::path(filePath).concat(".tmp"), ec);
            break;
        }
    }
    if (allExist && !needsBootcodeExtraction) {
        qDebug() << "FirmwareManager: cache hit for master";
        return versionDir;
    }

    // 3. Download each missing file
    if (progress)
        progress(0, 100, "Downloading rpiboot firmware...");

    std::error_code ec;
    std::filesystem::create_directories(versionDir, ec);
    if (ec) {
        _lastError = "Cannot create firmware cache directory: " + ec.message();
        return {};
    }

    size_t totalFiles = manifest.size();
    for (size_t i = 0; i < totalFiles; ++i) {
        if (cancelled.load()) {
            _lastError = "Cancelled";
            return {};
        }

        const auto& entry = manifest[i];
        auto destPath = versionDir / entry.localPath;

        // Skip files already downloaded
        if (std::filesystem::exists(destPath))
            continue;

        // Wrap progress callback to map per-file progress to overall progress
        auto fileProgress = [&progress, i, totalFiles](uint64_t current, uint64_t total,
                                                        const std::string&) {
            if (progress && total > 0) {
                uint64_t overallPct = (i * 100 + current * 100 / total) / totalFiles;
                progress(overallPct, 100, "Downloading rpiboot firmware...");
            }
        };

        if (!downloadFile(entry.url, destPath, fileProgress, cancelled)) {
            return {};  // _lastError already set by downloadFile
        }
    }

    // 3b. For BCM2712+Fastboot: extract the correct bootcode5.bin from the
    // bootfiles.bin TAR (upstream uses check_file() against this same TAR).
    if (needsBootcodeExtraction) {
        if (!extractBootcodeFromBootfiles(versionDir))
            return {};  // _lastError set by helper
    }

    // 4. Validate cache
    if (!validateCacheForDevice(versionDir, mode, chip)) {
        _lastError = "Downloaded firmware does not contain required files";
        std::filesystem::remove_all(versionDir, ec);
        return {};
    }

    if (progress)
        progress(100, 100, "Firmware ready");

    return versionDir;
}

// ── Curl helpers for file download ──────────────────────────────────────

static size_t curlWriteToFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream*>(userdata);
    size_t totalSize = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(totalSize));
    return out->good() ? totalSize : 0;
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
        (*data->progress)(static_cast<uint64_t>(dlnow), static_cast<uint64_t>(dltotal),
                          "Downloading rpiboot firmware...");
    }
    return 0;
}

// ── downloadFile ────────────────────────────────────────────────────────

bool FirmwareManager::downloadFile(const std::string& url,
                                    const std::filesystem::path& destPath,
                                    ProgressCallback progress,
                                    std::atomic<bool>& cancelled)
{
    qDebug() << "FirmwareManager: downloading" << url.c_str();

    // Ensure parent directory exists
    std::error_code ec;
    std::filesystem::create_directories(destPath.parent_path(), ec);
    if (ec) {
        _lastError = "Cannot create directory: " + ec.message();
        return false;
    }

    // Write to a temporary file and rename on success to prevent partial
    // downloads from poisoning the cache (crash, network drop, SIGKILL).
    auto tmpPath = std::filesystem::path(destPath).concat(".tmp");

    std::ofstream out(tmpPath, std::ios::binary);
    if (!out) {
        _lastError = "Cannot create file: " + tmpPath.string();
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        _lastError = "Failed to initialize curl";
        out.close();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    CurlNetworkConfig::instance().applyCurlSettings(
        curl, CurlNetworkConfig::FetchProfile::LargeFile, errorBuffer);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    FirmwareProgressData progressData{&progress, &cancelled};
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressData);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    out.close();

    if (cancelled.load()) {
        _lastError = "Download cancelled";
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    if (res != CURLE_OK) {
        _lastError = "Download failed: ";
        _lastError += errorBuffer[0] ? errorBuffer : curl_easy_strerror(res);
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    // Atomic rename: only a complete download becomes visible to the cache
    std::filesystem::rename(tmpPath, destPath, ec);
    if (ec) {
        _lastError = "Failed to rename downloaded file: " + ec.message();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }

    qDebug() << "FirmwareManager: saved" << QString::fromStdString(destPath.string());
    return true;
}

// ── validateCacheForDevice ──────────────────────────────────────────────

bool FirmwareManager::validateCacheForDevice(const std::filesystem::path& versionDir,
                                              SideloadMode mode,
                                              ChipGeneration chip) const
{
    if (!std::filesystem::exists(versionDir))
        return false;

    // Check bootcode file for this chip (must exist and be non-empty to
    // guard against partially-downloaded files from interrupted runs)
    std::error_code ec;
    auto bootcodeFile = versionDir / BootcodeLoader::bootcodeFilename(chip);
    auto bootcodeSize = std::filesystem::file_size(bootcodeFile, ec);
    if (ec || bootcodeSize == 0)
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
    auto masterDir = cacheRoot() / "master";
    if (validateCacheForDevice(masterDir, mode, chip))
        return masterDir;
    return std::nullopt;
}

// ── clearCache ──────────────────────────────────────────────────────────

void FirmwareManager::clearCache()
{
    std::error_code ec;
    std::filesystem::remove_all(cacheRoot(), ec);
}

// ── extractBootcodeFromBootfiles ─────────────────────────────────────────
// For BCM2712 in Fastboot mode, bootcode5.bin must come from inside the
// firmware/bootfiles.bin TAR archive (symlinked as
// mass-storage-gadget64/bootfiles.bin).  The upstream rpiboot uses
// check_file() with use_bootfiles=1 to read it from the same TAR.
// recovery5/bootcode5.bin is the EEPROM update binary and is only correct
// for SecureBootRecovery mode.

bool FirmwareManager::extractBootcodeFromBootfiles(const std::filesystem::path& versionDir)
{
    auto tarPath = versionDir / "fastboot" / "bootfiles.bin";

    Bootfiles bootfiles;
    if (!bootfiles.extractFromFile(tarPath.string())) {
        _lastError = "Cannot read fastboot/bootfiles.bin: " + bootfiles.lastError();
        return false;
    }

    // In firmware/bootfiles.bin (= mass-storage-gadget64/bootfiles.bin),
    // BCM2712 bootcode is stored as "2712/bootcode5.bin".
    // The rpi-sb-bootstrap.sh script confirms this layout:
    //   tar -vxf /usr/share/rpiboot/mass-storage-gadget64/bootfiles.bin
    //   # produces 2712/bootcode5.bin among other files
    const auto* data = bootfiles.find("2712/bootcode5.bin");
    if (!data || data->empty()) {
        // Fallback: some builds may have it at the root
        data = bootfiles.find("bootcode5.bin");
    }
    if (!data || data->empty()) {
        _lastError = "bootcode5.bin not found inside fastboot/bootfiles.bin "
                     "(tried 2712/bootcode5.bin and bootcode5.bin)";
        return false;
    }

    auto destPath = versionDir / "bootcode5.bin";
    std::ofstream out(destPath, std::ios::binary);
    if (!out) {
        _lastError = "Cannot write bootcode5.bin: " + destPath.string();
        return false;
    }
    out.write(reinterpret_cast<const char*>(data->data()),
              static_cast<std::streamsize>(data->size()));
    if (!out) {
        _lastError = "Write failed for bootcode5.bin";
        return false;
    }

    qDebug() << "FirmwareManager: extracted bootcode5.bin ("
             << data->size() << "bytes) from fastboot/bootfiles.bin";
    return true;
}

} // namespace rpiboot
