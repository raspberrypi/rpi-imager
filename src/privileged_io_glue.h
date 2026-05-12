// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Process-wide accessor for the IPrivilegedWriter used by the unprivileged
// client. Lazy-initialised on first call; thread-safe via Meyers singleton
// semantics. Constructed via PrivilegedWriterFactory using
// makeLocalShimConstructor() (phase 1a) so production calls flow through
// the LocalShimBackend by default.
//
// This accessor exists to keep the phase 1a proof-of-concept call-site
// migrations small. Once the PAL is fully wired through ImageWriter and
// DownloadThread (later in phase 1a), the singleton can be retired in
// favour of explicit dependency injection.

#pragma once

#include "privileged_io/privileged_writer.h"

namespace rpi_imager {

// Returns the process's IPrivilegedWriter. Constructed on first call.
// Backend selection is via PrivilegedWriterFactory; see implementation
// for the configuration used.
rpi_imager::privileged::IPrivilegedWriter& getProcessPrivilegedWriter();

} // namespace rpi_imager
