// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Process-wide accessor for the IPrivilegedWriter used by the unprivileged
// client. Lazy-initialised on first call; thread-safe via Meyers singleton
// semantics. Constructed via PrivilegedWriterFactory: on macOS this is the
// helper-routed MacOSXpcBackend by default, with the LocalShimBackend
// supplied as the opt-out / non-macOS fallback (see the implementation and
// doc/privileged-helper-plan.md §4).
//
// This accessor centralises backend construction so call sites don't each
// build their own. It can be retired in favour of explicit dependency
// injection if the wiring is ever threaded through ImageWriter and
// DownloadThread directly.

#pragma once

#include "privileged_io/privileged_writer.h"

namespace rpi_imager {

// Returns the process's IPrivilegedWriter. Constructed on first call.
// Backend selection is via PrivilegedWriterFactory; see implementation
// for the configuration used.
rpi_imager::privileged::IPrivilegedWriter& getProcessPrivilegedWriter();

// True when the native privileged helper should be used (default on Win/Linux
// when built). Opt out with RPI_IMAGER_USE_LEGACY_INPROCESS=1 or
// RPI_IMAGER_USE_WINDOWS_HELPER=0 / RPI_IMAGER_USE_LINUX_HELPER=0.
// platform_opt_out_env may be null on platforms without a per-platform override.
bool preferNativePrivilegedHelper(const char* platform_opt_out_env = nullptr);

} // namespace rpi_imager
