/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef PLATFORMQUIRKS_H
#define PLATFORMQUIRKS_H

namespace PlatformQuirks {
    /**
     * Apply platform-specific quirks and workarounds.
     * This function should be called early in main() before Qt initialization.
     * 
     * Currently handles:
     * - Windows: NVIDIA graphics card detection and QSG_RHI_PREFER_SOFTWARE_RENDERER workaround
     */
    void applyQuirks();

    /**
     * Play a system beep sound.
     * Platform-specific implementation to replace QApplication::beep().
     */
    void beep();
}

#endif // PLATFORMQUIRKS_H