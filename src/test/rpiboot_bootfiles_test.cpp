/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Unit tests for the rpiboot bootfiles tar extraction.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "rpiboot/bootfiles.h"

#include <archive.h>
#include <archive_entry.h>

#include <sys/resource.h>
#include <csignal>
#include <cstring>
#include <vector>
#include <QTemporaryDir>
#include <QDir>

using namespace rpiboot;

// Helper: create a tar archive in memory with the given files
static std::vector<uint8_t> createTarInMemory(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files)
{
    std::vector<uint8_t> result;
    // Use a pre-allocated buffer via archive_write
    ::archive* a = archive_write_new();
    archive_write_set_format_ustar(a);

    // Pre-allocate buffer (2MB should be more than enough for test data)
    constexpr size_t kBufSize = 2 * 1024 * 1024;
    std::vector<uint8_t> buf(kBufSize);
    size_t usedSize = 0;
    archive_write_open_memory(a, buf.data(), kBufSize, &usedSize);

    for (const auto& [name, data] : files) {
        ::archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_size(entry, static_cast<la_int64_t>(data.size()));
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);

        archive_write_header(a, entry);
        if (!data.empty())
            archive_write_data(a, data.data(), data.size());

        archive_entry_free(entry);
    }

    archive_write_close(a);

    result.assign(buf.data(), buf.data() + usedSize);

    archive_write_free(a);

    return result;
}

TEST_CASE("Bootfiles extracts tar from memory", "[rpiboot][bootfiles]")
{
    std::vector<uint8_t> contentA = {'H', 'e', 'l', 'l', 'o'};
    std::vector<uint8_t> contentB = {0x00, 0x01, 0x02, 0x03};

    auto tar = createTarInMemory({
        {"config.txt", contentA},
        {"kernel.img", contentB},
    });

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    CHECK(bf.size() == 2);

    auto* a = bf.find("config.txt");
    REQUIRE(a != nullptr);
    CHECK(*a == contentA);

    auto* b = bf.find("kernel.img");
    REQUIRE(b != nullptr);
    CHECK(*b == contentB);
}

TEST_CASE("Bootfiles handles empty archive", "[rpiboot][bootfiles]")
{
    // Empty tar data
    Bootfiles bf;
    CHECK_FALSE(bf.extractFromMemory({}));
}

TEST_CASE("Bootfiles returns nullptr for missing file", "[rpiboot][bootfiles]")
{
    auto tar = createTarInMemory({{"exists.txt", {1, 2, 3}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    CHECK(bf.find("exists.txt") != nullptr);
    CHECK(bf.find("missing.txt") == nullptr);
}

TEST_CASE("Bootfiles strips leading ./ from entry names", "[rpiboot][bootfiles]")
{
    auto tar = createTarInMemory({{"./config.txt", {1, 2, 3}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    // Should be findable without the "./" prefix
    CHECK(bf.find("config.txt") != nullptr);
}

TEST_CASE("Bootfiles handles large file without OOM", "[rpiboot][bootfiles]")
{
    // Create a 1 MB file -- large enough to test streaming but not slow
    std::vector<uint8_t> largeContent(1024 * 1024, 0xAA);
    auto tar = createTarInMemory({{"large.bin", largeContent}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    auto* data = bf.find("large.bin");
    REQUIRE(data != nullptr);
    CHECK(data->size() == largeContent.size());
    CHECK((*data)[0] == 0xAA);
    CHECK((*data)[data->size() - 1] == 0xAA);
}

// ────────────────────────────────────────────────────────────────────────
// Negative tests
// ────────────────────────────────────────────────────────────────────────

TEST_CASE("Bootfiles does not crash on corrupt data", "[rpiboot][bootfiles][negative]")
{
    // Random garbage bytes — not a valid tar.
    // Because extractFromArchive uses archive_read_support_format_raw(),
    // libarchive may interpret any input as a single raw entry. The key
    // property: it must not crash or produce unexpected named files.
    std::vector<uint8_t> garbage(512, 0xDE);
    garbage[0] = 0xBA;
    garbage[1] = 0xAD;

    Bootfiles bf;
    bool ok = bf.extractFromMemory(garbage);

    // Should not crash. If it "succeeds", no named files from a real
    // tar should be present.
    if (ok) {
        CHECK(bf.find("config.txt") == nullptr);
        CHECK(bf.find("kernel.img") == nullptr);
    }
}

TEST_CASE("Bootfiles does not crash on truncated data", "[rpiboot][bootfiles][negative]")
{
    // A tar header is 512 bytes; send fewer
    std::vector<uint8_t> truncated(100, 0x00);
    truncated[0] = 'f';

    Bootfiles bf;
    bool ok = bf.extractFromMemory(truncated);

    // Should not crash. The raw format handler may produce an entry,
    // but no real tar filenames should be resolvable.
    if (ok) {
        CHECK(bf.find("config.txt") == nullptr);
    }
}

TEST_CASE("Bootfiles populates lastError on empty archive", "[rpiboot][bootfiles][negative]")
{
    Bootfiles bf;
    CHECK_FALSE(bf.extractFromMemory({}));
    CHECK_FALSE(bf.lastError().empty());
}

TEST_CASE("Bootfiles find with ./ prefix and without are equivalent", "[rpiboot][bootfiles]")
{
    // Store file without prefix, look up with prefix
    auto tar = createTarInMemory({{"kernel.img", {0x01, 0x02}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    // Direct lookup
    CHECK(bf.find("kernel.img") != nullptr);
    // With ./ prefix — should still find via the fallback logic in find()
    CHECK(bf.find("./kernel.img") != nullptr);
}

TEST_CASE("Bootfiles extracts multiple files and iterates", "[rpiboot][bootfiles]")
{
    auto tar = createTarInMemory({
        {"a.txt", {1}},
        {"b.txt", {2}},
        {"c.txt", {3}},
    });

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    CHECK(bf.size() == 3);

    // Verify files() accessor gives the full map
    const auto& map = bf.files();
    CHECK(map.size() == 3);
    CHECK(map.count("a.txt") == 1);
    CHECK(map.count("b.txt") == 1);
    CHECK(map.count("c.txt") == 1);
}

TEST_CASE("Bootfiles find resolves chip-specific subdirectory via prefix", "[rpiboot][bootfiles]")
{
    std::vector<uint8_t> mcbContent = {0xDE, 0xAD};
    std::vector<uint8_t> topLevel = {0xBE, 0xEF};

    auto tar = createTarInMemory({
        {"2712/mcb.bin", mcbContent},
        {"config.txt", topLevel},
    });

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    // Exact name works for top-level files
    CHECK(bf.find("config.txt") != nullptr);

    // Bare name without prefix → not found
    CHECK(bf.find("mcb.bin") == nullptr);

    // With correct chip prefix → found via "2712/mcb.bin"
    auto* found = bf.find("mcb.bin", "2712");
    REQUIRE(found != nullptr);
    CHECK(*found == mcbContent);

    // Wrong chip prefix → not found
    CHECK(bf.find("mcb.bin", "2711") == nullptr);

    // Exact path still works regardless of prefix
    CHECK(bf.find("2712/mcb.bin") != nullptr);
}

TEST_CASE("Bootfiles extractFromMemory clears previous state", "[rpiboot][bootfiles]")
{
    auto tar1 = createTarInMemory({{"first.txt", {1}}});
    auto tar2 = createTarInMemory({{"second.txt", {2}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar1));
    CHECK(bf.find("first.txt") != nullptr);

    // Extract a second archive — should clear the first
    REQUIRE(bf.extractFromMemory(tar2));
    CHECK(bf.find("first.txt") == nullptr);
    CHECK(bf.find("second.txt") != nullptr);
    CHECK(bf.size() == 1);
}

// ── Rewriting the archive ───────────────────────────────────────────────────
//
// The imager does not just read the bootfiles archive: for a secure-boot
// board it replaces the fastboot gadget inside it with a counter-signed copy
// and writes the whole thing back out. Everything downstream boots from what
// comes out of here, so a replacement that silently does not take, or a
// rewrite that loses the other entries, produces a board that will not start
// with nothing to say why.

TEST_CASE("A replaced entry survives a write and re-read", "[rpiboot][bootfiles]")
{
    const std::vector<uint8_t> original = {'o', 'l', 'd'};
    const std::vector<uint8_t> other    = {'k', 'e', 'e', 'p'};
    const std::vector<uint8_t> signed_  = {'s', 'i', 'g', 'n', 'e', 'd'};

    auto tar = createTarInMemory({
        {"boot.img", original},
        {"config.txt", other},
    });

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    REQUIRE(bf.replaceEntry("boot.img", signed_));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string path = QDir(dir.path()).filePath(QStringLiteral("out.tar")).toStdString();
    REQUIRE(bf.writeToFile(path));

    Bootfiles reread;
    REQUIRE(reread.extractFromFile(path));

    auto* replaced = reread.find("boot.img");
    REQUIRE(replaced != nullptr);
    CHECK(*replaced == signed_);

    // The entries that were not touched have to come back intact, or the
    // board boots a signed gadget and nothing else.
    auto* untouched = reread.find("config.txt");
    REQUIRE(untouched != nullptr);
    CHECK(*untouched == other);

    CHECK(reread.size() == 2);
}

TEST_CASE("Replacing an entry that is not there is refused", "[rpiboot][bootfiles]")
{
    // Silently succeeding would mean the unsigned gadget gets shipped to the
    // board while the caller believes it swapped in the signed one.
    auto tar = createTarInMemory({{"config.txt", {'x'}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    CHECK_FALSE(bf.replaceEntry("absent.img", {'y'}));
    CHECK_FALSE(bf.lastError().empty());
    CHECK(bf.size() == 1);
}

TEST_CASE("Replacing works through the ./ prefix too", "[rpiboot][bootfiles]")
{
    // find() treats "./name" and "name" as the same entry; replaceEntry has
    // to agree, or a tar written with ./ prefixes silently refuses every
    // replacement.
    const std::vector<uint8_t> replacement = {'n', 'e', 'w'};
    auto tar = createTarInMemory({{"./boot.img", {'o', 'l', 'd'}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    REQUIRE(bf.replaceEntry("boot.img", replacement));

    auto* got = bf.find("boot.img");
    REQUIRE(got != nullptr);
    CHECK(*got == replacement);
}

TEST_CASE("A replacement of a different size is written correctly",
          "[rpiboot][bootfiles]")
{
    // A counter-signed gadget is bigger than the original. The tar header
    // records the size, so writing the new bytes under the old size would
    // truncate it or run into the next entry.
    const std::vector<uint8_t> small(64, 0x11);
    const std::vector<uint8_t> large(9000, 0x22);

    auto tar = createTarInMemory({{"boot.img", small}, {"after.txt", {'z'}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    REQUIRE(bf.replaceEntry("boot.img", large));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string path = QDir(dir.path()).filePath(QStringLiteral("grown.tar")).toStdString();
    REQUIRE(bf.writeToFile(path));

    Bootfiles reread;
    REQUIRE(reread.extractFromFile(path));

    auto* grown = reread.find("boot.img");
    REQUIRE(grown != nullptr);
    CHECK(grown->size() == large.size());
    CHECK(*grown == large);

    auto* after = reread.find("after.txt");
    REQUIRE(after != nullptr);
    CHECK(after->size() == 1);
}

TEST_CASE("Writing to a path that cannot be created is reported",
          "[rpiboot][bootfiles][negative]")
{
    auto tar = createTarInMemory({{"config.txt", {'x'}}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    CHECK_FALSE(bf.writeToFile("/nonexistent-rpi-imager-dir-8f21/out.tar"));
    CHECK_FALSE(bf.lastError().empty());
}

TEST_CASE("Reading an archive that is not there is reported",
          "[rpiboot][bootfiles][negative]")
{
    Bootfiles bf;
    CHECK_FALSE(bf.extractFromFile("/nonexistent-rpi-imager-dir-8f21/in.tar"));
    CHECK_FALSE(bf.lastError().empty());
    CHECK(bf.size() == 0);
}

// A short write leaves a half-written archive sitting on disk. Reported as
// success, that gets signed and served to a board as its firmware, and the
// board is what discovers the truncation.
//
// RLIMIT_FSIZE makes the write fail without needing a full filesystem or
// root. SIGXFSZ has to be ignored first: the default action is to kill the
// process, so without this the test dies instead of the write returning an
// error.
TEST_CASE("An archive that will not fit on disk is reported rather than truncated",
          "[rpiboot][bootfiles]")
{
    const std::vector<uint8_t> small = {'s', 'm', 'a', 'l', 'l'};
    // Comfortably past the limit set below, so the failure lands in the entry
    // data rather than the header.
    const std::vector<uint8_t> big(4u * 1024 * 1024, 'B');

    auto tar = createTarInMemory({{"config.txt", small}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    REQUIRE(bf.replaceEntry("config.txt", big));

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string path =
        QDir(dir.path()).filePath(QStringLiteral("out.tar")).toStdString();

    struct rlimit saved{};
    REQUIRE(getrlimit(RLIMIT_FSIZE, &saved) == 0);

    struct sigaction ignore{};
    struct sigaction previous{};
    ignore.sa_handler = SIG_IGN;
    REQUIRE(sigaction(SIGXFSZ, &ignore, &previous) == 0);

    struct rlimit tiny = saved;
    tiny.rlim_cur = 64 * 1024;
    const bool limited = setrlimit(RLIMIT_FSIZE, &tiny) == 0;

    bool wrote = true;
    if (limited)
        wrote = bf.writeToFile(path);

    // Put the process back before asserting, so a failure does not leave the
    // limit in place for every case that follows.
    if (limited)
        setrlimit(RLIMIT_FSIZE, &saved);
    sigaction(SIGXFSZ, &previous, nullptr);

    if (!limited)
        SKIP("RLIMIT_FSIZE could not be lowered, so no short write can be forced");

    CHECK_FALSE(wrote);
    INFO("error: " << bf.lastError());
    CHECK_FALSE(bf.lastError().empty());
}

// A tar with directory entries in it. rpi-eeprom firmware archives are laid
// out per chip generation -- "2712/bootcode5.bin" -- so the directory entries
// are there in the real thing, and an extractor that treated one as a file
// would hand a board an empty payload under a name it expects to boot from.
TEST_CASE("Directory entries in an archive are stepped over", "[rpiboot][bootfiles]")
{
    std::vector<uint8_t> result(2u * 1024 * 1024);
    size_t used = 0;
    {
        ::archive* a = archive_write_new();
        archive_write_set_format_ustar(a);
        archive_write_open_memory(a, result.data(), result.size(), &used);

        ::archive_entry* dir = archive_entry_new();
        archive_entry_set_pathname(dir, "2712");
        archive_entry_set_filetype(dir, AE_IFDIR);
        archive_entry_set_perm(dir, 0755);
        archive_entry_set_size(dir, 0);
        archive_write_header(a, dir);
        archive_entry_free(dir);

        const std::string payload = "bootcode for the 2712";
        ::archive_entry* file = archive_entry_new();
        archive_entry_set_pathname(file, "2712/bootcode5.bin");
        archive_entry_set_filetype(file, AE_IFREG);
        archive_entry_set_perm(file, 0644);
        archive_entry_set_size(file, static_cast<la_int64_t>(payload.size()));
        archive_write_header(a, file);
        archive_write_data(a, payload.data(), payload.size());
        archive_entry_free(file);

        archive_write_close(a);
        archive_write_free(a);
    }
    result.resize(used);

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(result));

    // The file came through with its contents, and the directory did not
    // arrive as a zero-length entry of its own.
    const auto* found = bf.find("2712/bootcode5.bin");
    REQUIRE(found != nullptr);
    CHECK(std::string(found->begin(), found->end()) == "bootcode for the 2712");
    CHECK(bf.find("2712") == nullptr);
}

// GNU tar writes "./config.txt" when told to archive a directory, and the
// rpi-eeprom packages are built that way. The device asks for "config.txt".
// find() strips a leading "./" and also adds one, and it is the adding half
// that an archive from tar exercises -- without it the board is told the
// file it needs is not in the package it was just sent.
TEST_CASE("A file stored with a leading ./ answers to its bare name", "[rpiboot][bootfiles]")
{
    const std::vector<uint8_t> payload = {'c', 'f', 'g'};
    auto tar = createTarInMemory({{"./config.txt", payload}});

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));

    const auto* found = bf.find("config.txt");
    REQUIRE(found != nullptr);
    CHECK(*found == payload);

    // The stored name still works, and neither spelling invents an entry.
    CHECK(bf.find("./config.txt") != nullptr);
    CHECK(bf.find("cfg.txt") == nullptr);
}

// Helper: the same archive as createTarInMemory but in pax, which can carry
// a pathname longer than USTAR's 100 characters.
static std::vector<uint8_t> createPaxTarInMemory(
    const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files)
{
    constexpr size_t kBufSize = 2 * 1024 * 1024;
    std::vector<uint8_t> buf(kBufSize);
    size_t usedSize = 0;

    ::archive* a = archive_write_new();
    archive_write_set_format_pax(a);
    archive_write_open_memory(a, buf.data(), kBufSize, &usedSize);

    for (const auto& [name, data] : files) {
        ::archive_entry* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_size(entry, static_cast<la_int64_t>(data.size()));
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_write_header(a, entry);
        if (!data.empty())
            archive_write_data(a, data.data(), data.size());
        archive_entry_free(entry);
    }

    archive_write_close(a);
    archive_write_free(a);

    buf.resize(usedSize);
    return buf;
}

// Read side and write side do not agree on what a name may be: extraction
// accepts pax, which carries any length, and writeToFile re-packs as USTAR,
// which cannot store a pathname over 100 characters that has nowhere to
// split. The repack is what a counter-signed bootcode is spliced into, so
// the failure lands between signing the firmware and serving it. It has to
// be reported rather than leave a half-written archive behind looking whole.
TEST_CASE("A name USTAR cannot hold fails the repack, with the name in the error",
          "[rpiboot][bootfiles]")
{
    const std::string longName(150, 'f');  // no '/', so ustar cannot split it
    auto tar = createPaxTarInMemory({
        {"config.txt", {'o', 'k'}},
        {longName, {'n', 'o'}},
    });

    Bootfiles bf;
    REQUIRE(bf.extractFromMemory(tar));
    REQUIRE(bf.find(longName) != nullptr);

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::string out = (dir.path() + "/repacked.tar").toStdString();

    CHECK_FALSE(bf.writeToFile(out));
    // Which entry stopped it, not just that something did.
    CHECK(bf.lastError().find(longName) != std::string::npos);
}

// A package that was cut short in transfer. The header block arrived whole,
// so the entry announces a size, and the bytes behind it are not there. Read
// as far as it goes and stop: the alternative is serving the board a file
// padded out with whatever the buffer held.
TEST_CASE("An entry whose data was cut short does not come through whole",
          "[rpiboot][bootfiles]")
{
    auto tar = createTarInMemory({{"bootcode5.bin", std::vector<uint8_t>(4096, 0x5A)}});
    REQUIRE(tar.size() > 1024);

    // Keep the header block and one block of payload; drop the rest.
    tar.resize(1024);

    Bootfiles bf;
    const bool ok = bf.extractFromMemory(tar);
    if (ok) {
        // libarchive stopped at the short read rather than reporting one.
        const auto* found = bf.find("bootcode5.bin");
        REQUIRE(found != nullptr);
        CHECK(found->size() < 4096);
    } else {
        CHECK_FALSE(bf.lastError().empty());
    }
}
