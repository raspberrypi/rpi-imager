// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// §14.4 peer authentication for the Windows privileged helper.
//
// Before honoring pipe RPCs, the helper verifies that the connecting process is
// the shipping rpi-imager client (see rpi_imager_identity.h): Authenticode-signed
// by the publisher org and (when configured) leaf cert thumbprint in
// cmake/rpi_imager_identity.cmake,
// named rpi-imager.exe, and installed beside rpi-imager-writer.exe.

#pragma once

#include <windows.h>

namespace rpi_imager::writer {

// Returns true when `client_pid` is an acceptable privileged-I/O client.
bool verifyConnectingClient(DWORD client_pid);

} // namespace rpi_imager::writer
