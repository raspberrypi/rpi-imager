// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Backend selection logic for IPrivilegedWriter.
//
// See doc/privileged-helper-plan.md §4 for the backend matrix and the
// selection order this implements.
//
// The factory's fallback selection is exercised in
// test/privileged_writer_inprocess_test.cpp.

#include "privileged_writer.h"
#include "backends/in_process_test.h"

#ifdef __APPLE__
#include "backends/macos_xpc.h"
#endif

#include <utility>

namespace rpi_imager::privileged {

std::unique_ptr<IPrivilegedWriter>
PrivilegedWriterFactory::create(Config config) {
    // Test override takes precedence over everything.
    if (config.testing_override) {
        return std::move(config.testing_override);
    }

#ifdef __APPLE__
    // On macOS, the helper-routed backend is the default. The handshake at
    // first use surfaces a helper-not-installed error if the daemon isn't
    // reachable; the unprivileged client decides what to do with that.
    if (config.prefer_helper) {
        backends::MacOSXpcBackend::Options xpc_opts;
        return std::make_unique<backends::MacOSXpcBackend>(std::move(xpc_opts));
    }
#endif

    // Client-supplied "do it ourselves" backend wraps the existing
    // in-process code paths (PlatformQuirks etc.). This is the active
    // backend on platforms without a native privileged backend yet
    // (Linux, Windows) and the macOS opt-out path. The factory itself
    // stays Qt-free - the client owns the Qt-using shim implementation.
    if (config.local_backend_constructor) {
        if (auto w = config.local_backend_constructor(); w) {
            return w;
        }
        // Constructor returned null - fall through to the default below.
    }

    // Default fallback: in-process backend backed by a tempfile. Useful
    // for unit tests; not appropriate for production. A production binary
    // that ends up here means the client failed to register its local
    // backend constructor - either a build mistake or a deliberate test
    // configuration.
    backends::InProcessTestBackend::Options defaults;
    return std::make_unique<backends::InProcessTestBackend>(std::move(defaults));
}

} // namespace rpi_imager::privileged
