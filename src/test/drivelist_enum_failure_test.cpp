/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What the drive list says when Windows will not enumerate drives at all.
 *
 * Its own binary because the failure is forced by a linker flag that applies
 * to everything linked with it -- see drivelist_enum_failure_wrap.cpp.
 */

#include <catch2/catch_test_macros.hpp>

#include "drivelist/drivelist.h"

#include <string>

TEST_CASE("An enumeration that fails is reported, not shown as no drives",
          "[drivelist][negative]")
{
    // An empty list is what a machine with nothing plugged in returns, so
    // returning one here would tell the user to plug something in when the
    // truth is that the enumeration itself failed. The sentinel is how the
    // UI tells those apart.
    const auto devices = Drivelist::ListStorageDevices();

    REQUIRE(devices.size() == 1);
    CHECK(devices[0].device == "__error__");
    CHECK_FALSE(devices[0].error.empty());
    CHECK_FALSE(devices[0].description.empty());

    // The Windows error is carried out by number rather than collapsed, so a
    // bug report can say which failure it was. ERROR_NOT_ENOUGH_MEMORY is 8.
    INFO("error text: " << devices[0].error);
    CHECK(devices[0].error.find("8") != std::string::npos);
}
