// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Windows privileged-helper service entry point. Qt-free (§8a). Mirrors the
// role of service_impl.mm on macOS: it owns the privileged side of the
// client<->helper protocol, here over a named pipe (§14.3) instead of NSXPC.
//
// SCAFFOLD: brings up the pipe server + framed dispatch loop. The privileged
// operations (device open/write/verify, drive enumeration, unmount/eject) are
// stubbed to ERROR_NOT_IMPLEMENTED pending the device-I/O wiring (§14.5) and
// the bulk shared-memory plane (§14.3).
//
// Windows-only translation unit: CMake compiles it only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set, so it carries no platform #ifdef.

#pragma once

namespace rpi_imager::writer {

// Parses argv (expects "--pipe <name>"), creates the named-pipe server,
// authenticates and services one client for the process lifetime, and exits
// when the control pipe disconnects (§14.8 disconnect watchdog). Returns a
// process exit code.
int RpiImagerWriterServiceMainWin(int argc, char** argv);

} // namespace rpi_imager::writer
