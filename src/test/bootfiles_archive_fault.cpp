/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * See bootfiles_archive_fault.h.
 *
 * Bootfiles::writeToFile() checks every libarchive call and gives up on the
 * first that refuses, but nothing could provoke one: the archive is small,
 * the destination is a temporary directory, and libarchive does not fail on
 * either. So the refusals were unreachable -- and a bootfiles.bin written
 * short is a board that will not come up over rpiboot, reported as written.
 *
 * A full volume is the real cause and there is no way to arrange one here
 * without a disk to fill, so the call is answered instead.
 */

#include "bootfiles_archive_fault.h"

#include <archive.h>

#include <atomic>

namespace {
std::atomic<rpi_test::ArchiveFault> g_fault{rpi_test::ArchiveFault::None};
}  // namespace

namespace rpi_test {

void failArchiveWrite(ArchiveFault fault) { g_fault.store(fault); }

}  // namespace rpi_test

extern "C" {

int __real_archive_write_header(struct archive *a, struct archive_entry *entry);
la_ssize_t __real_archive_write_data(struct archive *a, const void *buff, size_t size);
int __real_archive_write_close(struct archive *a);

int __wrap_archive_write_header(struct archive *a, struct archive_entry *entry)
{
    if (g_fault.load() == rpi_test::ArchiveFault::Header)
        return ARCHIVE_FATAL;
    return __real_archive_write_header(a, entry);
}

la_ssize_t __wrap_archive_write_data(struct archive *a, const void *buff, size_t size)
{
    const rpi_test::ArchiveFault fault = g_fault.load();
    if (fault == rpi_test::ArchiveFault::DataError)
        return -1;
    if (fault == rpi_test::ArchiveFault::ShortData) {
        // Written, but not all of it: what a device that filled up mid-entry
        // reports, and the case the caller has to tell from success.
        const la_ssize_t real = __real_archive_write_data(a, buff, size);
        return real > 0 ? real - 1 : real;
    }
    return __real_archive_write_data(a, buff, size);
}

int __wrap_archive_write_close(struct archive *a)
{
    if (g_fault.load() == rpi_test::ArchiveFault::Close) {
        // Closed for real first, so the handle is not leaked by a case that
        // only wanted the return value.
        (void)__real_archive_write_close(a);
        return ARCHIVE_FATAL;
    }
    return __real_archive_write_close(a);
}

}  // extern "C"
