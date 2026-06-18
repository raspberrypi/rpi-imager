// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Peer authentication for the Linux helper. AppImage clients: verify embedded
// GPG signature on $APPIMAGE and pin signer key fingerprints. Native dev
// fallback: co-located rpi-imager beside the helper when no keys are pinned.

#pragma once

#include <sys/types.h>

namespace rpi_imager::writer {

bool verifyConnectingClient(pid_t client_pid);

} // namespace rpi_imager::writer
