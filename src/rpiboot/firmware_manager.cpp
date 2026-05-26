/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "firmware_manager.h"
#include "bootcode_loader.h"
#include "bootfiles.h"
#include "secure_boot_provisioner.h"

#include "../curlnetworkconfig.h"
#include "../secureboot.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QString>

#include <curl/curl.h>

#include <fstream>
#include <sstream>

namespace rpiboot {

namespace {

// std::filesystem::copy_file(..., overwrite_existing, ec) is unreliable on
// Windows (mingw libstdc++): it can return std::errc::file_exists even
// when the option is set.  We've hit this at three separate sites in this
// file (custom fastboot gadget materialisation, bootfiles.bin restoration
// from .original, bootcode5.bin re-baseline from recovery.original.bin).
// Remove the destination first then copy without overwrite_existing — the
// remove is a no-op if the file doesn't exist, so this is always safe.
bool overwriteCopy(const std::filesystem::path& src,
                   const std::filesystem::path& dest,
                   std::error_code& ec) noexcept
{
    std::error_code removeEc;
    std::filesystem::remove(dest, removeEc);  // ignore — fine if absent
    ec.clear();
    return std::filesystem::copy_file(src, dest, ec);
}

}  // namespace

FirmwareManager::FirmwareManager() = default;

std::filesystem::path FirmwareManager::cacheRoot() const
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return std::filesystem::path(cacheDir.toStdString()) / "rpiboot-firmware";
}

// ── buildManifest ───────────────────────────────────────────────────────

std::vector<FirmwareManager::ManifestEntry> FirmwareManager::buildManifest(
    SideloadMode mode, ChipGeneration chip,
    const std::optional<std::string>& eepromVersion) const
{
    std::vector<ManifestEntry> entries;
    const std::string usbboot(USBBOOT_RAW_BASE);
    const std::string provisioner(PROVISIONER_RAW_BASE);
    const std::string eeprom(EEPROM_RAW_BASE);

    // Bootcode file — required for all chip generations.
    // BCM2836_7: downloaded directly from the usbboot msd/ directory; this is
    //   the same binary that upstream rpiboot has compiled in via msd/bootcode.h.
    // BCM2711/BCM2712 in Fastboot mode: extracted from bootfiles.bin at cache
    //   time (see extractBootcodeFromBootfiles).
    // BCM2711/BCM2712 in SecureBootRecovery mode: the USB-mode bootcode we
    //   upload to perform the EEPROM write *is* rpi-eeprom's recovery.bin
    //   for the matching chip — usbboot's various bootcode symlinks
    //   (secure-boot-recovery/bootcode4.bin, recovery5/bootcode5.bin) all
    //   chain into the same rpi-eeprom path, and fetching the symlinks via
    //   GitHub raw HTTP yields ~30 bytes of symlink-target text rather than
    //   the binary, so we go straight to the canonical source.
    //   (Note: usbboot/firmware/2711/bootcode4.bin is a different, real
    //   binary — not recovery — and shouldn't be used for SBR.)
    if (chip == ChipGeneration::BCM2711) {
        if (mode == SideloadMode::SecureBootRecovery) {
            entries.push_back({eeprom + "firmware-2711/latest/recovery.bin", "bootcode4.bin"});
        }
    } else if (chip == ChipGeneration::BCM2712) {
        if (mode == SideloadMode::SecureBootRecovery) {
            entries.push_back({eeprom + "firmware-2712/latest/recovery.bin", "bootcode5.bin"});
        }
    }

    switch (mode) {
    case SideloadMode::Fastboot:
        if (chip == ChipGeneration::BCM2836_7) {
            // BCM2837 (2710-class) Fastboot bootstrap follows rpi-sb-bootstrap.sh:
            // bootcode.bin is served from usbboot/msd (the same binary upstream
            // rpiboot has compiled in via msd/bootcode.h).  The 2710-bootfiles-bin
            // is a self-contained bundle — no separate boot.img or config.txt needed.
            entries.push_back({usbboot + "msd/bootcode.bin", "bootcode.bin"});
            entries.push_back({provisioner + "host-support/fastboot-gadget.2710-bootfiles-bin",
                               "fastboot/bootfiles.bin"});
        } else {
            // BCM2711/BCM2712: fastboot gadget kernel + config are separate from
            // bootfiles.bin; bootcode is extracted from the TAR at cache time.
            //
            // The upstream gadget is cached under its own name
            // (fastboot-gadget.img); the active boot.img the file_server
            // uploads is rematerialised on every run by ensureAvailable() —
            // either copied from this cached upstream or from the user's
            // current custom gadget.  Custom gadgets are session-only in the
            // UI, so they must not leak into the persistent cache.
            entries.push_back({provisioner + "host-support/fastboot-gadget.img",
                               "fastboot/fastboot-gadget.img"});
            entries.push_back({usbboot + "mass-storage-gadget64/config.txt",
                               "fastboot/config.txt"});
            entries.push_back({usbboot + "firmware/bootfiles.bin",
                               "fastboot/bootfiles.bin"});
        }
        break;

    case SideloadMode::SecureBootRecovery:
        // pieeprom and (BCM2712) recovery binaries come from rpi-eeprom's
        // `latest` channel — that's the canonical source for shipped
        // bootloader firmware.  Filenames there are dated (pieeprom-DATE.bin);
        // the date is resolved at runtime via versions.txt and threaded in
        // here as eepromVersion.  If unset (offline + no cache), the URL
        // is left empty so the download loop will fall back to the cached
        // file or fail with a clear error if neither is present.
        if (chip == ChipGeneration::BCM2711) {
            const std::string sub = "secure-boot-recovery/";
            entries.push_back({usbboot + sub + "boot.conf",            sub + "boot.conf"});
            // usbboot/secure-boot-recovery/bootcode4.bin is a git-symlink
            // (29 bytes via raw HTTP) — the real binary lives at the root
            // as bootcode4.bin (entry above), no need for a second copy.
            entries.push_back({usbboot + sub + "config.txt",            sub + "config.txt"});
            const std::string pieepromUrl = eepromVersion
                ? eeprom + "firmware-2711/latest/pieeprom-" + *eepromVersion + ".bin"
                : std::string();
            entries.push_back({pieepromUrl, sub + "pieeprom.original.bin"});
        } else if (chip == ChipGeneration::BCM2712) {
            const std::string sub = "secure-boot-recovery5/";
            entries.push_back({usbboot + sub + "boot.conf",              sub + "boot.conf"});
            entries.push_back({usbboot + sub + "config.txt",              sub + "config.txt"});
            const std::string pieepromUrl = eepromVersion
                ? eeprom + "firmware-2712/latest/pieeprom-" + *eepromVersion + ".bin"
                : std::string();
            entries.push_back({pieepromUrl, sub + "pieeprom.original.bin"});
            // recovery.bin in rpi-eeprom corresponds to what usbboot used
            // to ship as recovery.original.bin — same content role (the
            // unsigned baseline binary that gets counter-signed and used
            // as the bootloader the device runs to write the EEPROM).
            entries.push_back({eeprom + "firmware-2712/latest/recovery.bin",
                               sub + "recovery.original.bin"});
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

    // 1a. For SBR, resolve the latest rpi-eeprom version up-front.  The
    // EEPROM payload (pieeprom-DATE.bin / recovery.bin) lives in dated
    // files under firmware-271X/latest/ in the rpi-eeprom repo, so the
    // newest version date must be fetched from versions.txt before we
    // can construct the manifest URLs.  Tolerate network failure by
    // falling back to whatever date the sidecar from a previous run
    // recorded — this preserves offline behaviour.
    std::optional<std::string> eepromVersion;
    if (mode == SideloadMode::SecureBootRecovery) {
        eepromVersion = resolveLatestEepromVersion(chip, cancelled);
        if (!eepromVersion) {
            const std::string sub = (chip == ChipGeneration::BCM2712)
                                        ? "secure-boot-recovery5"
                                        : "secure-boot-recovery";
            auto sidecar = versionDir / sub / ".eeprom-version";
            if (std::filesystem::exists(sidecar)) {
                std::ifstream in(sidecar);
                std::string cached;
                if (std::getline(in, cached) && !cached.empty()) {
                    qWarning() << "FirmwareManager: rpi-eeprom version fetch failed,"
                                  "falling back to cached version"
                               << QString::fromStdString(cached);
                    eepromVersion = std::move(cached);
                }
            }
        }
    }

    // 1. Build the file manifest
    auto manifest = buildManifest(mode, chip, eepromVersion);
    if (manifest.empty()) {
        _lastError = "No firmware files defined for this device";
        return {};
    }

    // 1a-ii. SBR cache invalidation: if the resolved version differs from
    // the persisted sidecar, the cached pieeprom/recovery binaries are
    // stale and must be re-downloaded.  Remove them so the cache check
    // below treats them as missing.
    if (mode == SideloadMode::SecureBootRecovery && eepromVersion) {
        const std::string sub = (chip == ChipGeneration::BCM2712)
                                    ? "secure-boot-recovery5"
                                    : "secure-boot-recovery";
        auto sidecar = versionDir / sub / ".eeprom-version";
        std::string cached;
        if (std::filesystem::exists(sidecar)) {
            std::ifstream in(sidecar);
            std::getline(in, cached);
        }
        if (cached != *eepromVersion) {
            std::error_code purgeEc;
            std::filesystem::remove(versionDir / sub / "pieeprom.original.bin", purgeEc);
            if (chip == ChipGeneration::BCM2712)
                std::filesystem::remove(versionDir / sub / "recovery.original.bin", purgeEc);
            qDebug() << "FirmwareManager: rpi-eeprom version changed"
                     << QString::fromStdString(cached) << "→"
                     << QString::fromStdString(*eepromVersion)
                     << "— purged stale EEPROM payload";
        }
    }

    // 1b. Fastboot boot.img/boot.sig are derived per-run, not persisted across
    // runs.  Always purge them up-front so that:
    //   - a previous run's custom gadget never leaks into the next run (the
    //     custom-gadget UI is session-only — persisting it on disk would be a
    //     UI/state mismatch);
    //   - the signature is always regenerated from the *current* boot.img
    //     bytes, never inherited from a different binary;
    //   - copy_file below always creates a fresh destination (no stale-cache
    //     edge cases on Windows).
    // The cached upstream gadget lives under its own name (fastboot-gadget.img)
    // and remains intact, so this isn't a network re-fetch — just a copy.
    if (mode == SideloadMode::Fastboot) {
        std::error_code purgeEc;
        std::filesystem::remove(versionDir / "fastboot" / "boot.img", purgeEc);
        std::filesystem::remove(versionDir / "fastboot" / "boot.sig", purgeEc);
    }

    // BCM2711/BCM2712 in Fastboot mode bundle their bootcode inside
    // fastboot/bootfiles.bin (a usbboot firmware TAR):
    //   BCM2711 → 2711/bootcode4.bin inside the TAR
    //   BCM2712 → 2712/bootcode5.bin inside the TAR
    // BCM2836_7 downloads bootcode.bin directly — no extraction needed.
    // Re-extract on every run to avoid stale cached binaries.
    const bool needsBootcodeExtraction =
        (mode == SideloadMode::Fastboot &&
         (chip == ChipGeneration::BCM2711 || chip == ChipGeneration::BCM2712));

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
    // When fastboot-gadget signing is requested, also require boot.sig to be
    // present in the cache (otherwise we need to re-run the post-download
    // signing step below).
    const bool needsGadgetSigning =
        (mode == SideloadMode::Fastboot && !_signFastbootGadgetKey.empty());
    if (needsGadgetSigning && allExist) {
        if (!std::filesystem::exists(versionDir / "fastboot" / "boot.sig"))
            allExist = false;
    }
    // When re-provisioning signing is enabled (any mode), force a cache miss
    // so that the post-download signing steps run.  We don't track which key
    // the cached output was signed with, and re-signing is cheap (~100 ms of
    // RSA + file copies) — much simpler and more correct than introducing
    // sentinel marker files keyed by the user's PEM path.
    const bool needsRecoverySigning =
        (mode == SideloadMode::SecureBootRecovery && !_signFastbootGadgetKey.empty());
    if (needsRecoverySigning) {
        allExist = false;
    }
    if (allExist && !needsBootcodeExtraction) {
        qDebug() << "FirmwareManager: cache hit for master";
        // SBR config.txt overrides are idempotent and cheap — re-apply on
        // cache hit so an older cache from before this helper landed still
        // gets fixed up.
        if (mode == SideloadMode::SecureBootRecovery && !ensureSbrReenumerates(versionDir, chip))
            return {};
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

        // Empty URL means buildManifest couldn't resolve a remote source —
        // typically the rpi-eeprom version lookup failed with no cached
        // sidecar to fall back on.  If the file isn't already present,
        // there's nothing more we can do.
        if (entry.url.empty()) {
            _lastError = "Cannot resolve rpi-eeprom firmware URL for "
                       + entry.localPath
                       + " (offline and nothing cached from a previous run)";
            return {};
        }

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

    // 3a. SBR: persist the resolved rpi-eeprom version into a sidecar so
    // subsequent runs can diff against it and purge stale binaries when
    // a newer version becomes available.  Written here (post-download) so
    // a partial/failed download doesn't leave us claiming a version we
    // don't actually have on disk.
    if (mode == SideloadMode::SecureBootRecovery && eepromVersion) {
        const std::string sub = (chip == ChipGeneration::BCM2712)
                                    ? "secure-boot-recovery5"
                                    : "secure-boot-recovery";
        auto sidecar = versionDir / sub / ".eeprom-version";
        std::error_code sidecarEc;
        std::filesystem::create_directories(sidecar.parent_path(), sidecarEc);
        std::ofstream out(sidecar, std::ios::trunc);
        if (out)
            out << *eepromVersion << '\n';
    }

    // 3b. Materialise fastboot/boot.img for BCM2711/BCM2712 fastboot runs.
    // The active boot.img is rebuilt from scratch on every run (see step 1b):
    //   - if the user provided a custom gadget, copy from that file;
    //   - otherwise, copy from the cached upstream fastboot-gadget.img.
    // BCM2836_7 doesn't ship a separate boot.img (it uses a self-contained
    // bootfiles.bin bundle) — skip cleanly when the manifest doesn't include
    // an upstream gadget and no custom is set.
    if (mode == SideloadMode::Fastboot &&
        (chip == ChipGeneration::BCM2711 || chip == ChipGeneration::BCM2712)) {
        auto gadgetDest = versionDir / "fastboot" / "boot.img";
        std::filesystem::create_directories(gadgetDest.parent_path(), ec);

        std::filesystem::path gadgetSrc;
        if (!_customFastbootGadget.empty()) {
            gadgetSrc = _customFastbootGadget;
            qDebug() << "FirmwareManager: using custom fastboot gadget:"
                     << QString::fromStdString(_customFastbootGadget);
        } else {
            gadgetSrc = versionDir / "fastboot" / "fastboot-gadget.img";
            if (!std::filesystem::exists(gadgetSrc)) {
                _lastError = "Cached fastboot-gadget.img missing — cannot materialise boot.img";
                return {};
            }
        }

        std::error_code copyEc;
        overwriteCopy(gadgetSrc, gadgetDest, copyEc);
        if (copyEc) {
            _lastError = "Failed to copy fastboot gadget to boot.img: " + copyEc.message();
            return {};
        }
    }

    // 3c. Extract the correct bootcode from bootfiles.bin for chips that
    // need it (BCM2711 → bootcode4.bin, BCM2712 → bootcode5.bin).
    //
    // If a re-provisioning run on a previous invocation re-packed
    // bootfiles.bin (with a counter-signed bootcode inside), we kept the
    // upstream tar at bootfiles.bin.original.  Restore from .original
    // before extracting so we always read the unsigned upstream bootcode
    // — extracting from a signed blob and re-signing on top of that
    // would chain-sign and the ROM would reject the result.
    if (needsBootcodeExtraction) {
        auto bundlePath     = versionDir / "fastboot" / "bootfiles.bin";
        auto bundleOriginal = versionDir / "fastboot" / "bootfiles.bin.original";
        if (std::filesystem::exists(bundleOriginal)) {
            std::error_code restoreEc;
            overwriteCopy(bundleOriginal, bundlePath, restoreEc);
            if (restoreEc) {
                _lastError = "Failed to restore bootfiles.bin from .original: "
                           + restoreEc.message();
                return {};
            }
        }
        if (!extractBootcodeFromBootfiles(versionDir, chip))
            return {};  // _lastError set by helper
    }

    // 3c+. Counter-sign the bootcode that's about to be uploaded to the boot
    // ROM, when the user has enabled re-provisioning mode and we're talking
    // to a chip whose ROM enforces customer counter-signing on the
    // second-stage bootcode.
    //
    // - BCM2712 (CM5/Pi 5): the ROM verifies the uploaded bootcode against
    //   the OTP customer key hash before executing it.  Without this step,
    //   the ROM silently rejects the bootcode and the device never
    //   re-enumerates → "waiting for re-enumerate" timeout.  Format:
    //   [bootcode | u32_le len | u32_le keynum=16 | u32_le version=0 |
    //   256-byte RSA-2048 PKCS#1v1.5 SHA-256 sig | 264-byte pubkey].
    // - BCM2711 (CM4/Pi 4): per the secure-boot-recovery README, fused
    //   CM4 does not require the second-stage bootcode to be counter-signed
    //   — only the EEPROM (bootconf.sig + pubkey embedded in pieeprom.bin)
    //   is verified.  rpi-sign-bootcode does support a -c 2711 mode (HMAC +
    //   RSA-SHA1, with a separate HMAC key) but it's not part of the
    //   re-provisioning flow.  Skip silently for BCM2711.
    //
    // Applies to both Fastboot and SecureBootRecovery on BCM2712: in both
    // cases the bootcode lives at versionDir/bootcode5.bin (Fastboot
    // extracts from bootfiles.bin; SecureBootRecovery downloads it directly
    // — both targets are identical content per the upstream symlink).  The
    // BootcodeLoader reads from this exact path.
    if (chip == ChipGeneration::BCM2712 &&
        !_signFastbootGadgetKey.empty() &&
        (mode == SideloadMode::Fastboot ||
         mode == SideloadMode::SecureBootRecovery)) {
        auto bootcodePath = versionDir / "bootcode5.bin";

        // For SecureBootRecovery the cached bootcode5.bin may already be a
        // signed blob from a previous run.  Restore from the unsigned source
        // (recovery.original.bin in the recovery subdir) before re-signing
        // so we don't sign a signed blob.  Fastboot mode re-extracts on
        // every call already, so its baseline is always clean.
        if (mode == SideloadMode::SecureBootRecovery) {
            auto src = versionDir / "secure-boot-recovery5" / "recovery.original.bin";
            if (!std::filesystem::exists(src)) {
                _lastError = "recovery.original.bin missing — cannot re-baseline bootcode5.bin";
                return {};
            }
            std::error_code copyEc;
            overwriteCopy(src, bootcodePath, copyEc);
            if (copyEc) {
                _lastError = "Failed to refresh bootcode5.bin from recovery.original.bin: "
                           + copyEc.message();
                return {};
            }
        }

        QFile bf(QString::fromStdString(bootcodePath.string()));
        if (!bf.open(QIODevice::ReadOnly)) {
            _lastError = "Cannot read bootcode5.bin for counter-signing";
            return {};
        }
        QByteArray bootcode = bf.readAll();
        bf.close();
        QByteArray signedBootcode = SecureBoot::signBootcode2712(
            bootcode, QString::fromStdString(_signFastbootGadgetKey));
        if (signedBootcode.isEmpty()) {
            _lastError = "Failed to counter-sign bootcode5.bin for re-provisioning";
            return {};
        }
        QFile out(QString::fromStdString(bootcodePath.string()));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            _lastError = "Cannot rewrite bootcode5.bin with counter-signed copy";
            return {};
        }
        out.write(signedBootcode);
        out.close();
        qDebug() << "FirmwareManager: counter-signed bootcode5.bin ("
                 << bootcode.size() << "→" << signedBootcode.size() << "bytes)";

        // 3c++. ALSO splice the counter-signed bootcode into the
        // bootfiles.bin tar we serve via the file_server.  The running
        // bootcode (now customer-counter-signed and accepted by ROM)
        // chain-loads the second-stage firmware out of bootfiles.bin and
        // enforces customer signing on the embedded 2712/bootcode5.bin.
        // Without this, the device pulls the unsigned bootcode out of the
        // bundle and resets mid-transfer (typically observed as a partial
        // boot.img bulk write that disconnects after a fixed buffer size).
        // Mirrors the upstream rpi-sb-bootstrap.sh flow that re-signs
        // bootcode5.bin inside bootfiles.bin and re-packs the tar.
        if (mode == SideloadMode::Fastboot) {
            auto bundlePath = versionDir / "fastboot" / "bootfiles.bin";
            auto bundleOriginal = versionDir / "fastboot" / "bootfiles.bin.original";

            // Preserve a pristine copy of the upstream tar on first encounter.
            // This protects against re-signing-an-already-signed blob across
            // repeated runs; we always re-baseline from .original below.
            std::error_code preserveEc;
            if (!std::filesystem::exists(bundleOriginal)) {
                std::filesystem::copy_file(bundlePath, bundleOriginal, preserveEc);
                if (preserveEc) {
                    _lastError = "Failed to preserve bootfiles.bin.original: "
                               + preserveEc.message();
                    return {};
                }
            }

            // Read the pristine tar, splice in the signed bootcode, write
            // back to bootfiles.bin (overwriting any previous-run output).
            Bootfiles bundle;
            if (!bundle.extractFromFile(bundleOriginal.string())) {
                _lastError = "Cannot read bootfiles.bin.original: " + bundle.lastError();
                return {};
            }
            std::vector<uint8_t> signedBootcodeVec(
                signedBootcode.constData(),
                signedBootcode.constData() + signedBootcode.size());
            // The 2712 chip's bootcode lives under "2712/bootcode5.bin"
            // in the upstream tar.  Try both with and without the chip
            // prefix to tolerate any future repackaging.
            const std::string targetEntry = "2712/bootcode5.bin";
            if (!bundle.replaceEntry(targetEntry, signedBootcodeVec)) {
                _lastError = "Cannot replace " + targetEntry + " in bootfiles.bin: "
                           + bundle.lastError();
                return {};
            }
            if (!bundle.writeToFile(bundlePath.string())) {
                _lastError = "Cannot write re-packed bootfiles.bin: " + bundle.lastError();
                return {};
            }
            qDebug() << "FirmwareManager: re-packed fastboot/bootfiles.bin with"
                     << "counter-signed" << targetEntry.c_str();
        }
    }

    // 3d. If gadget signing is requested, generate fastboot/boot.sig from the
    // boot.img that's now on disk (custom or downloaded).  Only meaningful for
    // fastboot mode, and only for chips that ship a separate boot.img
    // (BCM2711/BCM2712).  BCM2836_7 uses a self-contained bootfiles.bin
    // bundle and there's no boot.img to sign.
    if (needsGadgetSigning) {
        auto bootImg = versionDir / "fastboot" / "boot.img";
        if (!std::filesystem::exists(bootImg)) {
            qDebug() << "FirmwareManager: skipping gadget signing — no boot.img for this chip";
        } else {
            auto bootSig = versionDir / "fastboot" / "boot.sig";
            qDebug() << "FirmwareManager: signing fastboot gadget with key"
                     << QString::fromStdString(_signFastbootGadgetKey);
            if (!SecureBoot::generateBootSig(QString::fromStdString(bootImg.string()),
                                              QString::fromStdString(_signFastbootGadgetKey),
                                              QString::fromStdString(bootSig.string()))) {
                _lastError = "Failed to sign fastboot gadget (boot.img). "
                             "Check that the RSA private key is valid (PEM, RSA-2048).";
                return {};
            }
        }
    }

    // 3e. If SecureBootRecovery signing is requested, prepare the signed
    // pieeprom.bin (with embedded bootconf.txt + bootconf.sig + pubkey.bin)
    // and counter-signed bootcode5.bin needed by an already-fused CM5/CM4.
    if (needsRecoverySigning) {
        const std::string sub = (chip == ChipGeneration::BCM2712)
                                    ? "secure-boot-recovery5"
                                    : "secure-boot-recovery";
        auto recoveryDir = versionDir / sub;
        std::string err;
        const bool counterSign = (chip == ChipGeneration::BCM2712);
        qDebug() << "FirmwareManager: preparing signed recovery firmware in"
                 << QString::fromStdString(recoveryDir.string());
        if (!SecureBootProvisioner::prepareSignedRecovery(
                chip, recoveryDir, _signFastbootGadgetKey, counterSign, err)) {
            _lastError = "Failed to prepare signed recovery firmware: " + err;
            return {};
        }
    }

    // 4. Validate cache
    if (!validateCacheForDevice(versionDir, mode, chip)) {
        _lastError = "Downloaded firmware does not contain required files";
        std::filesystem::remove_all(versionDir, ec);
        return {};
    }

    // 5. SBR: rewrite recovery config.txt so the device re-enumerates back
    // into rpiboot after writing the EEPROM.  Without this, the device boots
    // into the OS post-recovery and our SBR scanner would time out.
    if (mode == SideloadMode::SecureBootRecovery && !ensureSbrReenumerates(versionDir, chip))
        return {};

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

    // All supported chips have a bootcode file on disk:
    //   BCM2836_7 → bootcode.bin (downloaded from usbboot/msd/)
    //   BCM2711   → bootcode4.bin (extracted from fastboot/bootfiles.bin TAR)
    //   BCM2712   → bootcode5.bin (extracted from fastboot/bootfiles.bin TAR)
    {
        std::error_code ec;
        auto bootcodeFile = versionDir / BootcodeLoader::bootcodeFilename(chip);
        auto bootcodeSize = std::filesystem::file_size(bootcodeFile, ec);
        if (ec || bootcodeSize == 0)
            return false;
    }

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
// For BCM2711/BCM2712 in Fastboot mode, bootcode must come from inside
// the firmware/bootfiles.bin TAR archive.  The archive stores chip-specific
// files under directory prefixes (e.g. "2711/bootcode4.bin",
// "2712/bootcode5.bin").

bool FirmwareManager::extractBootcodeFromBootfiles(const std::filesystem::path& versionDir,
                                                    ChipGeneration chip)
{
    auto tarPath = versionDir / "fastboot" / "bootfiles.bin";

    Bootfiles bootfiles;
    if (!bootfiles.extractFromFile(tarPath.string())) {
        _lastError = "Cannot read fastboot/bootfiles.bin: " + bootfiles.lastError();
        return false;
    }

    auto bootcodeFilename = BootcodeLoader::bootcodeFilename(chip);
    auto prefix = std::string(chipDirectoryPrefix(chip));

    // Try chip-prefixed path first (e.g. "2711/bootcode4.bin"),
    // then fall back to the bare filename at the TAR root.
    const auto* data = bootfiles.find(prefix + "/" + bootcodeFilename);
    if (!data || data->empty())
        data = bootfiles.find(bootcodeFilename);
    if (!data || data->empty()) {
        _lastError = bootcodeFilename + " not found inside fastboot/bootfiles.bin "
                     "(tried " + prefix + "/" + bootcodeFilename + " and " + bootcodeFilename + ")";
        return false;
    }

    auto destPath = versionDir / bootcodeFilename;
    std::ofstream out(destPath, std::ios::binary);
    if (!out) {
        _lastError = "Cannot write " + bootcodeFilename + ": " + destPath.string();
        return false;
    }
    out.write(reinterpret_cast<const char*>(data->data()),
              static_cast<std::streamsize>(data->size()));
    if (!out) {
        _lastError = "Write failed for " + bootcodeFilename;
        return false;
    }

    qDebug() << "FirmwareManager: extracted" << bootcodeFilename.c_str()
             << "(" << data->size() << "bytes) from fastboot/bootfiles.bin";
    return true;
}

// ── ensureSbrReenumerates ───────────────────────────────────────────────

bool FirmwareManager::ensureSbrReenumerates(const std::filesystem::path& versionDir,
                                              ChipGeneration chip)
{
    const std::string sub = (chip == ChipGeneration::BCM2712)
                                ? "secure-boot-recovery5"
                                : "secure-boot-recovery";
    auto configPath = versionDir / sub / "config.txt";
    if (!std::filesystem::exists(configPath)) {
        // First-run case: file not downloaded yet.  Caller invokes us again
        // after the download loop, so this is a benign no-op.
        return true;
    }

    std::ifstream in(configPath);
    if (!in) {
        _lastError = "Cannot read SBR config.txt: " + configPath.string();
        return false;
    }

    // Strip any pre-existing set_boot_order= or recovery_reboot= lines.
    // We re-append them in the required order at the end of the file so
    // we don't have to reason about the section the upstream put them in
    // (config.txt is processed top-to-bottom; recovery_reboot must be
    // reached *after* set_boot_order has been observed, otherwise the
    // bootloader reboots before applying our boot-order override and the
    // device powers up into normal boot instead of back into rpiboot).
    auto isOverrideKey = [](const std::string& line) {
        auto trimStart = line.find_first_not_of(" \t");
        if (trimStart == std::string::npos)
            return false;
        std::string_view rest(line.data() + trimStart, line.size() - trimStart);
        return rest.starts_with("set_boot_order=") ||
               rest.starts_with("recovery_reboot=");
    };

    std::vector<std::string> kept;
    kept.reserve(64);
    std::string line;
    while (std::getline(in, line)) {
        // Tolerate CRLF input — strip the trailing \r so our re-write is
        // pure LF (matches what the bootloader expects).
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (isOverrideKey(line))
            continue;
        kept.push_back(std::move(line));
    }
    in.close();

    std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        _lastError = "Cannot rewrite SBR config.txt: " + configPath.string();
        return false;
    }
    for (const auto& l : kept)
        out << l << '\n';
    // Order is load-bearing: set_boot_order must precede recovery_reboot.
    out << "set_boot_order=0x3\n";
    out << "recovery_reboot=1\n";
    if (!out) {
        _lastError = "Write failed on SBR config.txt: " + configPath.string();
        return false;
    }
    qDebug() << "FirmwareManager: rewrote" << QString::fromStdString(configPath.string())
             << "with set_boot_order=0x3 + recovery_reboot=1"
             << "(kept" << kept.size() << "upstream line(s))";
    return true;
}

// ── resolveLatestEepromVersion ──────────────────────────────────────────

std::optional<std::string> FirmwareManager::resolveLatestEepromVersion(
    ChipGeneration chip, std::atomic<bool>& cancelled)
{
    const std::string firmwareDir = (chip == ChipGeneration::BCM2712)
                                        ? "firmware-2712"
                                        : "firmware-2711";
    const std::string url = std::string(EEPROM_RAW_BASE) + firmwareDir + "/versions.txt";

    // Cache the tiny metadata file under the cache root so it can be
    // diffed against the persisted sidecar on the next run.
    auto tmpPath = cacheRoot() / "master" / (firmwareDir + "-versions.txt");
    std::error_code mkdirEc;
    std::filesystem::create_directories(tmpPath.parent_path(), mkdirEc);

    if (!downloadFile(url, tmpPath, nullptr, cancelled)) {
        // Caller falls back to the version sidecar from a previous run.
        // Don't overwrite _lastError — it's the caller's decision how to
        // surface this (it's not a hard failure if a cached version exists).
        return std::nullopt;
    }

    std::ifstream in(tmpPath);
    if (!in) {
        return std::nullopt;
    }
    // Skip comment/blank lines; the first data row is the newest version
    // (the file is "Sorted newest-first" per its own header).
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#')
            continue;
        std::istringstream iss(line);
        std::string version;
        if (iss >> version && !version.empty()) {
            qDebug() << "FirmwareManager: resolved latest"
                     << QString::fromStdString(firmwareDir)
                     << "version:" << QString::fromStdString(version);
            return version;
        }
    }
    return std::nullopt;
}

} // namespace rpiboot
