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

    /**
     * Check if the system has network connectivity.
     * Uses platform-specific APIs to determine if network access is available.
     * 
     * @return true if network connectivity is available, false otherwise
     */
    bool hasNetworkConnectivity();
    
    /**
     * Check if system is ready for network operations.
     * On embedded Linux systems, this may include waiting for time synchronization.
     * On other platforms, this is equivalent to hasNetworkConnectivity().
     * 
     * This is a synchronous check - for async waiting, the application should
     * poll this method or use platform-specific mechanisms.
     * 
     * @return true if network is ready for use (including time sync if needed)
     */
    bool isNetworkReady();

    /**
     * Bring the application window to the foreground.
     * On Windows, this will restore a minimized window and attempt to bring it to the front.
     * On other platforms, this is a no-op.
     * 
     * @param windowHandle Native window handle (HWND on Windows, cast from QWindow::winId())
     */
    void bringWindowToForeground(void* windowHandle);
}

#endif // PLATFORMQUIRKS_H