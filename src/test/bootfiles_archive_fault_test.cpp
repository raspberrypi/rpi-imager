/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What Bootfiles::writeToFile() does when libarchive will not take the write.
 *
 * The file it writes is bootfiles.bin, served to a board over rpiboot. One
 * written short still looks like a file, so a refusal reported as success is
 * a board that does not come up and nothing saying why.
 *
 * Its own binary: the outcomes are forced by linker flags that apply to
 * everything linked with it. See bootfiles_archive_fault.h.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "bootfiles_archive_fault.h"
#include "rpiboot/bootfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <archive.h>
#include <archive_entry.h>

#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using rpiboot::Bootfiles;

namespace {

// A tar in memory for Bootfiles to read back, so a case starts from an
// archive that is genuinely populated rather than an empty one.
std::vector<uint8_t> tarOf(const std::string &name, const std::vector<uint8_t> &data)
{
    struct archive *a = archive_write_new();
    archive_write_set_format_ustar(a);
    archive_write_add_filter_none(a);

    std::vector<uint8_t> out(64 * 1024);
    size_t used = 0;
    REQUIRE(archive_write_open_memory(a, out.data(), out.size(), &used) == ARCHIVE_OK);

    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, name.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    REQUIRE(archive_write_header(a, e) == ARCHIVE_OK);
    REQUIRE(archive_write_data(a, data.data(), data.size())
            == static_cast<la_ssize_t>(data.size()));
    archive_entry_free(e);

    REQUIRE(archive_write_close(a) == ARCHIVE_OK);
    archive_write_free(a);

    out.resize(used);
    return out;
}

// A populated Bootfiles and somewhere to write it.
struct Fixture {
    QTemporaryDir dir;
    Bootfiles bf;
    std::string path;

    Fixture()
    {
        REQUIRE(dir.isValid());
        // Two blocks, so a short write has something left to fall short of.
        const std::vector<uint8_t> payload(4096, 0x5a);
        REQUIRE(bf.extractFromMemory(tarOf("boot.img", payload)));
        path = QDir(dir.path()).filePath(QStringLiteral("bootfiles.bin")).toStdString();
    }
};

}  // namespace

TEST_CASE("A bootfiles archive is written when nothing refuses it",
          "[rpiboot][bootfiles][fault]")
{
    // The control. Without it a refusal below could be the fixture failing to
    // build an archive at all, and every case would pass for the wrong reason.
    Fixture f;
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::None);
    CHECK(f.bf.writeToFile(f.path));
    CHECK(QFileInfo(QString::fromStdString(f.path)).size() > 0);
}

TEST_CASE("A bootfiles entry whose header is refused is reported",
          "[rpiboot][bootfiles][fault]")
{
    Fixture f;
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::Header);
    const bool ok = f.bf.writeToFile(f.path);
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::None);

    INFO("error: " << f.bf.lastError());
    CHECK_FALSE(ok);
    // Named, because the caller puts this in front of somebody whose board
    // did not boot.
    CHECK_THAT(f.bf.lastError(), ContainsSubstring("header"));
    CHECK_THAT(f.bf.lastError(), ContainsSubstring("boot.img"));
}

TEST_CASE("A bootfiles entry the archive only half takes is not called written",
          "[rpiboot][bootfiles][fault]")
{
    // The one that matters most: a short write leaves a file of very nearly
    // the right size, which nothing downstream would question.
    Fixture f;
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::ShortData);
    const bool ok = f.bf.writeToFile(f.path);
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::None);

    INFO("error: " << f.bf.lastError());
    CHECK_FALSE(ok);
    CHECK_THAT(f.bf.lastError(), ContainsSubstring("short write"));
}

TEST_CASE("A bootfiles entry the archive rejects outright is reported",
          "[rpiboot][bootfiles][fault]")
{
    Fixture f;
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::DataError);
    const bool ok = f.bf.writeToFile(f.path);
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::None);

    INFO("error: " << f.bf.lastError());
    CHECK_FALSE(ok);
    CHECK_THAT(f.bf.lastError(), ContainsSubstring("data"));
}

TEST_CASE("A bootfiles archive that will not close is not called written",
          "[rpiboot][bootfiles][fault]")
{
    // libarchive flushes the last block on close, so this is where a volume
    // that filled up on the final write reports it.
    Fixture f;
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::Close);
    const bool ok = f.bf.writeToFile(f.path);
    rpi_test::failArchiveWrite(rpi_test::ArchiveFault::None);

    INFO("error: " << f.bf.lastError());
    CHECK_FALSE(ok);
    CHECK_THAT(f.bf.lastError(), ContainsSubstring("close"));
}
