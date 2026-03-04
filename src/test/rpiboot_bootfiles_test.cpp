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

#include <cstring>
#include <vector>

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
