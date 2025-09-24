#ifndef EMBEDDED_CONFIG_H
#define EMBEDDED_CONFIG_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

/**
 * @brief Check if this build is configured for embedded mode
 * @return true if built with BUILD_EMBEDDED=ON, false otherwise
 */
constexpr bool isEmbeddedMode() {
#ifdef BUILD_EMBEDDED
    return true;
#else
    return false;
#endif
}

#endif // EMBEDDED_CONFIG_H
