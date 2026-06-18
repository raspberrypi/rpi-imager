// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#pragma once

#include <string>

namespace rpi_imager::linux_proc {

std::string environValue(pid_t pid, const char* key);

} // namespace rpi_imager::linux_proc
