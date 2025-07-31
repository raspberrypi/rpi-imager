/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>

namespace PlatformQuirks {

void applyQuirks() {
    // Currently no platform-specific quirks needed for macOS
    // This is a placeholder for future macOS-specific workarounds
    
    // Example of how to set environment variables without Qt:
    // setenv("VARIABLE_NAME", "value", 1);
}

} // namespace PlatformQuirks