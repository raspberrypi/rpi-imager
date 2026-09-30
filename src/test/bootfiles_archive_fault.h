/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Making libarchive refuse a write, for the refusals in Bootfiles that
 * depend on one.
 *
 * Only works in a target linked with
 *   -Wl,--wrap=archive_write_header
 *   -Wl,--wrap=archive_write_data
 *   -Wl,--wrap=archive_write_close
 * and with bootfiles_archive_fault.cpp. libarchive is linked statically, so
 * these are the plain names and the wrap targets are functions. The wrap
 * applies to everything linked into the target, so it wants a binary of its
 * own.
 */

#ifndef RPI_TEST_BOOTFILES_ARCHIVE_FAULT_H
#define RPI_TEST_BOOTFILES_ARCHIVE_FAULT_H

namespace rpi_test {

// Which call fails. Nothing fails until one is armed, and each is cleared by
// arming None.
enum class ArchiveFault {
    None,
    Header,     // archive_write_header returns ARCHIVE_FATAL
    ShortData,  // archive_write_data reports fewer bytes than it was given
    DataError,  // archive_write_data returns a negative count
    Close,      // archive_write_close returns ARCHIVE_FATAL
};

void failArchiveWrite(ArchiveFault fault);

}  // namespace rpi_test

#endif  // RPI_TEST_BOOTFILES_ARCHIVE_FAULT_H
