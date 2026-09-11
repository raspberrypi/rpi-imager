/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * One row of the drive picker.
 *
 * DriveListItem is what the chooser shows for each attached device, and
 * sizeInGb() is the number printed beside the name. It is the figure the user
 * checks a card against before agreeing to erase it, so it has to be the one
 * printed on the card -- which is decimal gigabytes, not binary.
 */

#include <catch2/catch_test_macros.hpp>

#include "drivelistitem.h"

#include <QString>

TEST_CASE("A drive's size is reported the way the card is labelled", "[drivelist]")
{
    // Cards and disks are sold in decimal gigabytes, and that is the number
    // on the label the user is holding. Dividing by 2^30 instead would show a
    // 64GB card as 59, and someone comparing the two would conclude they had
    // selected the wrong device.
    DriveListItem item(QStringLiteral("/dev/sdz"), QStringLiteral("Vendor Card"),
                       64000000000ULL);
    CHECK(item.sizeInGb() == 64);
}

TEST_CASE("A drive smaller than a gigabyte does not round up to one", "[drivelist]")
{
    // Truncation rather than rounding, so nothing is ever shown as larger
    // than it is: a device that cannot hold the image must not read as though
    // it might.
    DriveListItem tiny(QStringLiteral("/dev/sdz"), QStringLiteral("Tiny"), 999999999ULL);
    CHECK(tiny.sizeInGb() == 0);

    DriveListItem justOver(QStringLiteral("/dev/sdz"), QStringLiteral("Just over"),
                           1999999999ULL);
    CHECK(justOver.sizeInGb() == 1);
}
