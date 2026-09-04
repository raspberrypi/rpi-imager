// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// FirmwareManager decides which firmware a board needs and whether what is
// already cached can be trusted for it. Getting the second question wrong is
// the expensive one: handing back a cache directory that is missing a file,
// or was populated for a different chip, sideloads the wrong firmware to a
// Compute Module. None of it was covered.
//
// The public way in is ensureAvailable(), which downloads first, so the cache
// logic underneath was unreachable from a test. Those helpers are now
// protected rather than private -- no change to the public surface -- and the
// subclass below reaches them directly. Everything here is filesystem work
// against a scratch cache; nothing contacts the network.

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/firmware_manager.h"
#include "rpiboot/rpiboot_types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QUuid>

#include <atomic>
#include <vector>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "fixture_process.h"
#include <sstream>

namespace fs = std::filesystem;

using rpiboot::ChipGeneration;
using rpiboot::FirmwareManager;
using rpiboot::SideloadMode;

namespace {

// Exposes the cache helpers. The production class keeps them protected; a
// test is the one caller that legitimately wants at them without going
// through a download.
class TestableFirmwareManager : public FirmwareManager
{
public:
    using FirmwareManager::buildManifest;
    using FirmwareManager::downloadFile;
    using FirmwareManager::extractBootcodeFromBootfiles;
    using FirmwareManager::findCachedVersion;
    using FirmwareManager::selectLatestVersion;
    using FirmwareManager::ManifestEntry;
    using FirmwareManager::validateCacheForDevice;
};

class ScratchDir
{
public:
    ScratchDir()
        : _path(QDir::temp().filePath(QStringLiteral("rpi-imager-fw-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))))
    {
        QDir().mkpath(_path);
    }
    ~ScratchDir() { QDir(_path).removeRecursively(); }

    ScratchDir(const ScratchDir &) = delete;
    ScratchDir &operator=(const ScratchDir &) = delete;

    fs::path path() const { return fs::path(_path.toStdString()); }

private:
    QString _path;
};

void writeFile(const fs::path &p, const std::string &contents)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << contents;
}


// Build a cache directory that satisfies validateCacheForDevice().
//
// The contract is not "the manifest files are present": it wants the chip's
// bootcode -- bootcode4.bin for BCM2711, bootcode5.bin for BCM2712, which is
// extracted from the fastboot bootfiles TAR rather than downloaded directly
// -- to exist and be non-empty, plus the subdirectory for the mode.
const char *bootcodeFor(ChipGeneration chip)
{
    switch (chip) {
    case ChipGeneration::BCM2711: return "bootcode4.bin";
    case ChipGeneration::BCM2712: return "bootcode5.bin";
    default: return "bootcode.bin";
    }
}

void populateValidCache(const fs::path &versionDir, SideloadMode mode, ChipGeneration chip)
{
    fs::create_directories(versionDir);
    writeFile(versionDir / bootcodeFor(chip), "bootcode bytes");
    if (mode == SideloadMode::Fastboot)
        fs::create_directories(versionDir / "fastboot");
    else
        fs::create_directories(versionDir / "secure-boot-recovery5");
}

} // namespace

// ---------------------------------------------------------------------------
// Cache location
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager has a cache root inside the user's cache area",
          "[firmware]")
{
    TestableFirmwareManager fm;
    const fs::path root = fm.cacheRoot();

    // An empty or root-relative path here would have clearCache() deleting
    // something other than the firmware cache.
    CHECK_FALSE(root.empty());
    CHECK(root.is_absolute());
    CHECK(root.string().find("rpi") != std::string::npos);
}

TEST_CASE("FirmwareManager starts with no error", "[firmware]")
{
    TestableFirmwareManager fm;
    CHECK(fm.lastError().empty());
}

// ---------------------------------------------------------------------------
// Manifests
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager builds a fastboot manifest", "[firmware]")
{
    TestableFirmwareManager fm;

    const auto manifest = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);

    REQUIRE_FALSE(manifest.empty());
    for (const auto &entry : manifest) {
        INFO("url: " << entry.url << " -> " << entry.localPath);
        // Every entry needs somewhere to come from and somewhere to go; a
        // blank either side means a file silently never arrives.
        CHECK_FALSE(entry.url.empty());
        CHECK_FALSE(entry.localPath.empty());
        // localPath is documented as relative to the version directory, so an
        // absolute one would escape the cache.
        CHECK_FALSE(fs::path(entry.localPath).is_absolute());
        CHECK(entry.localPath.find("..") == std::string::npos);
    }
}

TEST_CASE("FirmwareManager downloads the same fastboot payload for either chip",
          "[firmware]")
{
    TestableFirmwareManager fm;

    const auto for2711 = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2711);
    const auto for2712 = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);

    REQUIRE_FALSE(for2711.empty());
    REQUIRE_FALSE(for2712.empty());

    // The manifest is deliberately chip-independent here: both CM4 and CM5
    // pull the same fastboot bootfiles.bin, and the chip only decides which
    // member is pulled out of that TAR afterwards -- bootcode4.bin for
    // BCM2711, bootcode5.bin for BCM2712. So the download list matching is
    // the correct answer, and validateCacheForDevice() is what actually keeps
    // one chip from using the other's extracted bootcode.
    REQUIRE(for2711.size() == for2712.size());
    for (std::size_t i = 0; i < for2711.size(); ++i) {
        INFO("entry " << i);
        CHECK(for2711[i].url == for2712[i].url);
        CHECK(for2711[i].localPath == for2712[i].localPath);
    }
}

TEST_CASE("FirmwareManager builds a secure-boot recovery manifest", "[firmware]")
{
    TestableFirmwareManager fm;

    const auto manifest = fm.buildManifest(SideloadMode::SecureBootRecovery,
                                           ChipGeneration::BCM2712,
                                           std::optional<std::string>("2026-05-22"));

    REQUIRE_FALSE(manifest.empty());

    // The dated EEPROM build has to appear in the URLs, or the wrong
    // firmware version is fetched for an OTP operation.
    std::string all;
    for (const auto &e : manifest) all += e.url + " ";
    INFO("urls: " << all);
    CHECK(all.find("2026-05-22") != std::string::npos);
}

TEST_CASE("FirmwareManager builds a recovery manifest without a version", "[firmware]")
{
    TestableFirmwareManager fm;

    // std::nullopt means "offline": fall back to whatever is cached. It must
    // still produce a manifest rather than throwing or returning nothing.
    const auto manifest = fm.buildManifest(SideloadMode::SecureBootRecovery,
                                           ChipGeneration::BCM2712, std::nullopt);
    CHECK_FALSE(manifest.empty());
}

TEST_CASE("FirmwareManager manifests differ between modes", "[firmware]")
{
    TestableFirmwareManager fm;

    const auto fastboot = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);
    const auto recovery = fm.buildManifest(SideloadMode::SecureBootRecovery,
                                           ChipGeneration::BCM2712,
                                           std::optional<std::string>("2026-05-22"));

    std::string a, b;
    for (const auto &e : fastboot) a += e.localPath + " ";
    for (const auto &e : recovery) b += e.localPath + " ";
    INFO("fastboot: " << a);
    INFO("recovery: " << b);
    CHECK(a != b);
}

// ---------------------------------------------------------------------------
// Trusting what is already on disk
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager rejects a cache directory that is not there", "[firmware]")
{
    TestableFirmwareManager fm;

    CHECK_FALSE(fm.validateCacheForDevice(fs::path("/nonexistent-rpi-imager/version"),
                                          SideloadMode::Fastboot, ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager rejects an empty cache directory", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "empty";
    fs::create_directories(versionDir);

    TestableFirmwareManager fm;

    // The directory exists but holds none of the files the manifest asks
    // for. Accepting it would sideload nothing and hang waiting for a device
    // that never comes up.
    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager rejects a cache missing one file", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "partial";
    fs::create_directories(versionDir);

    TestableFirmwareManager fm;
    const auto manifest = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);
    REQUIRE(manifest.size() > 1);

    // Everything the manifest wants except the last entry: an interrupted
    // download leaves exactly this state, and it must not be trusted.
    for (std::size_t i = 0; i + 1 < manifest.size(); ++i)
        writeFile(versionDir / manifest[i].localPath, "firmware bytes");

    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager rejects a cache of empty files", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "zerolen";
    fs::create_directories(versionDir);

    TestableFirmwareManager fm;
    const auto manifest = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);
    REQUIRE_FALSE(manifest.empty());

    // Zero-length files are what a download that failed at connect time
    // leaves behind. Present-but-empty must not count as cached.
    for (const auto &entry : manifest)
        writeFile(versionDir / entry.localPath, "");

    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager accepts a complete cache", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "complete";
    populateValidCache(versionDir, SideloadMode::Fastboot, ChipGeneration::BCM2712);

    TestableFirmwareManager fm;

    // The control case: with the chip's bootcode present and non-empty and
    // the mode's subdirectory in place, the cache is usable -- so a rejection
    // elsewhere means something was genuinely missing rather than the check
    // refusing everything.
    INFO("last error: " << fm.lastError());
    CHECK(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                    ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager accepts a complete secure-boot recovery cache", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "recovery";
    populateValidCache(versionDir, SideloadMode::SecureBootRecovery, ChipGeneration::BCM2712);

    TestableFirmwareManager fm;
    CHECK(fm.validateCacheForDevice(versionDir, SideloadMode::SecureBootRecovery,
                                    ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager rejects a cache whose bootcode is zero-length", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "emptyboot";
    populateValidCache(versionDir, SideloadMode::Fastboot, ChipGeneration::BCM2712);
    // Truncate the bootcode: what an interrupted extraction leaves behind.
    writeFile(versionDir / bootcodeFor(ChipGeneration::BCM2712), "");

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager rejects a cache missing the mode subdirectory", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "nomode";
    fs::create_directories(versionDir);
    writeFile(versionDir / bootcodeFor(ChipGeneration::BCM2712), "bootcode bytes");
    // Bootcode present, but no fastboot/ directory: the gadget itself is
    // missing, so the sideload would get partway and stall.

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager does not accept a fastboot cache for recovery", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "modemix";
    populateValidCache(versionDir, SideloadMode::Fastboot, ChipGeneration::BCM2712);

    TestableFirmwareManager fm;
    // Same chip, same bootcode, but the recovery payload is not there.
    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::SecureBootRecovery,
                                          ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager does not accept one chip's cache for another", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "wrongchip";

    // A cache populated for a CM4...
    populateValidCache(versionDir, SideloadMode::Fastboot, ChipGeneration::BCM2711);

    TestableFirmwareManager fm;
    REQUIRE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                      ChipGeneration::BCM2711));

    // ...must not be handed to a CM5. Sideloading bootcode4 to a BCM2712 is
    // exactly what this check exists to prevent.
    CHECK_FALSE(fm.validateCacheForDevice(versionDir, SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712));
}

// ---------------------------------------------------------------------------
// Looking for something already cached
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager finds nothing in an unpopulated cache", "[firmware]")
{
    TestableFirmwareManager fm;

    // Whatever is or is not in the real cache directory, this must answer
    // rather than throw -- it runs before every sideload.
    CHECK_NOTHROW(fm.findCachedVersion(SideloadMode::Fastboot, ChipGeneration::BCM2712));
    CHECK_NOTHROW(fm.findCachedVersion(SideloadMode::SecureBootRecovery,
                                       ChipGeneration::BCM2711));
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager remembers a custom fastboot gadget", "[firmware]")
{
    TestableFirmwareManager fm;
    CHECK(fm.customFastbootGadget().empty());

    fm.setCustomFastbootGadget("/tmp/my-gadget.bin");
    CHECK(fm.customFastbootGadget() == "/tmp/my-gadget.bin");
}

TEST_CASE("FirmwareManager remembers a gadget signing key", "[firmware]")
{
    TestableFirmwareManager fm;
    CHECK(fm.signFastbootGadgetKey().empty());

    fm.setSignFastbootGadgetKey("/tmp/key.pem");
    CHECK(fm.signFastbootGadgetKey() == "/tmp/key.pem");
}

TEST_CASE("FirmwareManager clearing an already-empty cache is harmless", "[firmware]")
{
    TestableFirmwareManager fm;

    // Runs when the user asks for a fresh download; it must cope with there
    // being nothing there, and must not throw from a UI callback.
    CHECK_NOTHROW(fm.clearCache());
    CHECK_NOTHROW(fm.clearCache());
}

// ---------------------------------------------------------------------------
// Extracting bootcode from the bootfiles TAR
// ---------------------------------------------------------------------------
//
// The fastboot payload arrives as bootfiles.bin, a TAR, and the chip decides
// which member is pulled out of it: bootcode4.bin for a CM4, bootcode5.bin
// for a CM5. Pulling the wrong one -- or reporting success when the archive
// does not contain it -- sideloads firmware the board cannot run.

namespace {

bool haveTar() { return QFileInfo::exists(QStringLiteral("/usr/bin/tar")) ||
                        QFileInfo::exists(QStringLiteral("/bin/tar")); }

QString tarPath()
{
    return QFileInfo::exists(QStringLiteral("/usr/bin/tar")) ? QStringLiteral("/usr/bin/tar")
                                                             : QStringLiteral("/bin/tar");
}

// Build versionDir/fastboot/bootfiles.bin as a TAR holding the named members.
bool buildBootfilesTar(const fs::path &versionDir, const std::vector<std::string> &members)
{
    const fs::path staging = versionDir / "staging";
    fs::create_directories(staging);
    fs::create_directories(versionDir / "fastboot");

    QStringList args{QStringLiteral("-cf"),
                     QString::fromStdString((versionDir / "fastboot" / "bootfiles.bin").string()),
                     QStringLiteral("-C"), QString::fromStdString(staging.string())};
    for (const auto &m : members) {
        writeFile(staging / m, std::string(4096, 'F'));
        args << QString::fromStdString(m);
    }

    QProcess tar;
    tar.start(tarPath(), args);
    tar.waitForFinished(rpi_test::kFixtureProcessTimeoutMs);
    fs::remove_all(staging);
    return tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0;
}

} // namespace

TEST_CASE("FirmwareManager extracts the bootcode for the chip", "[firmware]")
{
    if (!haveTar())
        SKIP("tar is not installed, so no bootfiles archive can be built");

    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "extract";
    REQUIRE(buildBootfilesTar(versionDir, {"bootcode4.bin", "bootcode5.bin", "config.txt"}));

    TestableFirmwareManager fm;
    REQUIRE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));

    // The CM5 member and nothing else.
    const fs::path extracted = versionDir / "bootcode5.bin";
    REQUIRE(fs::exists(extracted));
    CHECK(fs::file_size(extracted) == 4096);
}

TEST_CASE("FirmwareManager extracts the other chip's bootcode", "[firmware]")
{
    if (!haveTar())
        SKIP("tar is not installed, so no bootfiles archive can be built");

    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "extract2711";
    REQUIRE(buildBootfilesTar(versionDir, {"bootcode4.bin", "bootcode5.bin"}));

    TestableFirmwareManager fm;
    REQUIRE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2711));
    CHECK(fs::exists(versionDir / "bootcode4.bin"));
}

TEST_CASE("FirmwareManager reports a bootfiles archive without the member", "[firmware]")
{
    if (!haveTar())
        SKIP("tar is not installed, so no bootfiles archive can be built");

    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "wrongmember";
    // A well-formed archive that simply does not carry the CM5 bootcode.
    REQUIRE(buildBootfilesTar(versionDir, {"bootcode4.bin", "config.txt"}));

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));
    CHECK_FALSE(fs::exists(versionDir / "bootcode5.bin"));
}

TEST_CASE("FirmwareManager reports a missing bootfiles archive", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "noarchive";
    fs::create_directories(versionDir / "fastboot");

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));
}

TEST_CASE("FirmwareManager reports a corrupt bootfiles archive", "[firmware]")
{
    ScratchDir scratch;
    const fs::path versionDir = scratch.path() / "corruptarchive";
    fs::create_directories(versionDir / "fastboot");
    // Not a TAR at all: what a truncated or HTML-error-page download leaves.
    writeFile(versionDir / "fastboot" / "bootfiles.bin", "<html>404 Not Found</html>");

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));
}

// ---------------------------------------------------------------------------
// Downloading
// ---------------------------------------------------------------------------

TEST_CASE("FirmwareManager reports a download from nowhere", "[firmware]")
{
    ScratchDir scratch;
    std::atomic<bool> cancelled{false};

    TestableFirmwareManager fm;
    // Nothing listens on port 1, so this fails at connect rather than
    // hanging. A firmware file that silently did not arrive would be
    // sideloaded as a zero-length blob.
    CHECK_FALSE(fm.downloadFile("http://127.0.0.1:1/bootfiles.bin",
                                scratch.path() / "out.bin", nullptr, cancelled));
    CHECK_FALSE(fm.lastError().empty());
}

TEST_CASE("FirmwareManager reports a download it cannot write", "[firmware]")
{
    std::atomic<bool> cancelled{false};

    TestableFirmwareManager fm;
    CHECK_FALSE(fm.downloadFile("http://127.0.0.1:1/x.bin",
                                fs::path("/nonexistent-rpi-imager-dir/deeper/out.bin"),
                                nullptr, cancelled));
}

TEST_CASE("FirmwareManager honours cancellation before a download", "[firmware]")
{
    ScratchDir scratch;
    std::atomic<bool> cancelled{true};

    TestableFirmwareManager fm;
    // Already cancelled: the user pressed stop while the manifest was being
    // walked. It must not start the transfer.
    CHECK_FALSE(fm.downloadFile("http://127.0.0.1:1/x.bin",
                                scratch.path() / "cancelled.bin", nullptr, cancelled));
}

// ── Choosing which EEPROM version to fetch ──────────────────────────────────
//
// rpi-eeprom publishes a versions.txt listing every build, newest first, with
// a release column. The imager downloads from the latest/ channel directory,
// which is not guaranteed to still hold builds marked "old" -- so choosing an
// archived row produces a URL that 404s, the firmware fetch fails, and the
// Compute Module is left without the EEPROM image it was being set up with.
//
// Columns: version  build_epoch  fw_git_hash  release  [mfg_ver]

namespace {

std::optional<std::string> pick(const std::string &versionsTxt)
{
    std::istringstream in(versionsTxt);
    return TestableFirmwareManager::selectLatestVersion(in, "firmware-2712");
}

} // namespace

TEST_CASE("The newest usable version is chosen", "[firmware][versions]")
{
    const auto got = pick(
        "2024-09-23  1727086800  abc123  latest\n"
        "2024-06-05  1717574400  def456  default\n");

    REQUIRE(got.has_value());
    CHECK(*got == "2024-09-23");
}

TEST_CASE("Archived versions are skipped", "[firmware][versions]")
{
    // The newest row is archived, so it must be passed over in favour of the
    // newest row that is still published under latest/.
    const auto got = pick(
        "2024-09-23  1727086800  abc123  old\n"
        "2024-06-05  1717574400  def456  latest\n");

    REQUIRE(got.has_value());
    CHECK(*got == "2024-06-05");
}

TEST_CASE("A run of archived versions is skipped", "[firmware][versions]")
{
    const auto got = pick(
        "2025-01-01  1  a  old\n"
        "2024-12-01  2  b  old\n"
        "2024-11-01  3  c  old\n"
        "2024-06-05  4  d  default\n");

    REQUIRE(got.has_value());
    CHECK(*got == "2024-06-05");
}

TEST_CASE("Comments and blank lines are ignored", "[firmware][versions]")
{
    const auto got = pick(
        "# Sorted newest first\n"
        "\n"
        "#2024-10-01  0  x  latest\n"
        "\n"
        "2024-09-23  1727086800  abc123  latest\n");

    REQUIRE(got.has_value());
    CHECK(*got == "2024-09-23");
}

TEST_CASE("A row with no release column is still usable", "[firmware][versions]")
{
    // The column is read defensively; a shorter row must not be discarded,
    // or a perfectly good version is skipped for a formatting difference.
    const auto got = pick("2024-09-23  1727086800  abc123\n");
    REQUIRE(got.has_value());
    CHECK(*got == "2024-09-23");
}

TEST_CASE("A row of just a version is usable", "[firmware][versions]")
{
    const auto got = pick("2024-09-23\n");
    REQUIRE(got.has_value());
    CHECK(*got == "2024-09-23");
}

TEST_CASE("Ragged whitespace does not change the choice", "[firmware][versions]")
{
    const auto got = pick("   2024-09-23\t1727086800    abc123\t\told  \n"
                          "2024-06-05 1717574400 def456 latest\n");
    REQUIRE(got.has_value());
    CHECK(*got == "2024-06-05");
}

TEST_CASE("Windows line endings do not hide the release column",
          "[firmware][versions]")
{
    // If the trailing \r were read as part of the release column, "old\r"
    // would not match "old" and every archived build would be treated as
    // current -- resolving to a URL that is not there.
    const auto got = pick("2024-09-23  1727086800  abc123  old\r\n"
                          "2024-06-05  1717574400  def456  latest\r\n");
    REQUIRE(got.has_value());
    CHECK(*got == "2024-06-05");
}

TEST_CASE("A listing with nothing usable yields no version", "[firmware][versions]")
{
    // The caller falls back to the sidecar recorded on a previous run, which
    // it can only do if this reports that it found nothing.
    CHECK_FALSE(pick("").has_value());
    CHECK_FALSE(pick("# only a comment\n").has_value());
    CHECK_FALSE(pick("\n\n\n").has_value());
    CHECK_FALSE(pick("2025-01-01  1  a  old\n2024-12-01  2  b  old\n").has_value());
}
