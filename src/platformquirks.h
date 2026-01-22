/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef PLATFORMQUIRKS_H
#define PLATFORMQUIRKS_H

#include <QString>
#include <QStringList>
#include <functional>

namespace PlatformQuirks {
    
    /** Callback type for network status changes. Parameter is true if network is available. */
    using NetworkStatusCallback = std::function<void(bool)>;

    /** Result codes for disk unmount/eject operations. */
    enum class DiskResult {
        Success,
        InvalidDrive,
        AccessDenied,
        Busy,
        Error
    };
    /**
     * Apply platform-specific quirks and workarounds.
     * This function should be called early in main() before Qt initialization.
     */
    void applyQuirks();

    /** Play a system beep sound. */
    void beep();

    /** Check if beep/audio notification is available on this system. */
    bool isBeepAvailable();

    /** Check if the system has network connectivity. */
    bool hasNetworkConnectivity();
    
    /** Check if system is ready for network operations (including time sync on embedded). */
    bool isNetworkReady();
    
    /** 
     * Start monitoring network status changes.
     * Callback will be invoked when network status changes (true = available, false = unavailable).
     * Only one callback can be registered at a time.
     */
    void startNetworkMonitoring(NetworkStatusCallback callback);
    
    /** Stop monitoring network status changes. */
    void stopNetworkMonitoring();

    /** Bring the application window to the foreground (Windows-specific). */
    void bringWindowToForeground(void* windowHandle);

    /** Check if the application is running with elevated privileges. */
    bool hasElevatedPrivileges();

    /** Attach to or allocate a console for output (Windows-specific). */
    void attachConsole();

    /** Check if running from a self-contained application bundle (e.g., AppImage). */
    bool isElevatableBundle();

    /** Get the path to the application bundle file. */
    const char* getBundlePath();

    /** Check if automatic privilege elevation is configured. */
    bool hasElevationPolicyInstalled();

    /** Install system configuration for automatic privilege elevation. */
    bool installElevationPolicy();

    /** Attempt to re-execute the current process with elevated privileges. */
    bool tryElevate(int argc, char** argv);

    /** Launch a detached process that will outlive the parent. */
    bool launchDetached(const QString& program, const QStringList& arguments);

    /** Run the policy installer with elevated privileges (interactive). */
    bool runElevatedPolicyInstaller();

    /** Replace current process with an elevated instance. */
    void execElevated(const QStringList& extraArgs);

    /** Determine if scroll direction should be inverted for natural scrolling. */
    bool isScrollInverted(bool qtInvertedFlag);

    /**
     * Get the optimal device path for write I/O operations.
     * On macOS, converts /dev/diskN to /dev/rdiskN for direct I/O (bypasses buffer cache).
     * On other platforms, returns the path unchanged.
     */
    QString getWriteDevicePath(const QString& devicePath);

    /**
     * Get the device path suitable for eject/unmount operations.
     * On macOS, converts /dev/rdiskN back to /dev/diskN.
     * On other platforms, returns the path unchanged.
     */
    QString getEjectDevicePath(const QString& devicePath);

    /**
     * Unmount all volumes associated with a disk device.
     * This prepares the disk for writing by unmounting all mounted partitions.
     *
     * @param device The device path (e.g., "/dev/sda", "\\.\PhysicalDrive1", "/dev/disk2")
     * @return DiskResult::Success on success, error code otherwise
     */
    DiskResult unmountDisk(const QString& device);

    /**
     * Eject a disk device after writing is complete.
     * On removable media, this makes it safe to physically remove.
     * On card readers, this ejects the card but not the reader itself.
     *
     * @param device The device path (e.g., "/dev/sda", "\\.\PhysicalDrive1", "/dev/disk2")
     * @return DiskResult::Success on success, error code otherwise
     */
    DiskResult ejectDisk(const QString& device);

#ifdef Q_OS_LINUX
    /**
     * Find the system's CA certificate bundle for libcurl.
     *
     * AppImages bundle libcurl with a hardcoded CA certificate path from the
     * build system. When run on a different distribution, this path may not
     * exist, causing SSL/TLS connections to fail.
     *
     * @return Path to CA certificate bundle, or nullptr if not found.
     */
    const char* findCACertBundle();

    /**
     * Clear AppImage-specific environment variables before exec'ing external tools.
     *
     * AppImages set LD_LIBRARY_PATH and LD_PRELOAD to use bundled libraries.
     * External tools (runuser, kde-inhibit, systemd-inhibit, etc.) need to use
     * system libraries instead, otherwise they may fail due to symbol conflicts
     * (e.g., PAM modules failing with "cannot open session: Module is unknown").
     *
     * Call this in forked child processes before execvp().
     */
    void clearAppImageEnvironment();
#endif
}

#endif // PLATFORMQUIRKS_H
