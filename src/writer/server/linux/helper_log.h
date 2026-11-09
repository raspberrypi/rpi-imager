// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free helper audit log (§7). Linux analogue of macOS helper.log.

#pragma once

namespace rpi_imager::writer {

void helperLog(const char* msg);

} // namespace rpi_imager::writer
