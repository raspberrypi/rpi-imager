// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "privileged_io_glue.h"
#include "local_shim_backend.h"

#include <cstdlib>
#include <memory>

namespace rpi_imager {

rpi_imager::privileged::IPrivilegedWriter& getProcessPrivilegedWriter() {
    static std::unique_ptr<rpi_imager::privileged::IPrivilegedWriter> writer = [] {
        rpi_imager::privileged::PrivilegedWriterFactory::Config config;
#if defined(__APPLE__)
        // On macOS, the privileged helper backend is the default. Opt
        // out via RPI_IMAGER_USE_LEGACY_AUTHOPEN=1 (or the old
        // RPI_IMAGER_USE_XPC_HELPER=0) for diagnostic comparison or
        // for fallback to the local shim path on platforms / setups
        // where the helper can't be installed.
        const char* legacy = std::getenv("RPI_IMAGER_USE_LEGACY_AUTHOPEN");
        const char* use_xpc = std::getenv("RPI_IMAGER_USE_XPC_HELPER");
        const bool opt_out = (legacy && legacy[0] == '1') ||
                             (use_xpc && use_xpc[0] == '0');
        config.prefer_helper = !opt_out;
#elif defined(RPI_IMAGER_ENABLE_WINDOWS_HELPER)
        // On Windows the native helper (WindowsUacBackend) is opt-in while it
        // is brought up (§14.11 rollout gate): default off, enabled with
        // RPI_IMAGER_USE_WINDOWS_HELPER=1. When off, prefer_helper is false so
        // the factory selects the LocalShimBackend, i.e. the existing
        // in-process write path is unchanged.
        const char* use_win = std::getenv("RPI_IMAGER_USE_WINDOWS_HELPER");
        config.prefer_helper = (use_win && use_win[0] == '1');
#elif defined(RPI_IMAGER_ENABLE_LINUX_HELPER)
        // Linux polkit helper: opt-in via RPI_IMAGER_USE_LINUX_HELPER=1.
        // When running as root the factory selects LinuxEmbeddedBackend
        // regardless of this flag.
        const char* use_linux = std::getenv("RPI_IMAGER_USE_LINUX_HELPER");
        config.prefer_helper = (use_linux && use_linux[0] == '1');
#else
        // No native privileged backend on this platform; the LocalShimBackend
        // is the active path. prefer_helper has no native branch to select.
        config.prefer_helper = false;
#endif
        config.allow_user_prompt = true;
        config.local_backend_constructor = makeLocalShimConstructor();
        return rpi_imager::privileged::PrivilegedWriterFactory::create(
            std::move(config));
    }();
    return *writer;
}

} // namespace rpi_imager
