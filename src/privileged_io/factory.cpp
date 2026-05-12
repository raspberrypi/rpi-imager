// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Backend selection logic for IPrivilegedWriter.
//
// Phase 1a: only InProcessTestBackend exists in production code, plus the
// caller-supplied testing_override. Real platform backends slot in here
// in subsequent phases.
//
// The factory is unit-tested against synthetic platform/OS-version inputs;
// see test/privileged_writer_factory_test.cpp.

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
    // Phase 1b: when the helper is preferred and we're on macOS, route
    // through MacOSXpcBackend. The handshake at first use surfaces a
    // helper-not-installed error if the daemon isn't reachable; the
    // unprivileged client decides what to do with that (in phase 1a
    // production wiring, falls back to the local shim).
    if (config.prefer_helper) {
        backends::MacOSXpcBackend::Options xpc_opts;
        return std::make_unique<backends::MacOSXpcBackend>(std::move(xpc_opts));
    }
#endif

    // Phase 1a: client-supplied "do it ourselves" backend wraps the
    // existing in-process code paths (PlatformQuirks etc.). When it's
    // wired in, that's what production gets. The factory itself stays
    // Qt-free - the client owns the Qt-using shim implementation.
    if (config.local_backend_constructor) {
        if (auto w = config.local_backend_constructor(); w) {
            return w;
        }
        // Constructor returned null - fall through to the default below.
        // Real backend selection in subsequent phases inserts here:
        //   macOS 13+: MacOSXpcBackend (selected above when prefer_helper)
        //   macOS 12 : MacOSAuthopenLegacyBackend
        //   Linux    : LinuxPolkitBackend (or LinuxEmbeddedBackend in embedded)
        //   Windows  : WindowsUacBackend
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
