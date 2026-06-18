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

// Native Windows backend is opt-in and gated at build time so default Windows
// builds/behavior are unchanged while it's brought up (§14.6, §14.11). The
// define is only ever set on Windows builds (CMake gates it under WIN32), so
// it doubles as the platform guard.
#if defined(RPI_IMAGER_ENABLE_WINDOWS_HELPER)
#include "backends/windows_uac.h"
#endif

#if defined(__linux__)
#include <unistd.h>
#include "backends/linux_embedded.h"
#if defined(RPI_IMAGER_ENABLE_LINUX_HELPER)
#include "backends/linux_polkit.h"
#endif
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

#if defined(RPI_IMAGER_ENABLE_WINDOWS_HELPER)
    // On Windows the native helper is selected only when explicitly preferred
    // (the glue sets prefer_helper from RPI_IMAGER_USE_WINDOWS_HELPER, default
    // off). Otherwise control falls through to the LocalShimBackend below, so
    // shipping this code does not change the default in-process path.
    if (config.prefer_helper) {
        backends::WindowsUacBackend::Options win_opts;
        if (!config.app_bundle_path.empty()) {
            win_opts.helper_exe_path = config.app_bundle_path;
        }
        return std::make_unique<backends::WindowsUacBackend>(std::move(win_opts));
    }
#endif

#if defined(__linux__)
    // Embedded / kiosk installs that already run as root use in-process I/O
    // with no helper process or polkit prompt.
    if (::geteuid() == 0) {
        return std::make_unique<backends::LinuxEmbeddedBackend>();
    }

#if defined(RPI_IMAGER_ENABLE_LINUX_HELPER)
    // pkexec-launched helper; opt-in at runtime via RPI_IMAGER_USE_LINUX_HELPER.
    if (config.prefer_helper) {
        backends::LinuxPolkitBackend::Options linux_opts;
        if (!config.app_bundle_path.empty()) {
            linux_opts.helper_exe_path = config.app_bundle_path;
        }
        return std::make_unique<backends::LinuxPolkitBackend>(std::move(linux_opts));
    }
#endif
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
