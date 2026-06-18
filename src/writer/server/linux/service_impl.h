// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Linux privileged-helper service entry point. Qt-free (§8a). Mirrors the
// Windows helper: Unix-domain socket control plane + memfd bulk plane.

#pragma once

namespace rpi_imager::writer {

int RpiImagerWriterServiceMainLinux(int argc, char** argv);

} // namespace rpi_imager::writer
