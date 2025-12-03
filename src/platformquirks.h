/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef PLATFORMQUIRKS_H
#define PLATFORMQUIRKS_H

#include <QString>
#include <QStringList>

namespace PlatformQuirks {
    /**
     * Apply platform-specific quirks and workarounds.
     * This function should be called early in main() before Qt initialization.
     * 
     * Currently handles:
     * - Windows: NVIDIA graphics card detection and QSG_RHI_PREFER_SOFTWARE_RENDERER workaround
     * - Linux: Sudo user detection and environment override for:
     *   - HOME and XDG directories (cache/config/data)
     *   - XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS for D-Bus session access
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

    /**
     * Check if the application is running with elevated privileges.
     * On Linux: Checks if running as root (UID 0)
     * On Windows: Checks if running as Administrator
     * On macOS: Always returns true (sensible permissions model operates as expected)
     * 
     * @return true if running with elevated privileges, false otherwise
     */
    bool hasElevatedPrivileges();

    /**
     * Attach to or allocate a console for output.
     * On Windows: Attempts to attach to parent console or allocates a new one,
     *             redirecting stdout/stderr to the console.
     * On Linux/macOS: No-op (console is already available).
     */
    void attachConsole();

    /**
     * Check if running from a self-contained application bundle that supports
     * privilege escalation (e.g., Linux AppImage).
     * On Linux: Returns true if APPIMAGE environment variable is set
     * On other platforms: Returns false
     * @return true if running from an elevatable bundle
     */
    bool isElevatableBundle();

    /**
     * Get the path to the application bundle file (e.g., AppImage path).
     * On Linux: Returns APPIMAGE environment variable value
     * On other platforms: Returns nullptr
     * @return Path to the bundle, or nullptr if not running from a bundle
     */
    const char* getBundlePath();

    /**
     * Check if the system is configured to allow automatic privilege elevation
     * for this application bundle.
     * On Linux: Checks if a polkit policy exists for the AppImage
     * On other platforms: Returns false
     * @return true if automatic elevation is configured
     */
    bool hasElevationPolicyInstalled();

    /**
     * Install system configuration to allow automatic privilege elevation
     * for this application bundle.
     * On Linux: Installs a polkit policy file for the AppImage
     * On other platforms: No-op, returns false
     * This function must be called with elevated privileges.
     * @return true if policy was installed successfully, false otherwise
     */
    bool installElevationPolicy();

    /**
     * Attempt to re-execute the current process with elevated privileges.
     * On Linux: Uses pkexec if a polkit policy is installed
     * On other platforms: No-op, returns false
     * This function only returns if elevation fails or is cancelled.
     * On successful elevation, the process is replaced.
     * 
     * @param argc Argument count from main()
     * @param argv Argument vector from main()
     * @return false if elevation was not attempted or failed
     */
    bool tryElevate(int argc, char** argv);

    /**
     * Launch a detached process that will outlive the parent.
     * On Linux: Uses double-fork pattern to fully detach the child
     * On macOS/Windows: Uses platform-appropriate detached process creation
     * 
     * This is the preferred way to launch external processes like browsers,
     * file managers, or other tools that should continue running independently.
     * 
     * @param program Path to the program to execute
     * @param arguments List of arguments to pass to the program
     * @return true if the process was successfully launched
     */
    bool launchDetached(const QString& program, const QStringList& arguments);

    /**
     * Run the policy installer with elevated privileges (interactive).
     * On Linux: Uses pkexec to run the bundle with --install-elevation-policy
     * On other platforms: No-op, returns false
     * 
     * This is a blocking call that shows an authentication dialog.
     * 
     * @return true if policy was installed successfully
     */
    bool runElevatedPolicyInstaller();

    /**
     * Replace current process with an elevated instance.
     * On Linux: exec() into pkexec with the bundle path
     * On other platforms: No-op
     * 
     * This function does not return on success (process is replaced).
     * 
     * @param extraArgs Additional arguments to pass to the elevated process
     */
    void execElevated(const QStringList& extraArgs);

    /**
     * Determine if scroll direction should be inverted for natural scrolling.
     * 
     * This method encapsulates platform-specific scroll direction detection:
     * - On Windows: Qt doesn't report the system scroll setting, so we read
     *   from the registry and ignore the qtInvertedFlag.
     * - On macOS/Linux: Qt correctly reports the inverted flag, so we pass
     *   through the qtInvertedFlag value.
     * 
     * @param qtInvertedFlag The inverted flag from Qt's WheelEvent
     * @return true if scroll direction should be inverted (natural scrolling)
     */
    bool isScrollInverted(bool qtInvertedFlag);
}

#endif // PLATFORMQUIRKS_H