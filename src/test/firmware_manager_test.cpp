// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

// FirmwareManager decides which firmware a board needs and whether what is
// already cached can be trusted for it. Getting the second question wrong is
// the expensive one: handing back a cache directory that is missing a file,
// or was populated for a different chip, sideloads the wrong firmware to a
// Compute Module. None of it was covered.

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/firmware_manager.h"
#include <QTemporaryDir>
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
#include <set>
#include <optional>
#include <string>

#include "fixture_process.h"
#include "platform_tools.h"
#include <sstream>

#include "curlnetworkconfig.h"
#include "local_http_server.h"
#include "rpiboot/bootfiles.h"

#include <archive.h>
#include <archive_entry.h>

#include <sys/stat.h>
#include <unistd.h>

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
    using FirmwareManager::ensureSbrReenumerates;
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
    // This ran against the real cache root, so it could only assert that
    // nothing threw -- the answer depended on whether the developer had
    // sideloaded recently. Pointed at an empty directory of its own it can
    // say what the name claims, and it stops reading the user's cache.
    class EmptyCacheManager : public TestableFirmwareManager
    {
    public:
        EmptyCacheManager() { REQUIRE(_dir.isValid()); }
        std::filesystem::path cacheRoot() const override
        {
            return std::filesystem::path(_dir.path().toStdString()) / "empty-cache";
        }
    private:
        QTemporaryDir _dir;
    };

    EmptyCacheManager fm;

    CHECK_FALSE(fm.findCachedVersion(SideloadMode::Fastboot,
                                     ChipGeneration::BCM2712).has_value());
    CHECK_FALSE(fm.findCachedVersion(SideloadMode::SecureBootRecovery,
                                     ChipGeneration::BCM2711).has_value());
    CHECK_FALSE(fm.findCachedVersion(SideloadMode::Fastboot,
                                     ChipGeneration::BCM2711).has_value());
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

// ── Fetching firmware, end to end ───────────────────────────────────────────
//
// ensureAvailable() is the largest untested thing in this class: it resolves
// the EEPROM version, builds the manifest, downloads what is missing, caches
// it, and validates what it has. Until now none of it could be reached,
// because the three base URLs were compile-time constants pointing at
// github.com -- so only real network access would do.
//
// They are read through overridable accessors now, which is enough to point
// the whole path at a local server. Nothing about production behaviour
// changes; the defaults are the constants.

namespace {

// Serves a directory tree over HTTP on a loopback port.
class LocalFirmwareServer
{
public:
    explicit LocalFirmwareServer(const QString &root)
    {
        static const char *kScript =
            "import http.server, socketserver, sys\n"
            "class H(http.server.SimpleHTTPRequestHandler):\n"
            "    def log_message(self, *a): pass\n"
            "socketserver.TCPServer.allow_reuse_address = True\n"
            "h = lambda *a, **k: H(*a, directory=sys.argv[1], **k)\n"
            "s = socketserver.TCPServer(('127.0.0.1', 0), h)\n"
            "print(s.server_address[1], flush=True)\n"
            "s.serve_forever()\n";
        _proc.start(QStringLiteral("/usr/bin/python3"),
                    {QStringLiteral("-c"), QString::fromUtf8(kScript), root});
        if (!_proc.waitForStarted(10000))
            return;
        if (_proc.waitForReadyRead(10000))
            _port = _proc.readLine().trimmed().toInt();
    }

    ~LocalFirmwareServer()
    {
        _proc.kill();
        _proc.waitForFinished(5000);
    }

    LocalFirmwareServer(const LocalFirmwareServer &) = delete;
    LocalFirmwareServer &operator=(const LocalFirmwareServer &) = delete;

    bool isRunning() const { return _port > 0; }
    std::string base() const
    {
        return "http://127.0.0.1:" + std::to_string(_port) + "/";
    }

private:
    QProcess _proc;
    int _port = 0;
};

// A FirmwareManager whose three sources are the local server.
// Serves firmware from a local URL and caches into a directory of its own.
//
// The cache root matters as much as the source. These tests call clearCache(),
// which is remove_all() on the root -- against the real cache that is both a
// developer's firmware download deleted by running the suite, and, under
// ctest -j, one test wiping the directory another is renaming its download
// into. That surfaced as an intermittent "Failed to rename downloaded file:
// No such file or directory".
class ServedFirmwareManager : public FirmwareManager
{
public:
    explicit ServedFirmwareManager(std::string base) : _base(std::move(base))
    {
        REQUIRE(_cache.isValid());
    }

    std::filesystem::path cacheRoot() const override
    {
        return std::filesystem::path(_cache.path().toStdString()) / "rpiboot-firmware";
    }

protected:
    std::string usbbootBase() const override { return _base; }
    std::string eepromBase() const override { return _base; }
    std::string provisionerBase() const override { return _base; }

private:
    std::string _base;
    QTemporaryDir _cache;
};

// Lay out the files the fastboot manifest asks for.
void layOutFirmware(const QString &root)
{
    struct Entry { const char *path; const char *body; };
    const Entry entries[] = {
        {"firmware-2711/latest/recovery.bin", "bcm2711 recovery"},
        {"firmware-2712/latest/recovery.bin", "bcm2712 recovery"},
        {"msd/bootcode.bin",                  "msd bootcode"},
        {"mass-storage-gadget64/config.txt",  "gadget config"},
        // firmware/bootfiles.bin is written separately below: it has to be a
        // real tar, because the bootcode is extracted from inside it.
        {"host-support/fastboot-gadget.img",  "fastboot gadget"},
        {"host-support/fastboot-gadget.2710-bootfiles-bin", "2710 bootfiles"},
        {"firmware-2711/versions.txt",        "2024-09-23  1727086800  abc  latest\n"},
        {"firmware-2712/versions.txt",        "2024-09-23  1727086800  abc  latest\n"},
    };
    for (const Entry &e : entries) {
        const QString full = QDir(root).filePath(QString::fromLatin1(e.path));
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(e.body);
        f.close();
    }

    // A genuine tar carrying the chip-specific bootcode, which is what
    // extractBootcodeFromBootfiles() reaches into.
    const QString workDir = QDir(root).filePath(QStringLiteral("_tarwork"));
    QDir().mkpath(workDir + QStringLiteral("/2712"));
    {
        QFile f(workDir + QStringLiteral("/2712/bootcode5.bin"));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("bcm2712 bootcode from the archive");
        f.close();
    }
    QDir().mkpath(QDir(root).filePath(QStringLiteral("firmware")));
    QProcess tar;
    tar.setWorkingDirectory(workDir);
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-cf"),
               QDir(root).filePath(QStringLiteral("firmware/bootfiles.bin")),
               QStringLiteral("2712")});
    REQUIRE(tar.waitForFinished(rpi_test::kFixtureProcessTimeoutMs));
    REQUIRE(tar.exitCode() == 0);
}

} // namespace

TEST_CASE("Firmware is fetched and cached from the configured source",
          "[firmware][fetch]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    INFO("dir: " << dir.string());
    REQUIRE_FALSE(dir.empty());
    CHECK(std::filesystem::exists(dir));

    // A second call is served from the cache rather than fetched again.
    //
    // Comparing the two paths does not show that: the path is derived from
    // the mode and the chip, so it is the same whether the second call read
    // the cache or downloaded everything over again. Taking the source away
    // first is what distinguishes them -- a fetch now has nothing to fetch.
    const QDir servedDir(served.path());
    for (const QFileInfo &fi : servedDir.entryInfoList(QDir::Dirs | QDir::Files
                                                       | QDir::NoDotAndDotDot)) {
        if (fi.isDir())
            QDir(fi.absoluteFilePath()).removeRecursively();
        else
            QFile::remove(fi.absoluteFilePath());
    }
    REQUIRE(servedDir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot).isEmpty());

    const auto again = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                          rpiboot::ChipGeneration::BCM2712,
                                          nullptr, cancelled);
    INFO("second call error: " << fm.lastError());
    CHECK(again == dir);
    CHECK_FALSE(again.empty());
    CHECK(std::filesystem::exists(again));

    fm.clearCache();
}

TEST_CASE("A source serving nothing is reported, not silently accepted",
          "[firmware][fetch]")
{
    // An empty tree: every download 404s. The caller has to be told, rather
    // than handed a directory with no firmware in it and left to sideload
    // nothing.
    QTemporaryDir served;
    REQUIRE(served.isValid());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());
    CHECK_FALSE(fm.lastError().empty());

    fm.clearCache();
}

TEST_CASE("Cancelling a firmware fetch stops it", "[firmware][fetch]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{true};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());

    fm.clearCache();
}

TEST_CASE("A source that is not there is reported", "[firmware][fetch]")
{
    // Offline, or the repository host is down.
    ServedFirmwareManager fm("http://127.0.0.1:1/");
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());
    CHECK_FALSE(fm.lastError().empty());

    fm.clearCache();
}


namespace {
size_t countOccurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        ++n;
    return n;
}
} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Making a secure-boot recovery come back as rpiboot
//
// Provisioning writes the signed bootloader, then needs the device to reboot
// into rpiboot again rather than into whatever it would normally boot. That is
// arranged by appending two settings to the recovery's config.txt, and the
// order of the two is load-bearing: config.txt is read top to bottom, and a
// recovery_reboot reached before set_boot_order reboots the device before the
// override has been seen. The device then powers up into normal boot, the
// imager sits waiting for a device that is never coming back, and nothing
// says why.

namespace {

std::filesystem::path sbrConfigFor(const std::filesystem::path &versionDir,
                                   ChipGeneration chip,
                                   const std::string &contents,
                                   bool create = true)
{
    const std::string sub = (chip == ChipGeneration::BCM2712)
                                ? "secure-boot-recovery5"
                                : "secure-boot-recovery";
    const auto dir = versionDir / sub;
    std::filesystem::create_directories(dir);
    const auto path = dir / "config.txt";
    if (create) {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out << contents;
    }
    return path;
}

std::string readAll(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("A recovery config gains both settings, boot order first",
          "[firmware][sbr]")
{
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();
    const auto cfg = sbrConfigFor(versionDir, ChipGeneration::BCM2712,
                                  "[all]\narm_64bit=1\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));

    const std::string out = readAll(cfg);
    INFO("config.txt:\n" << out);

    const auto orderAt = out.find("set_boot_order=0x3");
    const auto rebootAt = out.find("recovery_reboot=1");
    REQUIRE(orderAt != std::string::npos);
    REQUIRE(rebootAt != std::string::npos);
    CHECK(orderAt < rebootAt);

    // What upstream shipped is still there.
    CHECK(out.find("arm_64bit=1") != std::string::npos);
    CHECK(out.find("[all]") != std::string::npos);
}

TEST_CASE("Settings already in the file are moved rather than duplicated",
          "[firmware][sbr]")
{
    // Upstream may ship its own, and in the wrong order for our purposes.
    // Leaving them where they are would mean a recovery_reboot that fires
    // before the boot order is seen.
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();
    const auto cfg = sbrConfigFor(versionDir, ChipGeneration::BCM2712,
                                  "recovery_reboot=1\n"
                                  "[all]\n"
                                  "  set_boot_order=0xf41\n"
                                  "arm_64bit=1\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));

    const std::string out = readAll(cfg);
    INFO("config.txt:\n" << out);

    // Exactly one of each, ours, in our order.
    CHECK(countOccurrences(out, "set_boot_order=") == 1);
    CHECK(countOccurrences(out, "recovery_reboot=") == 1);
    CHECK(out.find("0xf41") == std::string::npos);
    CHECK(out.find("set_boot_order=0x3") < out.find("recovery_reboot=1"));
    CHECK(out.find("arm_64bit=1") != std::string::npos);
}

TEST_CASE("Running twice leaves the file as it was after once",
          "[firmware][sbr]")
{
    // ensureAvailable calls this again after the download loop, so it has to
    // be idempotent or the file grows a pair of lines per attempt.
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();
    const auto cfg = sbrConfigFor(versionDir, ChipGeneration::BCM2712, "[all]\narm_64bit=1\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));
    const std::string once = readAll(cfg);

    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));
    const std::string twice = readAll(cfg);

    INFO("after one:\n" << once << "\nafter two:\n" << twice);
    CHECK(once == twice);
}

TEST_CASE("A recovery config with Windows line endings is rewritten as LF",
          "[firmware][sbr]")
{
    // The bootloader wants LF. A stray CR left on a kept line would ride
    // along into the rewritten file.
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();
    const auto cfg = sbrConfigFor(versionDir, ChipGeneration::BCM2712,
                                  "[all]\r\narm_64bit=1\r\nrecovery_reboot=1\r\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));

    const std::string out = readAll(cfg);
    INFO("config.txt:\n" << out);
    CHECK(out.find('\r') == std::string::npos);
    CHECK(out.find("arm_64bit=1") != std::string::npos);
    CHECK(countOccurrences(out, "recovery_reboot=") == 1);
}

TEST_CASE("A recovery config that is not there yet is not an error",
          "[firmware][sbr]")
{
    // First run: the download has not happened. The caller comes back after
    // the download loop, so this has to be a benign no-op rather than a
    // failure that aborts provisioning.
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();

    TestableFirmwareManager fm;
    CHECK(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));
    CHECK(fm.lastError().empty());
}

TEST_CASE("The recovery directory depends on the chip", "[firmware][sbr]")
{
    // 2712 keeps its recovery in secure-boot-recovery5; earlier chips use
    // secure-boot-recovery. Writing to the wrong one leaves the real config
    // untouched and the device booting normally.
    ScratchDir scratch;
    const std::filesystem::path versionDir = scratch.path();

    const auto older = sbrConfigFor(versionDir, ChipGeneration::BCM2711, "[all]\n");
    const auto newer = sbrConfigFor(versionDir, ChipGeneration::BCM2712, "[all]\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2711));

    CHECK(readAll(older).find("set_boot_order=0x3") != std::string::npos);
    CHECK(readAll(newer).find("set_boot_order=0x3") == std::string::npos);
}

// ══════════════════════════════════════════════════════════════
// A new EEPROM release must not leave the old payload cached
//
// Secure-boot provisioning writes pieeprom to the device's EEPROM. The
// binary is dated -- pieeprom-2024-09-23.bin -- and which date to fetch is
// resolved at runtime from rpi-eeprom's versions.txt, so the cached copy
// under secure-boot-recovery5/ has no name to distinguish it from a copy of
// a different release. ensureAvailable() therefore records the version it
// fetched in a .eeprom-version sidecar and purges the payload when the
// resolved version no longer matches.

namespace {

// The files a BCM2712 secure-boot-recovery run asks for, on top of what
// layOutFirmware() already serves.
void layOutSecureBootRecovery(const QString &root,
                              const QString &eepromVersion,
                              const QByteArray &pieepromBody)
{
    struct Entry { QString path; QByteArray body; };
    const Entry entries[] = {
        {QStringLiteral("secure-boot-recovery5/boot.conf"),  QByteArrayLiteral("BOOT_ORDER=0xf1")},
        {QStringLiteral("secure-boot-recovery5/config.txt"), QByteArrayLiteral("# recovery config")},
        {QStringLiteral("firmware-2712/latest/pieeprom-") + eepromVersion + QStringLiteral(".bin"),
         pieepromBody},
        // "Sorted newest-first"; the first usable row is the one taken.
        {QStringLiteral("firmware-2712/versions.txt"),
         (eepromVersion + QStringLiteral("  1727086800  abc  latest\n")).toLatin1()},
    };
    for (const Entry &e : entries) {
        const QString full = QDir(root).filePath(e.path);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(e.body);
        f.close();
    }
}

std::string readFileContents(const std::filesystem::path &p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("A secure-boot run records which EEPROM version it cached",
          "[firmware][sbr][eeprom]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());
    layOutSecureBootRecovery(served.path(), QStringLiteral("2024-09-23"),
                             QByteArrayLiteral("pieeprom for 2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    const auto sub = dir / "secure-boot-recovery5";
    CHECK(std::filesystem::exists(sub / "pieeprom.original.bin"));

    // The sidecar is what the next run compares against. Without it every
    // run looks like a version change, or none of them do.
    const auto sidecar = sub / ".eeprom-version";
    REQUIRE(std::filesystem::exists(sidecar));
    CHECK(readFileContents(sidecar) == "2024-09-23\n");

    fm.clearCache();
}

TEST_CASE("A newer EEPROM release is fetched in place of the old one",
          "[firmware][sbr][eeprom]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());
    layOutSecureBootRecovery(served.path(), QStringLiteral("2024-09-23"),
                             QByteArrayLiteral("pieeprom for 2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    INFO("first run error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    const auto payload = dir / "secure-boot-recovery5" / "pieeprom.original.bin";
    REQUIRE(std::filesystem::exists(payload));
    REQUIRE(readFileContents(payload) == "pieeprom for 2024-09-23");

    // rpi-eeprom publishes a new build. The cached file keeps its name, so
    // nothing but the sidecar can tell the two apart.
    layOutSecureBootRecovery(served.path(), QStringLiteral("2025-01-15"),
                             QByteArrayLiteral("pieeprom for 2025-01-15"));

    const auto again = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                          rpiboot::ChipGeneration::BCM2712,
                                          nullptr, cancelled);
    INFO("second run error: " << fm.lastError());
    REQUIRE_FALSE(again.empty());

    // Note this passes with the purge disabled: the manifest URL carries the
    // version, so a reachable source overwrites the payload regardless. What
    // the purge is actually for is the next test.
    CHECK(readFileContents(payload) == "pieeprom for 2025-01-15");
    CHECK(readFileContents(dir / "secure-boot-recovery5" / ".eeprom-version")
          == "2025-01-15\n");

    fm.clearCache();
}

TEST_CASE("An unchanged EEPROM version keeps the payload it already has",
          "[firmware][sbr][eeprom]")
{
    // The counterpart: the purge must be driven by the version actually
    // changing, not fire on every run. Purging unconditionally would turn
    // every secure-boot provisioning into a fresh download of the
    // bootloader, and would fail outright whenever the source is
    // unreachable -- the case the cache exists for.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());
    layOutSecureBootRecovery(served.path(), QStringLiteral("2024-09-23"),
                             QByteArrayLiteral("pieeprom for 2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    INFO("first run error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    const auto payload = dir / "secure-boot-recovery5" / "pieeprom.original.bin";
    REQUIRE(std::filesystem::exists(payload));

    REQUIRE(QFile::remove(QDir(served.path())
                              .filePath(QStringLiteral("firmware-2712/latest/pieeprom-2024-09-23.bin"))));

    const auto again = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                          rpiboot::ChipGeneration::BCM2712,
                                          nullptr, cancelled);
    INFO("second run error: " << fm.lastError());
    CHECK_FALSE(again.empty());
    CHECK(std::filesystem::exists(payload));
    CHECK(readFileContents(payload) == "pieeprom for 2024-09-23");

    fm.clearCache();
}

TEST_CASE("A new EEPROM version whose payload cannot be fetched fails rather than "
          "provisioning the old one", "[firmware][sbr][eeprom]")
{
    // This is what the purge is for, and the only case that distinguishes it
    // from doing nothing.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());
    layOutSecureBootRecovery(served.path(), QStringLiteral("2024-09-23"),
                             QByteArrayLiteral("pieeprom for 2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    ServedFirmwareManager fm(server.base());
    fm.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    INFO("first run error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    const auto payload = dir / "secure-boot-recovery5" / "pieeprom.original.bin";
    REQUIRE(readFileContents(payload) == "pieeprom for 2024-09-23");

    // A new release is announced, but its payload is not there to fetch --
    // a partially published release, or a source reachable for the small
    // metadata file and not for the binary.
    {
        const QString versions =
            QDir(served.path()).filePath(QStringLiteral("firmware-2712/versions.txt"));
        QFile f(versions);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("2025-01-15  1727086800  abc  latest\n");
    }

    const auto again = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                          rpiboot::ChipGeneration::BCM2712,
                                          nullptr, cancelled);

    INFO("second run error: " << fm.lastError());
    INFO("payload now: "
         << (std::filesystem::exists(payload) ? readFileContents(payload)
                                              : std::string("<absent>")));

    // Refusing to proceed is the correct outcome. Silently provisioning the
    // 2024-09-23 bootloader under the name of the 2025-01-15 release is not.
    CHECK(again.empty());
    CHECK_FALSE(fm.lastError().empty());
    CHECK_FALSE(std::filesystem::exists(payload));

    fm.clearCache();
}

// ══════════════════════════════════════════════════════════════════════════
// The boards that were never exercised, and the network going away
//
// Everything above drives a CM5 (BCM2712). The manifest, though, is a table
// of URLs keyed on chip *and* mode, and three of its arms had never been
// read: secure boot on a CM4, fastboot on a CM3, and secure boot on a chip
// that has no secure-boot firmware at all. A wrong URL in any of them is not
// a compile error and not a crash -- it is a 404, and what the user sees is
// provisioning that stops with nothing to say why.

namespace {

std::string manifestUrlFor(const std::vector<TestableFirmwareManager::ManifestEntry> &m,
                           const std::string &localPath)
{
    for (const auto &e : m)
        if (e.localPath == localPath)
            return e.url;
    return {};
}

bool manifestHas(const std::vector<TestableFirmwareManager::ManifestEntry> &m,
                 const std::string &localPath)
{
    for (const auto &e : m)
        if (e.localPath == localPath)
            return true;
    return false;
}

std::string manifestJoined(const std::vector<TestableFirmwareManager::ManifestEntry> &m)
{
    std::string all;
    for (const auto &e : m)
        all += e.url + " -> " + e.localPath + "\n";
    return all;
}

} // namespace

TEST_CASE("A CM4 recovery manifest asks only for CM4 firmware", "[firmware][sbr]")
{
    // A CM4's EEPROM and a CM5's are different silicon with different
    // images. Fetching one for the other does not fail at download time --
    // both files exist upstream -- so the first thing that notices is the
    // board, after the write.
    TestableFirmwareManager fm;
    const auto m = fm.buildManifest(SideloadMode::SecureBootRecovery,
                                    ChipGeneration::BCM2711,
                                    std::optional<std::string>("2024-09-23"));
    INFO(manifestJoined(m));
    REQUIRE_FALSE(m.empty());

    // The USB-mode bootcode a CM4 runs to write its EEPROM is rpi-eeprom's
    // 2711 recovery.bin, landing as bootcode4.bin.
    CHECK(manifestUrlFor(m, "bootcode4.bin").find("firmware-2711/latest/recovery.bin")
          != std::string::npos);

    // The recovery subdirectory is the unsuffixed one; secure-boot-recovery5
    // is the CM5 spelling and nothing here may use it.
    CHECK(manifestHas(m, "secure-boot-recovery/boot.conf"));
    CHECK(manifestHas(m, "secure-boot-recovery/config.txt"));

    const std::string pieeprom = manifestUrlFor(m, "secure-boot-recovery/pieeprom.original.bin");
    CHECK(pieeprom.find("firmware-2711/latest/pieeprom-2024-09-23.bin") != std::string::npos);

    // Nothing 2712-shaped anywhere in the list.
    const std::string all = manifestJoined(m);
    CHECK(all.find("2712") == std::string::npos);
    CHECK(all.find("bootcode5") == std::string::npos);
    CHECK(all.find("recovery5") == std::string::npos);

    for (const auto &e : m) {
        INFO("entry: " << e.url << " -> " << e.localPath);
        CHECK_FALSE(e.url.empty());
        CHECK_FALSE(fs::path(e.localPath).is_absolute());
        CHECK(e.localPath.find("..") == std::string::npos);
    }
}

TEST_CASE("A CM3 fastboot manifest uses the self-contained bundle", "[firmware]")
{
    // BCM2836/7 bootstraps differently from the later chips: bootcode.bin
    // comes from usbboot's mass-storage directory, and the fastboot payload
    // is one bundle rather than a gadget kernel plus a config plus a TAR.
    // Handing it the CM4/CM5 file list would leave the board with a
    // bootcode it cannot run.
    TestableFirmwareManager fm;
    const auto m = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2836_7);
    INFO(manifestJoined(m));
    REQUIRE_FALSE(m.empty());

    CHECK(manifestUrlFor(m, "bootcode.bin").find("msd/bootcode.bin") != std::string::npos);
    CHECK(manifestUrlFor(m, "fastboot/bootfiles.bin").find("2710-bootfiles-bin")
          != std::string::npos);

    // No separate gadget kernel and no separate config: the bundle carries
    // both, and asking for them would 404.
    CHECK_FALSE(manifestHas(m, "fastboot/fastboot-gadget.img"));
    CHECK_FALSE(manifestHas(m, "fastboot/config.txt"));

    // And it is genuinely a different list from the later chips'.
    const auto later = fm.buildManifest(SideloadMode::Fastboot, ChipGeneration::BCM2712);
    CHECK(manifestJoined(m) != manifestJoined(later));
}

TEST_CASE("Secure boot on a board that has no secure-boot firmware is refused",
          "[firmware][sbr]")
{
    // There is no secure-boot recovery for BCM2836/7. The manifest for it is
    // empty, and an empty manifest must stop the run: the alternative is a
    // cache directory with nothing in it, reported as ready.
    TestableFirmwareManager fm;
    CHECK(fm.buildManifest(SideloadMode::SecureBootRecovery,
                           ChipGeneration::BCM2836_7,
                           std::optional<std::string>("2024-09-23")).empty());

    // Port 1 is nothing: the version lookup fails immediately, so this
    // exercises the empty manifest rather than the network.
    ServedFirmwareManager served("http://127.0.0.1:1/");
    served.clearCache();

    std::atomic<bool> cancelled{false};
    const auto dir = served.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                            rpiboot::ChipGeneration::BCM2836_7,
                                            nullptr, cancelled);
    INFO("error: " << served.lastError());
    CHECK(dir.empty());
    CHECK(served.lastError().find("No firmware files defined") != std::string::npos);

    served.clearCache();
}

namespace {

// A manager whose source and cache are both given from outside, so two runs
// can share a cache while the source between them changes -- which is how
// "the network went away" is expressed here.
class PinnedFirmwareManager : public FirmwareManager
{
public:
    PinnedFirmwareManager(std::string base, fs::path cache)
        : _base(std::move(base)), _cache(std::move(cache)) {}

    std::filesystem::path cacheRoot() const override { return _cache; }

protected:
    std::string usbbootBase() const override { return _base; }
    std::string eepromBase() const override { return _base; }
    std::string provisionerBase() const override { return _base; }

private:
    std::string _base;
    fs::path    _cache;
};

// What a CM4 secure-boot-recovery run asks for. The unsuffixed
// secure-boot-recovery directory and firmware-2711 throughout -- deliberately
// not shared with the CM5 helper above, so that a manifest that reached for
// the wrong chip's paths would find nothing here.
void layOutSecureBootRecovery2711(const QString &root, const QString &eepromVersion)
{
    struct Entry { QString path; QByteArray body; };
    const Entry entries[] = {
        {QStringLiteral("secure-boot-recovery/boot.conf"),  QByteArrayLiteral("BOOT_ORDER=0xf1")},
        {QStringLiteral("secure-boot-recovery/config.txt"), QByteArrayLiteral("# cm4 recovery")},
        {QStringLiteral("firmware-2711/latest/pieeprom-") + eepromVersion + QStringLiteral(".bin"),
         QByteArrayLiteral("cm4 pieeprom")},
        {QStringLiteral("firmware-2711/latest/recovery.bin"), QByteArrayLiteral("cm4 recovery")},
        {QStringLiteral("firmware-2711/versions.txt"),
         (eepromVersion + QStringLiteral("  1727086800  abc  latest\n")).toLatin1()},
    };
    for (const Entry &e : entries) {
        const QString full = QDir(root).filePath(e.path);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(e.body);
        f.close();
    }
}

} // namespace

TEST_CASE("A CM4 secure-boot fetch caches the CM4 firmware", "[firmware][fetch][sbr]")
{
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutSecureBootRecovery2711(served.path(), QStringLiteral("2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2711,
                                        nullptr, cancelled);
    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    // The bootcode a fused CM4 runs is the 2711 recovery binary, under the
    // 2711 name. A CM5-shaped cache would carry bootcode5.bin instead.
    CHECK(readFileContents(dir / "bootcode4.bin") == "cm4 recovery");
    CHECK_FALSE(fs::exists(dir / "bootcode5.bin"));

    const auto sub = dir / "secure-boot-recovery";
    CHECK(readFileContents(sub / "pieeprom.original.bin") == "cm4 pieeprom");
    CHECK(fs::exists(sub / "boot.conf"));
    CHECK(readFileContents(sub / ".eeprom-version") == "2024-09-23\n");
    CHECK_FALSE(fs::exists(dir / "secure-boot-recovery5"));
}

TEST_CASE("Offline with nothing cached names the file it cannot resolve",
          "[firmware][fetch][sbr]")
{
    // A first run with no network: the version lookup fails, no previous run
    // left a version behind, and so there is no name for the dated EEPROM
    // binary to fetch. The manifest carries an empty URL for it rather than a
    // wrong one, and the run has to stop and say which file is missing --
    // otherwise the cache validates on the files that *did* arrive and the
    // board is provisioned without its bootloader.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    // Everything except versions.txt, so the two config files download and
    // only the EEPROM payload is unresolvable.
    const struct { const char *path; const char *body; } entries[] = {
        {"secure-boot-recovery5/boot.conf",  "BOOT_ORDER=0xf1"},
        {"secure-boot-recovery5/config.txt", "# recovery config"},
        {"firmware-2712/latest/recovery.bin", "cm5 recovery"},
    };
    for (const auto &e : entries) {
        const QString full = QDir(served.path()).filePath(QString::fromLatin1(e.path));
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile f(full);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(e.body);
        f.close();
    }

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());
    CHECK(fm.lastError().find("pieeprom.original.bin") != std::string::npos);
    CHECK(fm.lastError().find("nothing cached") != std::string::npos);
}

TEST_CASE("A remembered EEPROM version carries a run whose version lookup fails",
          "[firmware][fetch][sbr]")
{
    // The version metadata is a separate small file with its own cached copy,
    // and the version a run settled on is also recorded beside the payload.
    // When the metadata is gone and cannot be re-fetched -- a partially
    // cleared cache, or macOS purging part of the app's cache directory --
    // the recorded version is what is left to go on. Without it the run has
    // no name for the EEPROM binary and stops, even though the binary it
    // needs is already sitting in the cache.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutSecureBootRecovery2711(served.path(), QStringLiteral("2024-09-23"));

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir cacheDir;
    REQUIRE(cacheDir.isValid());
    const fs::path cache = fs::path(cacheDir.path().toStdString()) / "rpiboot-firmware";

    std::atomic<bool> cancelled{false};
    fs::path dir;
    {
        PinnedFirmwareManager fm(server.base(), cache);
        dir = fm.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                 rpiboot::ChipGeneration::BCM2711, nullptr, cancelled);
        INFO("first run error: " << fm.lastError());
        REQUIRE_FALSE(dir.empty());
    }

    // A purge takes whole files, and it does not get to choose which. Here it
    // has taken the cached metadata and the EEPROM payload and left the
    // recorded version, which is the only case that costs anything: with the
    // payload still on disk the run would carry on regardless of whether the
    // version were remembered.
    const auto versionsCache = cache / "master" / "firmware-2711-versions.txt";
    const auto payload = dir / "secure-boot-recovery" / "pieeprom.original.bin";
    REQUIRE(fs::exists(versionsCache));
    REQUIRE(fs::exists(payload));
    fs::remove(versionsCache);
    fs::remove(payload);
    REQUIRE(readFileContents(dir / "secure-boot-recovery" / ".eeprom-version")
            == "2024-09-23\n");

    // The source is still up -- the metadata is simply not where it was, which
    // is what an upstream move looks like from here. The remembered version is
    // what names the file to fetch; without it the URL is empty and the run
    // stops on a payload it could have had.
    QFile::remove(QDir(served.path()).filePath(QStringLiteral("firmware-2711/versions.txt")));

    PinnedFirmwareManager second(server.base(), cache);
    const auto again = second.ensureAvailable(rpiboot::SideloadMode::SecureBootRecovery,
                                              rpiboot::ChipGeneration::BCM2711,
                                              nullptr, cancelled);
    INFO("second run error: " << second.lastError());
    CHECK_FALSE(again.empty());
    CHECK(again == dir);
    CHECK(fs::exists(payload));
    CHECK(readFileContents(payload) == "cm4 pieeprom");
}

TEST_CASE("Fetching firmware reports progress the whole way", "[firmware][fetch]")
{
    // "Checking rpiboot firmware..." is the only thing on screen while this
    // runs, and on a slow connection it is there for a while. A percentage
    // that never moves, or that runs past the end, is what the user reads as
    // a hang.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    // The gadget kernel is the one large file in a real fetch (tens of MB),
    // and it is the only one that produces progress *within* a file rather
    // than a single tick at completion. The fixture's placeholder is a
    // handful of bytes, which arrives in one go and so cannot show whether
    // the within-file fraction is computed the right way round.
    {
        QFile big(QDir(served.path()).filePath(
            QStringLiteral("host-support/fastboot-gadget.img")));
        REQUIRE(big.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray chunk(1 << 20, 'g');
        for (int i = 0; i < 24; ++i)
            REQUIRE(big.write(chunk) == chunk.size());
        big.close();
    }

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");

    struct Tick { uint64_t current, total; std::string status; };
    std::vector<Tick> ticks;
    auto onProgress = [&ticks](uint64_t c, uint64_t t, const std::string &s) {
        ticks.push_back({c, t, s});
    };

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        onProgress, cancelled);
    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    REQUIRE(ticks.size() > 1);
    std::string trace;
    for (const Tick &t : ticks)
        trace += std::to_string(t.current) + " ";
    INFO("ticks: " << trace);

    uint64_t previous = 0;
    for (const Tick &t : ticks) {
        CHECK(t.total == 100);
        CHECK(t.current <= t.total);
        // A bar that goes backwards reads as the download restarting.
        CHECK(t.current >= previous);
        previous = t.current;
        // The status is what is on screen; an empty one blanks the label.
        CHECK_FALSE(t.status.empty());
    }

    // It has to actually move, and it has to arrive: a percentage stuck at
    // its opening value for the whole fetch is the thing users report as a
    // hang, and one that stops short of the end leaves the step looking
    // unfinished after it has finished.
    CHECK(ticks.front().current == 0);
    CHECK(ticks.back().current == 100);
    CHECK(ticks.front().status.find("firmware") != std::string::npos);

    // And it has to move *per file*, not merely open at 0 and close at 100:
    // the fastboot manifest is three files, so a fetch that reports only its
    // two endpoints is a bar that sits still for the whole download.
    std::set<uint64_t> distinct;
    for (const Tick &t : ticks)
        distinct.insert(t.current);
    INFO("distinct: " << distinct.size());
    CHECK(distinct.size() >= 4);
}

TEST_CASE("A custom fastboot gadget that is not there is reported",
          "[firmware][fetch]")
{
    // The custom-gadget field is a path the user typed or picked, and it can
    // have been moved or unmounted since. Copying it is the last step before
    // the board is told to boot it, so a silent failure here means a device
    // that sits waiting for a gadget that was never written.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");
    fm.setCustomFastbootGadget((fs::path(cache.path().toStdString())
                                / "gadget-that-was-moved.img").string());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());
    CHECK(fm.lastError().find("boot.img") != std::string::npos);
}

// ══════════════════════════════════════════════════════════════════════════
// Signing what a fused board will be asked to run
//
// A CM5 whose customer key is in OTP will not run a fastboot gadget that is
// not signed with it, and will not run the bootcode inside the bundle either.
// The refusal is silent: the board simply does not come back, and the imager
// sits waiting for a device that is never going to appear.

namespace {

// A throwaway RSA-2048 key. Nothing here signs anything real; the point is
// that the signing path runs against a key openssl will accept.
bool makeRsaKey(const QString &path)
{
    QProcess openssl;
    openssl.start(rpi_test::toolPath(QStringLiteral("openssl")),
                  {QStringLiteral("genrsa"), QStringLiteral("-out"), path,
                   QStringLiteral("2048")});
    if (!openssl.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return false;
    return openssl.exitCode() == 0 && QFileInfo(path).size() > 0;
}

qint64 sizeOf(const std::filesystem::path &p)
{
    return QFileInfo(QString::fromStdString(p.string())).size();
}

// One member of a tar, as bytes. Used to read the bootcode back out of the
// bundle the board is served from.
QByteArray tarMember(const std::filesystem::path &archive, const QString &member)
{
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-xOf"), QString::fromStdString(archive.string()), member});
    if (!tar.waitForFinished(rpi_test::kFixtureProcessTimeoutMs))
        return {};
    if (tar.exitCode() != 0)
        return {};
    return tar.readAllStandardOutput();
}

// The counter-signature the 2712 boot ROM checks: len + keynum + version +
// a 256-byte RSA-2048 signature + a 264-byte public key.
constexpr qint64 kCounterSignatureBytes = 4 + 4 + 4 + 256 + 264;

} // namespace

TEST_CASE("A signing key gets the gadget and the bootcode signed",
          "[firmware][fetch][sign]")
{
    if (!rpi_test::haveTool(QStringLiteral("openssl")))
        SKIP("openssl is not installed, so no key can be made to sign with");

    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir keyDir;
    REQUIRE(keyDir.isValid());
    const QString key = keyDir.filePath(QStringLiteral("customer.pem"));
    REQUIRE(makeRsaKey(key));

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");
    fm.setSignFastbootGadgetKey(key.toStdString());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    // The gadget the board is told to boot, and its signature beside it.
    CHECK(fs::exists(dir / "fastboot" / "boot.img"));
    REQUIRE(fs::exists(dir / "fastboot" / "boot.sig"));
    CHECK(sizeOf(dir / "fastboot" / "boot.sig") > 0);

    // The bootcode uploaded to the ROM, counter-signed. The unsigned original
    // is what the bundle carried, so the difference is the signature.
    const QByteArray upstream = tarMember(dir / "fastboot" / "bootfiles.bin.original",
                                          QStringLiteral("2712/bootcode5.bin"));
    REQUIRE_FALSE(upstream.isEmpty());
    const qint64 signedSize = sizeOf(dir / "bootcode5.bin");
    INFO("upstream " << upstream.size() << " signed " << signedSize);
    CHECK(signedSize == upstream.size() + kCounterSignatureBytes);

    // And the same signed bytes put back into the bundle, because the running
    // bootcode chain-loads the next stage out of it and checks that one too.
    const QByteArray inBundle = tarMember(dir / "fastboot" / "bootfiles.bin",
                                          QStringLiteral("2712/bootcode5.bin"));
    CHECK(inBundle.size() == signedSize);
    CHECK(inBundle.left(upstream.size()) == upstream);
}

TEST_CASE("Signing twice does not sign the signature", "[firmware][fetch][sign]")
{
    // Re-provisioning runs the whole thing again over a cache that already
    // holds a signed bundle. Signing what is in it would chain a second
    // signature onto the first, and the ROM rejects that -- which is what the
    // pristine copy kept alongside it is for.
    if (!rpi_test::haveTool(QStringLiteral("openssl")))
        SKIP("openssl is not installed, so no key can be made to sign with");

    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir keyDir;
    REQUIRE(keyDir.isValid());
    const QString key = keyDir.filePath(QStringLiteral("customer.pem"));
    REQUIRE(makeRsaKey(key));

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");
    fm.setSignFastbootGadgetKey(key.toStdString());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);
    REQUIRE_FALSE(dir.empty());
    const qint64 first = sizeOf(dir / "bootcode5.bin");
    REQUIRE(first > 0);

    // Take the bundle off the source, so the second run keeps the cached copy
    // rather than replacing it. That cached copy is the signed one this run
    // wrote, which is the only way the question arises: with the bundle
    // re-downloaded every time there is never a signed one to sign again.
    REQUIRE(QFile::remove(QDir(served.path())
                              .filePath(QStringLiteral("firmware/bootfiles.bin"))));

    const auto again = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                          rpiboot::ChipGeneration::BCM2712,
                                          nullptr, cancelled);
    INFO("second run error: " << fm.lastError());
    REQUIRE_FALSE(again.empty());

    // Same length, so one signature and not two.
    CHECK(sizeOf(dir / "bootcode5.bin") == first);
    CHECK(tarMember(dir / "fastboot" / "bootfiles.bin",
                    QStringLiteral("2712/bootcode5.bin")).size() == first);
}

TEST_CASE("A key that is not a key is reported rather than shipped",
          "[firmware][fetch][sign]")
{
    // Someone points the setting at the wrong file. Carrying on would put an
    // unsigned or half-signed gadget in front of a board that refuses it,
    // with nothing to say why.
    QTemporaryDir served;
    REQUIRE(served.isValid());
    layOutFirmware(served.path());

    LocalFirmwareServer server(served.path());
    if (!server.isRunning())
        SKIP("could not start the local firmware server");

    QTemporaryDir keyDir;
    REQUIRE(keyDir.isValid());
    const QString key = keyDir.filePath(QStringLiteral("not-a-key.pem"));
    {
        QFile f(key);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("this is not a private key\n");
    }

    QTemporaryDir cache;
    REQUIRE(cache.isValid());
    PinnedFirmwareManager fm(server.base(),
                             fs::path(cache.path().toStdString()) / "rpiboot-firmware");
    fm.setSignFastbootGadgetKey(key.toStdString());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(rpiboot::SideloadMode::Fastboot,
                                        rpiboot::ChipGeneration::BCM2712,
                                        nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK(dir.empty());
    CHECK_FALSE(fm.lastError().empty());
    // Names the key, so the reader knows which setting to look at.
    CHECK(fm.lastError().find("key") != std::string::npos);
}

// ---------------------------------------------------------------------------
// A warm cache with no network
//
// ensureAvailable() is the largest untested thing in this class, and the
// reason it is worth reaching is not the download: it is what happens when
// the download cannot happen. Somebody bootstrapping a Compute Module on a
// workbench has the firmware from last time and no internet, and the code is
// written to carry on -- "network failures degrade gracefully: if the file is
// already cached, we proceed with the stale copy". Nothing checked that.
// ---------------------------------------------------------------------------

namespace {

// Points every transfer in the process at a closed port for the life of the
// object, so a case that means to be offline cannot quietly reach GitHub.
class NoNetworkGuard
{
public:
    NoNetworkGuard() : _saved(CurlNetworkConfig::instance().proxy())
    {
        CurlNetworkConfig::instance().setProxy(QByteArrayLiteral("http://127.0.0.1:1"));
    }
    ~NoNetworkGuard() { CurlNetworkConfig::instance().setProxy(_saved); }

    NoNetworkGuard(const NoNetworkGuard &) = delete;
    NoNetworkGuard &operator=(const NoNetworkGuard &) = delete;

private:
    QByteArray _saved;
};

// A manager whose cache is a directory the case owns.
class CachedFirmwareManager : public TestableFirmwareManager
{
public:
    explicit CachedFirmwareManager(fs::path root) : _root(std::move(root)) {}
    fs::path cacheRoot() const override { return _root; }

private:
    fs::path _root;
};

} // namespace

TEST_CASE("Firmware already cached is enough when the network has gone",
          "[firmware]")
{
    ScratchDir scratch;
    CachedFirmwareManager fm(scratch.path());
    const fs::path versionDir = scratch.path() / "master";

    // What a previous run left behind: every file the manifest names, the
    // chip's bootcode, and the sidecar recording which EEPROM build it was.
    const std::string version = "2026-05-22";
    writeFile(versionDir / "secure-boot-recovery5" / ".eeprom-version", version + "\n");
    for (const auto &entry : fm.buildManifest(SideloadMode::SecureBootRecovery,
                                              ChipGeneration::BCM2712,
                                              std::optional<std::string>(version)))
        writeFile(versionDir / entry.localPath, "firmware bytes from last time");
    writeFile(versionDir / "bootcode5.bin", "bootcode bytes");

    NoNetworkGuard offline;
    std::atomic<bool> cancelled{false};
    const fs::path out = fm.ensureAvailable(SideloadMode::SecureBootRecovery,
                                            ChipGeneration::BCM2712, nullptr, cancelled);

    INFO("error: " << fm.lastError());
    CHECK_FALSE(out.empty());
    CHECK(fs::exists(out / "bootcode5.bin"));
}

TEST_CASE("A cold cache with no network says which file it could not get",
          "[firmware]")
{
    // The other side of it. Nothing cached and nowhere to fetch from is a
    // hard failure, and it has to name what is missing -- "firmware download
    // failed" on its own leaves somebody with no idea whether to check their
    // network or their disk.
    ScratchDir scratch;
    CachedFirmwareManager fm(scratch.path());

    NoNetworkGuard offline;
    std::atomic<bool> cancelled{false};
    const fs::path out = fm.ensureAvailable(SideloadMode::SecureBootRecovery,
                                            ChipGeneration::BCM2712, nullptr, cancelled);

    CHECK(out.empty());
    INFO("error: " << fm.lastError());
    CHECK_FALSE(fm.lastError().empty());
}

// ---------------------------------------------------------------------------
// The conditional GET, and the disks that will not take what is written
// ---------------------------------------------------------------------------

TEST_CASE("Firmware already cached is revalidated, not fetched again",
          "[firmware][etag]")
{
    // Every session re-checks the firmware bundle. GitHub raw serves a
    // content-hash ETag, so a conditional GET turns that into a couple of
    // hundred bytes instead of a firmware download -- but only if the
    // sidecar is read back and the header actually sent. None of that had
    // run: no test server answered 304, so a server that always returns 200
    // could not tell a working conditional request from an absent one.
    rpi_test::ConditionalHttpServer server(QByteArray("firmware bytes"),
                                           QByteArray("\"abc123\""));
    REQUIRE_HTTP_SERVER(server);

    ScratchDir dir;
    const fs::path dest = dir.path() / "cached" / "pieeprom.bin";
    writeFile(dest, "firmware bytes");
    writeFile(fs::path(dest).concat(".etag"), "\"abc123\"");

    TestableFirmwareManager fm;
    std::atomic<bool> cancelled{false};
    const std::string url = server.urlFor(QStringLiteral("pieeprom.bin")).toStdString();

    CHECK(fm.downloadFile(url, dest, nullptr, cancelled));

    // Kept, not replaced: a 304 carries no body, so a downloadFile that
    // committed the empty temporary file would blank the cache.
    std::ifstream in(dest, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(got == "firmware bytes");
    CHECK_FALSE(fs::exists(fs::path(dest).concat(".tmp")));
}

TEST_CASE("A sidecar that no longer matches gets the new bytes", "[firmware][etag]")
{
    // The other half of the same mechanism. A stale sidecar must produce a
    // 200 and a replacement, not a 304 against something the server no
    // longer has.
    rpi_test::ConditionalHttpServer server(QByteArray("new firmware"),
                                           QByteArray("\"v2\""));
    REQUIRE_HTTP_SERVER(server);

    ScratchDir dir;
    const fs::path dest = dir.path() / "cached" / "pieeprom.bin";
    writeFile(dest, "old firmware");
    writeFile(fs::path(dest).concat(".etag"), "\"v1\"");

    TestableFirmwareManager fm;
    std::atomic<bool> cancelled{false};
    CHECK(fm.downloadFile(server.urlFor(QStringLiteral("pieeprom.bin")).toStdString(),
                          dest, nullptr, cancelled));

    std::ifstream in(dest, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(got == "new firmware");

    // And the sidecar moved on with it, or the next session asks the wrong
    // question.
    std::ifstream tagIn(fs::path(dest).concat(".etag"));
    std::string tag;
    std::getline(tagIn, tag);
    CHECK(tag == "\"v2\"");
}

TEST_CASE("A cache directory that cannot be written to is reported", "[firmware]")
{
    // A cache on a full or read-only filesystem. The download has to say so
    // rather than leave a zero-length file where firmware should be.
    if (::geteuid() == 0)
        SKIP("running as root, which the mode bits do not stop");

    ScratchDir dir;
    const fs::path holder = dir.path() / "locked";
    fs::create_directories(holder);
    REQUIRE(::chmod(holder.c_str(), 0500) == 0);

    TestableFirmwareManager fm;
    std::atomic<bool> cancelled{false};
    CHECK_FALSE(fm.downloadFile("http://127.0.0.1:1/never.bin", holder / "pieeprom.bin",
                                nullptr, cancelled));
    CHECK_FALSE(fm.lastError().empty());

    ::chmod(holder.c_str(), 0700);
}

TEST_CASE("A bootcode that cannot be written out is reported", "[firmware]")
{
    // The bootcode is extracted from the bootfiles TAR into the version
    // directory. If that write fails the board would be sent whatever was
    // there before, so it has to stop rather than carry on.
    if (::geteuid() == 0)
        SKIP("running as root, which the mode bits do not stop");

    ScratchDir dir;
    const fs::path versionDir = dir.path() / "v1";
    fs::create_directories(versionDir / "fastboot");

    // A bootfiles.bin holding the bootcode the extractor will look for.
    {
        ::archive *a = archive_write_new();
        archive_write_set_format_ustar(a);
        archive_write_open_filename(a, (versionDir / "fastboot" / "bootfiles.bin").c_str());
        const std::string payload = "bootcode bytes";
        ::archive_entry *e = archive_entry_new();
        archive_entry_set_pathname(e, "2712/bootcode5.bin");
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_entry_set_size(e, static_cast<la_int64_t>(payload.size()));
        archive_write_header(a, e);
        archive_write_data(a, payload.data(), payload.size());
        archive_entry_free(e);
        archive_write_close(a);
        archive_write_free(a);
    }

    TestableFirmwareManager fm;
    REQUIRE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));
    REQUIRE(fs::exists(versionDir / "bootcode5.bin"));

    // Now take the write permission away and do it again.
    fs::remove(versionDir / "bootcode5.bin");
    REQUIRE(::chmod(versionDir.c_str(), 0500) == 0);

    CHECK_FALSE(fm.extractBootcodeFromBootfiles(versionDir, ChipGeneration::BCM2712));
    CHECK(fm.lastError().find("bootcode5.bin") != std::string::npos);

    ::chmod(versionDir.c_str(), 0700);
}

TEST_CASE("Boot-order lines already in the recovery config are replaced, not repeated",
          "[firmware]")
{
    // config.txt is read top to bottom and set_boot_order has to be seen
    // before recovery_reboot, or the bootloader reboots before applying the
    // override and the board comes up in normal boot instead of rpiboot.
    // Upstream's own config may carry either key, so they are stripped and
    // re-appended in order rather than left where they were found.
    ScratchDir dir;
    const fs::path versionDir = dir.path() / "v1";
    const fs::path configPath = versionDir / "secure-boot-recovery5" / "config.txt";
    writeFile(configPath,
              "recovery_reboot=1\n"
              "  set_boot_order=0x1\n"
              "arm_64bit=1\r\n"
              "set_boot_order=0xf41\n");

    TestableFirmwareManager fm;
    REQUIRE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));

    std::ifstream in(configPath);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);

    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "arm_64bit=1");   // the CR was stripped with it
    CHECK(lines[1] == "set_boot_order=0x3");
    CHECK(lines[2] == "recovery_reboot=1");
}

TEST_CASE("A recovery config that cannot be read or rewritten is reported", "[firmware]")
{
    if (::geteuid() == 0)
        SKIP("running as root, which the mode bits do not stop");

    SECTION("unreadable") {
        ScratchDir dir;
        const fs::path versionDir = dir.path() / "v1";
        const fs::path configPath = versionDir / "secure-boot-recovery5" / "config.txt";
        writeFile(configPath, "arm_64bit=1\n");
        REQUIRE(::chmod(configPath.c_str(), 0000) == 0);

        TestableFirmwareManager fm;
        CHECK_FALSE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));
        CHECK(fm.lastError().find("Cannot read") != std::string::npos);

        ::chmod(configPath.c_str(), 0600);
    }

    SECTION("read-only") {
        // Readable, so the upstream lines are gathered, and then the rewrite
        // is refused. Silently keeping the original would send a board into
        // normal boot when the user asked for recovery.
        ScratchDir dir;
        const fs::path versionDir = dir.path() / "v1";
        const fs::path configPath = versionDir / "secure-boot-recovery5" / "config.txt";
        writeFile(configPath, "arm_64bit=1\n");
        REQUIRE(::chmod(configPath.c_str(), 0400) == 0);

        TestableFirmwareManager fm;
        CHECK_FALSE(fm.ensureSbrReenumerates(versionDir, ChipGeneration::BCM2712));
        CHECK(fm.lastError().find("Cannot rewrite") != std::string::npos);

        ::chmod(configPath.c_str(), 0600);
    }
}

// ══════════════════════════════════════════════════════════════
// The whole download-and-cache path, against a server of our own
//
// ensureAvailable() is the largest thing in this class and had never run:
// it fetches from github.com, so nothing short of real network access could
// reach it. The base URLs are already virtual accessors put there for this
// purpose -- overriding them points the manifest, the downloads, the cache
// validation, the bootcode extraction and the counter-signing at a local
// directory.

namespace {

// The same class, pointed at a local tree instead of GitHub.
class LocalSourceFirmwareManager : public TestableFirmwareManager
{
public:
    LocalSourceFirmwareManager(QString base, fs::path cache)
        : _base(std::move(base)), _cache(std::move(cache)) {}

protected:
    std::string usbbootBase() const override { return _base.toStdString(); }
    std::string eepromBase() const override { return _base.toStdString(); }
    std::string provisionerBase() const override { return _base.toStdString(); }

    // A cache of its own, not the developer's. Sharing the real one made
    // these pass in 0.13 s off a directory a previous run had filled --
    // every assertion held and the download path never ran.
    fs::path cacheRoot() const override { return _cache; }

private:
    QString _base;
    fs::path _cache;
};

// A bootfiles tar carrying the chip's bootcode where the extractor looks.
void writeBootfilesTar(const fs::path &path, const std::string &entry)
{
    fs::create_directories(path.parent_path());
    ::archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_open_filename(a, path.c_str());

    const std::string payload(4096, '\xa5');
    ::archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, entry.c_str());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_size(e, static_cast<la_int64_t>(payload.size()));
    archive_write_header(a, e);
    archive_write_data(a, payload.data(), payload.size());
    archive_entry_free(e);

    archive_write_close(a);
    archive_write_free(a);
}

} // namespace

TEST_CASE("A fastboot firmware set is fetched, cached and validated",
          "[firmware][ensure]")
{
    if (!rpi_test::havePython())
        SKIP("python3 is not installed, so no local server can be started");

    ScratchDir served;
    // Laid out as the upstream repositories are, because the manifest builds
    // its URLs from those paths.
    writeFile(served.path() / "host-support" / "fastboot-gadget.img", "gadget bytes");
    writeFile(served.path() / "mass-storage-gadget64" / "config.txt", "arm_64bit=1\n");
    writeBootfilesTar(served.path() / "firmware" / "bootfiles.bin", "2712/bootcode5.bin");

    rpi_test::LocalHttpServer server(QString::fromStdString(served.path().string()));
    REQUIRE_HTTP_SERVER(server);

    ScratchDir cache;
    const QString base = QString::fromUtf8(server.urlFor(QString()));
    LocalSourceFirmwareManager fm(base, cache.path() / "fw");

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(SideloadMode::Fastboot,
                                        ChipGeneration::BCM2712, nullptr, cancelled);

    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    // Every manifest file landed where the file server will look for it.
    CHECK(fs::exists(dir / "fastboot" / "fastboot-gadget.img"));
    CHECK(fs::exists(dir / "fastboot" / "config.txt"));
    CHECK(fs::exists(dir / "fastboot" / "bootfiles.bin"));

    // And the bootcode was extracted out of the tar rather than downloaded,
    // which is the only way it arrives for this chip.
    REQUIRE(fs::exists(dir / "bootcode5.bin"));
    CHECK(fs::file_size(dir / "bootcode5.bin") == 4096);

    // The cache it just built satisfies the check the write path makes.
    CHECK(fm.validateCacheForDevice(dir, SideloadMode::Fastboot,
                                    ChipGeneration::BCM2712));
}

TEST_CASE("A fastboot set is counter-signed and re-packed for a fused board",
          "[firmware][ensure]")
{
    // With a signing key set and a BCM2712, the bootcode is re-signed and
    // spliced back into the tar the board is served -- the ROM enforces
    // customer signing on the copy inside bootfiles.bin, not just the one
    // uploaded over USB.
    if (!rpi_test::havePython())
        SKIP("python3 is not installed, so no local server can be started");
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/openssl")))
        SKIP("openssl is not installed, so no key can be generated");

    ScratchDir served;
    writeFile(served.path() / "host-support" / "fastboot-gadget.img", "gadget bytes");
    writeFile(served.path() / "mass-storage-gadget64" / "config.txt", "arm_64bit=1\n");
    writeBootfilesTar(served.path() / "firmware" / "bootfiles.bin", "2712/bootcode5.bin");

    rpi_test::LocalHttpServer server(QString::fromStdString(served.path().string()));
    REQUIRE_HTTP_SERVER(server);

    ScratchDir keys;
    const auto key = keys.path() / "signing.pem";
    QProcess gen;
    gen.start(QStringLiteral("/usr/bin/openssl"),
              {QStringLiteral("genrsa"), QStringLiteral("-out"),
               QString::fromStdString(key.string()), QStringLiteral("2048")});
    REQUIRE(gen.waitForFinished(60000));
    REQUIRE(gen.exitCode() == 0);

    ScratchDir cache;
    LocalSourceFirmwareManager fm(QString::fromUtf8(server.urlFor(QString())),
                                  cache.path() / "fw");
    fm.setSignFastbootGadgetKey(key.string());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(SideloadMode::Fastboot,
                                        ChipGeneration::BCM2712, nullptr, cancelled);

    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    // A pristine copy of the upstream tar is kept, so a second run re-signs
    // from that rather than signing an already-signed blob.
    CHECK(fs::exists(dir / "fastboot" / "bootfiles.bin.original"));

    // The bootcode on disk grew: a counter-signature was appended.
    REQUIRE(fs::exists(dir / "bootcode5.bin"));
    CHECK(fs::file_size(dir / "bootcode5.bin") > 4096);

    // And the copy inside the re-packed tar is the signed one, not the
    // upstream bytes -- that is the whole point of the re-pack.
    rpiboot::Bootfiles repacked;
    REQUIRE(repacked.extractFromFile((dir / "fastboot" / "bootfiles.bin").string()));
    const auto *inTar = repacked.find("2712/bootcode5.bin");
    REQUIRE(inTar != nullptr);
    CHECK(inTar->size() == fs::file_size(dir / "bootcode5.bin"));

    // Running it again must not sign the signature.
    const auto again = fm.ensureAvailable(SideloadMode::Fastboot,
                                          ChipGeneration::BCM2712, nullptr, cancelled);
    REQUIRE_FALSE(again.empty());
    CHECK(fs::file_size(again / "bootcode5.bin") == fs::file_size(dir / "bootcode5.bin"));
}

namespace {

// A minimal EEPROM image in the format BootloaderImage parses: a bootcode
// blob, filler to the read-only boundary, then the three sections the
// provisioner rewrites. Mirrors the fixture in
// secure_boot_provisioner_test.cpp, which cannot be shared without making a
// header of it.
void writeSyntheticEeprom(const fs::path &path)
{
    constexpr uint32_t kMagic     = 0x55aaf00f;
    constexpr uint32_t kPadMagic  = 0x55aafeef;
    constexpr uint32_t kFileMagic = 0x55aaf11f;
    constexpr size_t   kImageSize = 2 * 1024 * 1024;
    // The bootcode region has to reserve at least 128 KiB or the signed
    // blob cannot be embedded -- BootloaderImage refuses with "Bootcode
    // reserved region < 128 KiB", which is what a 4 KiB fixture got.
    constexpr size_t   kBootcode   = 160 * 1024;
    constexpr size_t   kReadOnly   = 320 * 1024;

    std::vector<uint8_t> img(kImageSize, 0xff);
    const auto putBe32 = [&img](size_t off, uint32_t v) {
        img[off + 0] = uint8_t((v >> 24) & 0xff);
        img[off + 1] = uint8_t((v >> 16) & 0xff);
        img[off + 2] = uint8_t((v >>  8) & 0xff);
        img[off + 3] = uint8_t( v        & 0xff);
    };

    const std::vector<uint8_t> bootcode(kBootcode, 0xAA);
    putBe32(0, kMagic);
    putBe32(4, uint32_t(bootcode.size()));
    std::memcpy(&img[8], bootcode.data(), bootcode.size());
    size_t off = 8 + bootcode.size();
    while (off % 8 != 0)
        img[off++] = 0xff;

    putBe32(off, kPadMagic);
    putBe32(off + 4, uint32_t(kReadOnly - (off + 8)));
    off = kReadOnly;

    for (const auto &[name, reserve] : std::initializer_list<std::pair<const char *, size_t>>{
             {"bootconf.txt", 4096}, {"bootconf.sig", 4096}, {"pubkey.bin", 1024}}) {
        const uint32_t length = uint32_t(reserve + 12 + 4);
        putBe32(off + 0, kFileMagic);
        putBe32(off + 4, length);
        std::memset(&img[off + 8], 0, 16);
        std::memcpy(&img[off + 8], name, std::strlen(name));
        std::memset(&img[off + 24], 0, reserve);
        size_t end = off + 8 + length;
        while (end % 8 != 0)
            img[end++] = 0xff;
        off = end;
    }

    fs::create_directories(path.parent_path());
    QFile f(QString::fromStdString(path.string()));
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(reinterpret_cast<const char *>(img.data()), qint64(img.size()));
}

} // namespace

TEST_CASE("A secure-boot recovery set resolves its version and signs the EEPROM",
          "[firmware][ensure]")
{
    // The other half of ensureAvailable, and the one that writes to OTP.
    // Secure-boot recovery has three steps nothing had covered: the dated
    // pieeprom filename has to be resolved from versions.txt before the
    // manifest can be built, the bootcode is re-baselined from
    // recovery.original.bin so a second run does not sign a signed blob,
    // and the EEPROM itself is signed with the customer key.
    //
    // Getting any of it wrong leaves a board that has had its OTP fused
    // and will not boot, which is the least recoverable failure in the
    // application.
    if (!rpi_test::havePython())
        SKIP("python3 is not installed, so no local server can be started");
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/openssl")))
        SKIP("openssl is not installed, so no key can be generated");

    ScratchDir served;
    // versions.txt, newest first, with an archived row that must be passed
    // over -- an `old` build is not guaranteed to exist under latest/.
    writeFile(served.path() / "firmware-2712" / "versions.txt",
              "# version build_epoch git_hash release\n"
              "2026-09-01 1788000000 abc123 old\n"
              "2026-08-15 1787000000 def456 latest\n");

    // The dated file the resolved version names, plus the rest of the set.
    writeSyntheticEeprom(served.path() / "firmware-2712" / "latest"
                         / "pieeprom-2026-08-15.bin");
    writeFile(served.path() / "firmware-2712" / "latest" / "recovery.bin",
              std::string(4096, '\x5a'));
    writeFile(served.path() / "secure-boot-recovery5" / "boot.conf", "[all]\n");
    writeFile(served.path() / "secure-boot-recovery5" / "config.txt", "arm_64bit=1\n");

    rpi_test::LocalHttpServer server(QString::fromStdString(served.path().string()));
    REQUIRE_HTTP_SERVER(server);

    ScratchDir keys;
    const auto key = keys.path() / "customer.pem";
    QProcess gen;
    gen.start(QStringLiteral("/usr/bin/openssl"),
              {QStringLiteral("genrsa"), QStringLiteral("-out"),
               QString::fromStdString(key.string()), QStringLiteral("2048")});
    REQUIRE(gen.waitForFinished(60000));
    REQUIRE(gen.exitCode() == 0);

    ScratchDir cache;
    LocalSourceFirmwareManager fm(QString::fromUtf8(server.urlFor(QString())),
                                  cache.path() / "fw");
    fm.setSignFastbootGadgetKey(key.string());

    std::atomic<bool> cancelled{false};
    const auto dir = fm.ensureAvailable(SideloadMode::SecureBootRecovery,
                                        ChipGeneration::BCM2712, nullptr, cancelled);

    INFO("error: " << fm.lastError());
    REQUIRE_FALSE(dir.empty());

    const auto sub = dir / "secure-boot-recovery5";

    // The archived row was skipped and the dated filename resolved from the
    // one tagged latest -- a version fetch that picked `old` would 404.
    CHECK(fs::exists(sub / "pieeprom.original.bin"));

    // The signed EEPROM and its signature, which the recovery verifies
    // before it will flash anything.
    CHECK(fs::exists(sub / "pieeprom.bin"));
    CHECK(fs::exists(sub / "pieeprom.sig"));

    // config.txt was rewritten so the board comes back into rpiboot rather
    // than booting normally after the EEPROM write.
    QFile config(QString::fromStdString((sub / "config.txt").string()));
    REQUIRE(config.open(QIODevice::ReadOnly));
    const QByteArray body = config.readAll();
    CHECK(body.contains("set_boot_order=0x3"));
    CHECK(body.contains("recovery_reboot=1"));

    // And the bootcode was counter-signed from the unsigned baseline.
    REQUIRE(fs::exists(dir / "bootcode5.bin"));
    CHECK(fs::file_size(dir / "bootcode5.bin") > 4096);

    // A second run re-baselines rather than signing the signature.
    const auto again = fm.ensureAvailable(SideloadMode::SecureBootRecovery,
                                          ChipGeneration::BCM2712, nullptr, cancelled);
    REQUIRE_FALSE(again.empty());
    CHECK(fs::file_size(again / "bootcode5.bin") == fs::file_size(dir / "bootcode5.bin"));
}
