/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Which board a revision code names.
 *
 * The embedded build filters the OS list by this, so a board is offered only
 * images that will boot on it. Name the wrong board and somebody is offered
 * an image theirs cannot run; name none and the list is empty.
 *
 * device_info_test.cpp drives the same decision through a real
 * /proc/cpuinfo, which needs a mount namespace and so only runs on Linux.
 * These cases are the table itself, and run anywhere.
 */

#include <catch2/catch_test_macros.hpp>

#include "embedded/revision_code.h"

using rpi_imager::deviceTypeOf;
using rpi_imager::hardwareNameForDeviceType;
using rpi_imager::hardwareNameForRevision;

TEST_CASE("The device type is the eight bits above the revision", "[revision]")
{
    // A real 4B code, as /proc/cpuinfo carries it: 1 GB, Sony UK, BCM2711,
    // device type 0x11.
    CHECK(deviceTypeOf(0xa03111u) == 0x11u);
    // A 5B, and a Zero 2 W.
    CHECK(deviceTypeOf(0xc04170u) == 0x17u);
    CHECK(deviceTypeOf(0x902120u) == 0x12u);
    // The revision and the memory size either side of the field are ignored.
    CHECK(deviceTypeOf(0x00000000u) == 0x00u);
    CHECK(deviceTypeOf(0xffff0fffu) == 0xffu);
}

TEST_CASE("Every board in the table is named", "[revision]")
{
    struct Board {
        uint32_t type;
        const char *name;
    };
    static const Board kKnown[] = {
        {0x00, "Raspberry Pi 1"},        {0x01, "Raspberry Pi 1"},
        {0x02, "Raspberry Pi 1"},        {0x03, "Raspberry Pi 1"},
        {0x04, "Raspberry Pi 2"},        {0x06, "Raspberry Pi 1"},
        {0x08, "Raspberry Pi 3"},        {0x09, "Raspberry Pi Zero"},
        {0x0a, "Raspberry Pi 3"},        {0x0c, "Raspberry Pi Zero"},
        {0x0d, "Raspberry Pi 3"},        {0x0e, "Raspberry Pi 3"},
        {0x10, "Raspberry Pi 3"},        {0x11, "Raspberry Pi 4"},
        {0x12, "Raspberry Pi Zero 2 W"}, {0x13, "Raspberry Pi 4"},
        {0x14, "Raspberry Pi 4"},        {0x15, "Raspberry Pi 4"},
        {0x17, "Raspberry Pi 5"},        {0x18, "Raspberry Pi 5"},
        {0x19, "Raspberry Pi 5"},        {0x1a, "Raspberry Pi 5"},
    };

    for (const Board &board : kKnown) {
        INFO("device type " << board.type);
        CHECK(hardwareNameForDeviceType(board.type) ==
              QString::fromLatin1(board.name));
    }
}

TEST_CASE("A device type in a gap in the table is not named", "[revision]")
{
    // 0x05, 0x07, 0x0b, 0x0f and 0x16 are not assigned. Reading the table
    // with at() threw std::out_of_range on each of them, during startup,
    // with nothing to catch it.
    for (uint32_t type : {0x05u, 0x07u, 0x0bu, 0x0fu, 0x16u}) {
        INFO("device type " << type);
        CHECK(hardwareNameForDeviceType(type).isEmpty());
    }
}

TEST_CASE("A board newer than the table is not named", "[revision]")
{
    // The case that matters most: the table stops at the newest board known
    // when it was written, and there is always a next one.
    CHECK(hardwareNameForDeviceType(0x1bu).isEmpty());
    CHECK(hardwareNameForDeviceType(0x20u).isEmpty());
    CHECK(hardwareNameForDeviceType(0xffu).isEmpty());
}

TEST_CASE("A revision code names its board", "[revision]")
{
    bool parsed = false;
    CHECK(hardwareNameForRevision(QStringLiteral("a03111"), &parsed) ==
          QStringLiteral("Raspberry Pi 4"));
    CHECK(parsed);

    // Upper case, as some kernels print it.
    CHECK(hardwareNameForRevision(QStringLiteral("C04170")) ==
          QStringLiteral("Raspberry Pi 5"));
}

TEST_CASE("A revision field that is not a number is reported as unread",
          "[revision]")
{
    // Separate from an unknown board: both give no name, and only this one
    // means the line no longer has the shape it used to.
    bool parsed = true;
    CHECK(hardwareNameForRevision(QStringLiteral("not a number"), &parsed).isEmpty());
    CHECK_FALSE(parsed);

    parsed = true;
    CHECK(hardwareNameForRevision(QString(), &parsed).isEmpty());
    CHECK_FALSE(parsed);

    // Longer than 32 bits, which toUInt refuses rather than truncating.
    parsed = true;
    CHECK(hardwareNameForRevision(QStringLiteral("a031110000"), &parsed).isEmpty());
    CHECK_FALSE(parsed);
}

TEST_CASE("An unknown board is told apart from an unreadable line",
          "[revision]")
{
    // A well-formed code for a board the table does not have. The field was
    // read; there is simply nothing to call it.
    bool parsed = false;
    CHECK(hardwareNameForRevision(QStringLiteral("d04200"), &parsed).isEmpty());
    CHECK(parsed);
}

TEST_CASE("The caller may ignore whether the field was read", "[revision]")
{
    // The out parameter is optional, and passing nothing must not be a way
    // to write through a null pointer.
    CHECK(hardwareNameForRevision(QStringLiteral("a03111")) ==
          QStringLiteral("Raspberry Pi 4"));
    CHECK(hardwareNameForRevision(QStringLiteral("nonsense")).isEmpty());
}
