// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Shared wire protocol version for client backends and helper servers.

#pragma once

#include <cstdint>

namespace rpi_imager::privileged::wire {

constexpr std::uint32_t kProtocolVersion = 2;

} // namespace rpi_imager::privileged::wire
