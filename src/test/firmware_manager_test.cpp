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
//
// What this covers is what a user meets when setting up a Compute Module: the
// firmware either arrives and is cached, or the failure is reported rather
// than the board being left half-provisioned.

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
//
// The function had no tests at all.
// ══════════════════════════════════════════════════════════════════════════

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
//
// Without that, a Compute Module provisioned after an rpi-eeprom release
// gets whatever bootloader firmware happened to be cached first, and there
// is nothing on the device or in the log to say so. Neither the purge nor
// the sidecar that drives it was tested.
// ══════════════════════════════════════════════════════════════

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
    //
    // Comparing file contents cannot show this, because the test server has
    // no ETag support and so serves a fresh 200 every time; the cached copy
    // is rewritten either way. Taking the payload off the server is what
    // separates them. With the version unchanged the file is not purged, the
    // download fails, and ensureAvailable() proceeds with what it already
    // has. Had the purge fired there would be nothing left to proceed with.
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
    //
    // Downloads degrade gracefully: a network failure on a file that is
    // already cached proceeds with the cached copy rather than failing the
    // run. That is right for a file whose content does not depend on the
    // version -- and wrong for pieeprom, whose cached copy belongs to the
    // release it was fetched for. Left alone, a resolved-but-unreachable new
    // version would fall back to the previous bootloader and write it to the
    // device's EEPROM, reporting success and leaving nothing to say which
    // firmware the Compute Module actually got.
    //
    // Purging on a version change removes the fallback, so the run fails
    // where it can still be seen.
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
//
// The rest is what happens when the source cannot be reached, which for this
// class is the normal case rather than the exceptional one: firmware is
// fetched once and then used offline for as long as the cache survives.
// ══════════════════════════════════════════════════════════════════════════

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
