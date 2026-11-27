// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// rpi-imager-writer.exe: the privileged Windows helper.
//
// Launched elevated, on demand, by the unprivileged client via ShellExecuteEx
// with the "runas" verb (doc/privileged-helper-plan.md §14.2 / §14.8). It is
// passed the control-pipe name on the command line, creates the pipe server,
// services the client, and exits when the pipe disconnects. Qt-free (§8a).
//
// Windows-only translation unit: CMake compiles it only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set, so it carries no platform #ifdef.

#include "server/win/service_impl.h"

int main(int argc, char** argv) {
    return rpi_imager::writer::RpiImagerWriterServiceMainWin(argc, argv);
}
