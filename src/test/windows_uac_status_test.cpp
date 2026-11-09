// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Asking after the Windows helper must not start it. The app asks at start-up
// and after every error, and starting the helper means a UAC prompt, so a
// status probe that launched it asked for elevation before the user had
// chosen to write anything.
//
// The stand-in helper has an extension nothing is registered for. Were the
// probe to launch it, ShellExecuteEx would fail rather than raise a prompt,
// and the status would read "not installed" instead of "ready".

#include <catch2/catch_test_macros.hpp>

#include "backends/windows_uac.h"

#include <filesystem>
#include <fstream>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;
namespace proto = rpi_imager::privileged::proto;
using rpi_imager::privileged::backends::WindowsUacBackend;

namespace {

struct StandInHelper {
    fs::path path = fs::temp_directory_path()
                    / ("rpi-imager-status-test-" + std::to_string(GetCurrentProcessId())
                       + ".rpi-imager-no-handler");
    StandInHelper() { std::ofstream(path) << "not a program"; }
    ~StandInHelper() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

WindowsUacBackend backendFor(const fs::path& helper) {
    WindowsUacBackend::Options opts;
    opts.helper_exe_path = helper.string();
    return WindowsUacBackend(opts);
}

} // namespace

TEST_CASE("A helper that is present reads as ready without being started", "[windows_uac]") {
    StandInHelper helper;
    WindowsUacBackend backend = backendFor(helper.path);

    const auto status = backend.queryHelperStatus();
    REQUIRE(status.ok);
    CHECK(status.value.state() == proto::HELPER_STATE_INSTALLED_READY);
    CHECK_FALSE(backend.helperActive());
}

TEST_CASE("A helper that is missing reads as not installed", "[windows_uac]") {
    WindowsUacBackend backend = backendFor(fs::temp_directory_path() / "rpi-imager-no-such-helper.exe");

    const auto status = backend.queryHelperStatus();
    REQUIRE(status.ok);
    CHECK(status.value.state() == proto::HELPER_STATE_NOT_INSTALLED);
    CHECK_FALSE(backend.helperActive());
}
