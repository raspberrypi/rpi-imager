/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What the formatter does when it cannot get a buffer.
 *
 * Its own binary: the failure is forced by a linker flag that applies to
 * everything linked with it. See aligned_alloc_fault.h.
 */

#include <catch2/catch_test_macros.hpp>

#include "aligned_alloc_fault.h"
#include "disk_formatter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <string>

namespace {

// 64 MB: comfortably past FAT32's minimum, so nothing is refused for size.
constexpr std::uint64_t kImageBytes = 64ull * 1024 * 1024;

std::string scratchImage(QTemporaryDir &dir)
{
    return QDir(dir.path()).filePath(QStringLiteral("format.img")).toStdString();
}

// A format here can also fail for reasons that have nothing to do with the
// injector -- the disk, the path, a scanner holding the file open. Without
// this the refusal comes back as a bare REQUIRE and says nothing about which.
const char *reason(rpi_imager::FormatError error)
{
    switch (error) {
    case rpi_imager::FormatError::kFileOpenError:    return "file open";
    case rpi_imager::FormatError::kFileWriteError:   return "file write";
    case rpi_imager::FormatError::kFileSeekError:    return "file seek";
    case rpi_imager::FormatError::kInvalidParameters:return "invalid parameters";
    case rpi_imager::FormatError::kInsufficientSpace:return "insufficient space";
    case rpi_imager::FormatError::kCancelled:        return "cancelled";
    }
    return "unknown";
}

}  // namespace

TEST_CASE("A format that cannot get a buffer is refused, not half written",
          "[format][alloc]")
{
    // Every buffer the formatter takes is checked, and each check refuses the
    // whole format. None had been reached: the allocations are small and
    // succeed on any machine that can run this.
    //
    // Walked rather than aimed at one: the format takes several buffers in
    // sequence -- the MBR, the boot sector, the FATs, the root directory --
    // and each refusal is a different line. Failing each in turn reaches them
    // without this case having to know which is which.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // How many it asks for when nothing fails, so the walk covers them all.
    rpi_test::failAlignedAllocAt(-1);
    {
        rpi_imager::DiskFormatter formatter;
        const auto clean = formatter.FormatFile(scratchImage(dir), kImageBytes);
        INFO("a format with nothing failing was refused: " << reason(clean.error()));
        REQUIRE(clean);
    }
    const int total = rpi_test::alignedAllocCallsMade();
    INFO("aligned allocations in a whole format: " << total);
    // Six at the time of writing -- the MBR, the boot sector, its backup, the
    // FATs and the root directory. Held above one so the walk below cannot
    // quietly shrink to a single refusal and still pass.
    REQUIRE(total >= 4);

    for (int i = 0; i < total; ++i) {
        const std::string path =
            QDir(dir.path()).filePath(QStringLiteral("fail-%1.img").arg(i)).toStdString();

        rpi_test::failAlignedAllocAt(i);
        rpi_imager::DiskFormatter formatter;
        const auto result = formatter.FormatFile(path, kImageBytes);
        const int reached = rpi_test::alignedAllocCallsMade();
        rpi_test::stopFailingAlignedAlloc();

        INFO("allocation " << i << " of " << total << " made to fail");
        // A format that asked for fewer buffers than the clean one never met
        // the armed allocation, so a success here would say nothing.
        INFO("allocations reached: " << reached);
        // Refused, every time. What must not happen is a format reporting
        // success having skipped a structure it could not build a buffer for.
        CHECK_FALSE(result);
    }
}

TEST_CASE("A format asks for the same buffers whether or not one fails",
          "[format][alloc]")
{
    // Guards the case above from passing vacuously. If arming the injector
    // stopped the formatter reaching its allocations at all, every refusal
    // would be the first one and the walk would prove nothing.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    rpi_test::failAlignedAllocAt(-1);
    {
        rpi_imager::DiskFormatter formatter;
        const auto clean = formatter.FormatFile(scratchImage(dir), kImageBytes);
        INFO("a format with nothing failing was refused: " << reason(clean.error()));
        REQUIRE(clean);
    }
    const int whole = rpi_test::alignedAllocCallsMade();

    // Fail one well past the end: nothing fails, so the count must match.
    rpi_test::failAlignedAllocAt(whole + 1000);
    {
        const std::string path =
            QDir(dir.path()).filePath(QStringLiteral("again.img")).toStdString();
        rpi_imager::DiskFormatter formatter;
        CHECK(formatter.FormatFile(path, kImageBytes));
    }
    CHECK(rpi_test::alignedAllocCallsMade() == whole);
    rpi_test::stopFailingAlignedAlloc();
}
