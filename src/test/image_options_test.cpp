/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The advanced-option flags.
 *
 * They are combined with | and read back with testFlag, which only works
 * while each one owns a bit nothing else uses. UserDefinedFirstRun was 0x3 --
 * IsRpiosCloudInit and EnableSsh together -- so asking for it asked for both
 * of those as well, and the command line asks for it on every run given a
 * first-run script. No caller reads those two yet, which is the only reason
 * it never showed.
 */

#include <catch2/catch_test_macros.hpp>

#include "imageadvancedoptions.h"

#include <QMetaEnum>

#include <vector>

using ImageOptions::AdvancedOption;
using ImageOptions::AdvancedOptions;

namespace {

const std::vector<AdvancedOption> kFlags = {
    ImageOptions::IsRpiosCloudInit,
    ImageOptions::EnableSsh,
    ImageOptions::EnableSecureBoot,
    ImageOptions::UserDefinedFirstRun,
};

bool isSingleBit(int value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

TEST_CASE("Every advanced option is one bit of its own", "[imageoptions]")
{
    for (AdvancedOption flag : kFlags) {
        INFO("flag value " << int(flag));
        CHECK(isSingleBit(int(flag)));
    }
}

TEST_CASE("No two advanced options share a bit", "[imageoptions]")
{
    int seen = 0;
    for (AdvancedOption flag : kFlags) {
        INFO("flag value " << int(flag));
        CHECK((seen & int(flag)) == 0);
        seen |= int(flag);
    }
}

TEST_CASE("Asking for one advanced option does not ask for another",
          "[imageoptions]")
{
    // The property the command line depends on: it sets UserDefinedFirstRun
    // whenever it is handed a first-run script, and that must not turn on
    // SSH or declare the image a cloud-init one.
    for (AdvancedOption flag : kFlags) {
        const AdvancedOptions only(flag);
        for (AdvancedOption other : kFlags) {
            if (other == flag)
                continue;
            INFO("setting " << int(flag) << " must not set " << int(other));
            CHECK_FALSE(only.testFlag(other));
        }
        CHECK(only.testFlag(flag));
    }
}

TEST_CASE("Nothing is set when nothing was asked for", "[imageoptions]")
{
    const AdvancedOptions none(ImageOptions::NoAdvancedOptions);
    for (AdvancedOption flag : kFlags) {
        INFO("flag value " << int(flag));
        CHECK_FALSE(none.testFlag(flag));
    }
}

TEST_CASE("Options combine and are read back one at a time", "[imageoptions]")
{
    // What cli.cpp builds: the first-run marker, plus whatever else was
    // asked for on the command line.
    const AdvancedOptions both =
        ImageOptions::UserDefinedFirstRun | ImageOptions::EnableSecureBoot;

    CHECK(both.testFlag(ImageOptions::UserDefinedFirstRun));
    CHECK(both.testFlag(ImageOptions::EnableSecureBoot));
    CHECK_FALSE(both.testFlag(ImageOptions::EnableSsh));
    CHECK_FALSE(both.testFlag(ImageOptions::IsRpiosCloudInit));
}

TEST_CASE("The options are registered with the meta-object system",
          "[imageoptions]")
{
    // Q_ENUM_NS is what lets a value be named in a log line or a property
    // rather than printed as a number.
    const QMetaEnum meta = QMetaEnum::fromType<AdvancedOption>();
    REQUIRE(meta.isValid());

    for (AdvancedOption flag : kFlags) {
        INFO("flag value " << int(flag));
        CHECK(meta.valueToKey(int(flag)) != nullptr);
    }
}
