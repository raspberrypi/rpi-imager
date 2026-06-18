// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free unmount/eject for the Windows privileged helper.

#pragma once

#include <cstdint>
#include <string>

namespace rpi_imager::win_maint {

enum class Result {
    Success,
    InvalidDrive,
    AccessDenied,
    Busy,
    Error,
};

std::int32_t lastWin32Error();
const std::string& lastDetail();

Result unmountDisk(const std::string& device_path);
Result ejectDisk(const std::string& device_path);

} // namespace rpi_imager::win_maint
