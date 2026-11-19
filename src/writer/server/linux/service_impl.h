// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Linux privileged-helper service entry point. Qt-free (§8a). Mirrors the
// Windows helper: Unix-domain socket control plane + memfd bulk plane.

#pragma once

namespace rpi_imager::writer {

int RpiImagerWriterServiceMainLinux(int argc, char** argv);

// Serves requests on a connection whose client is already authenticated,
// until it closes. The tests drive the helper through one end of a socketpair.
void ServeClientLinux(int client_fd);

} // namespace rpi_imager::writer
