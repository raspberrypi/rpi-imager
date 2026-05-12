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
        config.allow_user_prompt = true;
        config.local_backend_constructor = makeLocalShimConstructor();
        return rpi_imager::privileged::PrivilegedWriterFactory::create(
            std::move(config));
    }();
    return *writer;
}

} // namespace rpi_imager
