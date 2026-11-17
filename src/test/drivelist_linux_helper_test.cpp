/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The privileged helper's reading of an `lsblk -P` line: which sector sizes
 * a drive reports. A 4K-native drive written as if it had 512-byte sectors
 * has its O_DIRECT writes refused.
 */

#include <catch2/catch_test_macros.hpp>

#include "writer/server/linux/drivelist_linux_helper.h"

#include <string>

using rpi_imager::writer::linux_drivelist::detail::parseLsblkLine;

namespace {

std::string usbDisk(const std::string& sectorColumns)
{
    return "NAME=\"sda\" PATH=\"/dev/sda\" TYPE=\"disk\" SIZE=\"64000000000\" "
           "RM=\"1\" RO=\"0\" MODEL=\"Flash\" VENDOR=\"Acme\" TRAN=\"usb\" "
           "MOUNTPOINT=\"\" FSTYPE=\"\" SUBSYSTEMS=\"block:scsi:usb:pci\" "
           + sectorColumns;
}

}  // namespace

TEST_CASE("A 4K-native drive is reported as one", "[drivelist][helper]")
{
    const auto d = parseLsblkLine(usbDisk("LOG-SEC=\"4096\" PHY-SEC=\"4096\""));
    REQUIRE(d);
    CHECK(d->logicalBlockSize == 4096);
    CHECK(d->blockSize == 4096);
}

TEST_CASE("Newer lsblk's spelling of the sector columns is read too",
          "[drivelist][helper]")
{
    // util-linux 2.38 and later, which the embedded image ships.
    const auto d = parseLsblkLine(usbDisk("LOG_SEC=\"4096\" PHY_SEC=\"4096\""));
    REQUIRE(d);
    CHECK(d->logicalBlockSize == 4096);
    CHECK(d->blockSize == 4096);
}

TEST_CASE("A 512e drive keeps its two sizes apart", "[drivelist][helper]")
{
    const auto d = parseLsblkLine(usbDisk("LOG-SEC=\"512\" PHY-SEC=\"4096\""));
    REQUIRE(d);
    CHECK(d->logicalBlockSize == 512);
    CHECK(d->blockSize == 4096);
}

TEST_CASE("A line with no sector sizes falls back to 512", "[drivelist][helper]")
{
    const auto d = parseLsblkLine(usbDisk(""));
    REQUIRE(d);
    CHECK(d->logicalBlockSize == 512);
    CHECK(d->blockSize == 512);
}
