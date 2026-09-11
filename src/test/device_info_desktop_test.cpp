/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * DeviceInfo on a desktop.
 *
 * The embedded (kiosk) build reads the board's revision code out of
 * /proc/cpuinfo and tells the rest of the application what hardware it is
 * running on. The desktop build has no such notion -- it may be running on a
 * Pi, but it is not *the* Pi being imaged -- so it links a set of stubs that
 * answer "no hardware" to everything.
 */

#include <catch2/catch_test_macros.hpp>

#include "device_info.h"

#include <QJsonArray>
#include <QString>

TEST_CASE("The desktop build claims no Raspberry Pi hardware of its own",
          "[deviceinfo-desktop]")
{
    DeviceInfo info;

    // Not a Pi, whatever the machine underneath actually is. Answering
    // otherwise would have the desktop build describe the host it runs on as
    // though it were the board being written.
    CHECK_FALSE(info.isRaspberryPi());
    CHECK(info.revision().isEmpty());
    CHECK(info.hardwareName().isEmpty());
}

TEST_CASE("Hardware tags on a desktop stay empty and stay unset",
          "[deviceinfo-desktop]")
{
    DeviceInfo info;

    CHECK_FALSE(info.hardwareTagsSet());
    CHECK(info.getHardwareTags().isEmpty());

    // Setting them is accepted and changes nothing: the caller is shared with
    // the embedded build and does not branch on which it is talking to.
    QJsonArray tags;
    tags.append(QStringLiteral("pi5"));
    CHECK_NOTHROW(info.setHardwareTags(tags));
    CHECK_FALSE(info.hardwareTagsSet());
    CHECK(info.getHardwareTags().isEmpty());

    // And re-running detection is a no-op rather than a crash.
    CHECK_NOTHROW(info.determineHardware());
    CHECK_FALSE(info.isRaspberryPi());
}
