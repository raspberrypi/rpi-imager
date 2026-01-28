/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Linux platform-specific implementation.
 *
 * Design notes:
 * - Uses modern Linux APIs: poll() instead of select(), eventfd() for signaling
 * - All fds created with CLOEXEC to prevent leaks to child processes
 * - Thread-safe: uses reentrant functions (getpwuid_r) and proper synchronization
 * - Netlink for network monitoring (no polling, kernel pushes events)
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/eventfd.h>  // eventfd for modern thread signaling
#include <sys/utsname.h>   // uname() for kernel version logging
#include <poll.h>          // poll() instead of select()
#include <signal.h>        // Signal masking for worker thread
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <mntent.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <atomic>
#include <mutex>
#include <net/if_arp.h>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <vector>
#include <string>

namespace {
    // Network monitoring state
    int g_netlinkSocket = -1;
    pthread_t g_monitorThread;
    std::atomic<bool> g_monitorRunning{false};
    int g_stopEventFd = -1;  // eventfd for signaling thread to stop (more efficient than pipe)
    PlatformQuirks::NetworkStatusCallback g_networkCallback = nullptr;
    pthread_mutex_t g_callbackMutex = PTHREAD_MUTEX_INITIALIZER;
    
    // Cached network connectivity state (updated by netlink monitor)
    std::atomic<bool> g_cachedNetworkConnectivity{false};
    std::atomic<bool> g_networkConnectivityCacheValid{false};
    
    void* netlinkMonitorThread(void* arg) {
        (void)arg;
        
        // Block all signals in this thread - let the main thread handle them
        // This prevents EINTR interruptions and signal handler races
        sigset_t allSignals;
        sigfillset(&allSignals);
        pthread_sigmask(SIG_BLOCK, &allSignals, nullptr);
        
        char buf[4096];
        struct iovec iov = { buf, sizeof(buf) };
        struct sockaddr_nl sa;
        struct msghdr msg = { &sa, sizeof(sa), &iov, 1, nullptr, 0, 0 };
        
        // Use poll() instead of select() - no fd number limitations
        struct pollfd fds[2];
        fds[0].fd = g_netlinkSocket;
        fds[0].events = POLLIN;
        fds[1].fd = g_stopEventFd;
        fds[1].events = POLLIN;
        
        while (g_monitorRunning.load(std::memory_order_acquire)) {
            // Wait for events with 1 second timeout
            int ret = poll(fds, 2, 1000);
            
            if (ret < 0) {
                if (errno == EINTR) continue;
                int savedErrno = errno;
                fprintf(stderr, "netlinkMonitorThread: poll() failed: %s\n", strerror(savedErrno));
                break;
            }
            
            // Check if we should stop (eventfd signaled)
            if (fds[1].revents & POLLIN) {
                break;
            }
            
            if (ret == 0) continue;  // Timeout, check g_monitorRunning again
            
            // Check for netlink events
            if (fds[0].revents & POLLIN) {
                ssize_t len = recvmsg(g_netlinkSocket, &msg, MSG_DONTWAIT);
                if (len < 0) {
                    if (errno == EINTR || errno == EAGAIN) continue;
                    int savedErrno = errno;
                    fprintf(stderr, "netlinkMonitorThread: recvmsg() failed: %s\n", strerror(savedErrno));
                    break;
                }
                
                // Parse netlink messages
                for (struct nlmsghdr* nh = reinterpret_cast<struct nlmsghdr*>(buf);
                     NLMSG_OK(nh, static_cast<size_t>(len));
                     nh = NLMSG_NEXT(nh, len)) {
                    
                    if (nh->nlmsg_type == NLMSG_DONE) break;
                    if (nh->nlmsg_type == NLMSG_ERROR) continue;
                    
                    // We're interested in link up/down events
                    if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) {
                        // Validate payload size before accessing ifinfomsg
                        if (NLMSG_PAYLOAD(nh, 0) < sizeof(struct ifinfomsg)) {
                            continue;  // Malformed message, skip
                        }
                        
                        struct ifinfomsg* ifi = reinterpret_cast<struct ifinfomsg*>(NLMSG_DATA(nh));
                        
                        // Skip loopback interface
                        if (ifi->ifi_type == ARPHRD_LOOPBACK) continue;
                        
                        bool linkUp = (ifi->ifi_flags & IFF_UP) && (ifi->ifi_flags & IFF_RUNNING);
                        fprintf(stderr, "Network link change detected: interface %d, up=%d\n", 
                                ifi->ifi_index, linkUp);
                        
                        // Check overall connectivity and invoke callback under mutex
                        // Invalidate cache INSIDE the mutex to prevent race where another
                        // thread could re-populate cache with stale data between invalidation
                        // and our re-check
                        pthread_mutex_lock(&g_callbackMutex);
                        g_networkConnectivityCacheValid.store(false, std::memory_order_relaxed);
                        if (g_networkCallback) {
                            bool isAvailable = PlatformQuirks::hasNetworkConnectivity();
                            g_networkCallback(isAvailable);
                        }
                        pthread_mutex_unlock(&g_callbackMutex);
                    }
                }
            }
            
            // Handle error conditions on fds
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "netlinkMonitorThread: netlink socket error (revents=0x%x)\n", 
                        fds[0].revents);
                break;
            }
        }
        
        return nullptr;
    }
}

namespace PlatformQuirks {

void applyQuirks() {
    // Log system information for remote debugging
    // This helps diagnose distro-specific issues from user reports
    struct utsname sysInfo;
    if (uname(&sysInfo) == 0) {
        std::fprintf(stderr, "Platform: %s %s (%s)\n", 
                     sysInfo.sysname, sysInfo.release, sysInfo.machine);
    }
    
    // When running as root via sudo or pkexec, ensure cache and settings directories
    // are tied to the original user, not root
    if (::geteuid() == 0) {
        // Check if we're running via sudo or pkexec
        const char* sudoUid = ::getenv("SUDO_UID");
        const char* pkexecUid = ::getenv("PKEXEC_UID");
        const char* originalUidStr = nullptr;
        const char* elevationMethod = nullptr;
        
        if (sudoUid) {
            originalUidStr = sudoUid;
            elevationMethod = "sudo";
        } else if (pkexecUid) {
            originalUidStr = pkexecUid;
            elevationMethod = "pkexec";
        }
        
        if (originalUidStr) {
            // We're running under sudo or pkexec - get the original user's information
            // Security: Use strtoul with validation instead of atoi to prevent integer overflow
            // An attacker could set SUDO_UID to a value that overflows uid_t
            char* endptr = nullptr;
            errno = 0;
            unsigned long parsedUid = std::strtoul(originalUidStr, &endptr, 10);
            
            // Validate the conversion:
            // 1. Check for conversion errors (errno set, or no digits consumed)
            // 2. Check for trailing garbage (endptr should point to '\0')
            // 3. Check for overflow (parsedUid > maximum uid_t value)
            if (errno != 0 || endptr == originalUidStr || *endptr != '\0' ||
                parsedUid > static_cast<unsigned long>(static_cast<uid_t>(-1))) {
                std::fprintf(stderr, "WARNING: Invalid UID value in environment: %s\n", originalUidStr);
                return;  // Don't proceed with invalid UID
            }
            
            uid_t originalUid = static_cast<uid_t>(parsedUid);
            
            // Use getpwuid_r for thread safety - getpwuid returns static buffer
            // that could be overwritten by another thread
            struct passwd pwBuf;
            struct passwd* pwResult = nullptr;
            char pwStrBuf[4096];  // Buffer for string fields (name, dir, shell, etc.)
            
            int pwErr = getpwuid_r(originalUid, &pwBuf, pwStrBuf, sizeof(pwStrBuf), &pwResult);
            if (pwErr != 0 || pwResult == nullptr) {
                std::fprintf(stderr, "WARNING: Could not retrieve user info for UID %lu: %s\n",
                             parsedUid, pwErr != 0 ? strerror(pwErr) : "user not found");
                return;
            }
            
            // Use pwResult (== &pwBuf) for the retrieved data
            if (pwResult->pw_dir) {
                // Use C-style strings since this happens before Qt is fully initialized
                char xdgCacheHome[512];
                char xdgConfigHome[512];
                char xdgDataHome[512];
                char xdgRuntimeDir[512];
                
                std::snprintf(xdgCacheHome, sizeof(xdgCacheHome), "%s/.cache", pwResult->pw_dir);
                std::snprintf(xdgConfigHome, sizeof(xdgConfigHome), "%s/.config", pwResult->pw_dir);
                std::snprintf(xdgDataHome, sizeof(xdgDataHome), "%s/.local/share", pwResult->pw_dir);
                std::snprintf(xdgRuntimeDir, sizeof(xdgRuntimeDir), "/run/user/%s", originalUidStr);
                
                std::fprintf(stderr, "Running as root via %s\n", elevationMethod);
                std::fprintf(stderr, "Original user: %s\n", pwResult->pw_name);
                std::fprintf(stderr, "Original UID: %s\n", originalUidStr);
                std::fprintf(stderr, "Original home directory: %s\n", pwResult->pw_dir);
                
                // Override HOME to point to the original user's home directory
                // This ensures QStandardPaths and QSettings use the correct user's directories
                ::setenv("HOME", pwResult->pw_dir, 1);
                
                // Set XDG environment variables to ensure proper directory usage
                // Qt respects XDG Base Directory specification on Linux
                ::setenv("XDG_CACHE_HOME", xdgCacheHome, 1);
                ::setenv("XDG_CONFIG_HOME", xdgConfigHome, 1);
                ::setenv("XDG_DATA_HOME", xdgDataHome, 1);
                
                // Set XDG_RUNTIME_DIR to point to the original user's runtime directory
                // This is crucial for D-Bus session bus communication
                ::setenv("XDG_RUNTIME_DIR", xdgRuntimeDir, 1);
                
                // Try to construct the D-Bus session bus address from the original user's runtime directory
                // This is needed for XDG Desktop Portal (file dialogs), suspend inhibitor, and other D-Bus services
                char dbusSessionAddress[1024];
                std::snprintf(dbusSessionAddress, sizeof(dbusSessionAddress), 
                             "unix:path=%s/bus", xdgRuntimeDir);
                ::setenv("DBUS_SESSION_BUS_ADDRESS", dbusSessionAddress, 1);
                
                // Preserve display-related environment variables from sudo/pkexec
                // These are needed for xdg-open and other GUI operations
                
                // First, check if these are already in the environment (common with sudo)
                const char* currentDisplay = ::getenv("DISPLAY");
                const char* currentXauthority = ::getenv("XAUTHORITY");
                const char* currentWaylandDisplay = ::getenv("WAYLAND_DISPLAY");
                
                // If not found, try to detect from the user's active session
                // For X11, common display values are :0, :1, etc.
                // For Wayland, common values are wayland-0, wayland-1, etc.
                if (!currentDisplay && !currentWaylandDisplay) {
                    // Try to find X11 socket in /tmp/.X11-unix/
                    // This is a common location for X11 display sockets
                    if (access("/tmp/.X11-unix/X0", F_OK) == 0) {
                        ::setenv("DISPLAY", ":0", 1);
                        std::fprintf(stderr, "Detected X11 display socket, set DISPLAY to: :0\n");
                    }
                    
                    // Check for Wayland socket in the user's XDG_RUNTIME_DIR
                    char waylandSocketPath[512];
                    std::snprintf(waylandSocketPath, sizeof(waylandSocketPath), 
                                 "%s/wayland-0", xdgRuntimeDir);
                    if (access(waylandSocketPath, F_OK) == 0) {
                        ::setenv("WAYLAND_DISPLAY", "wayland-0", 1);
                        std::fprintf(stderr, "Detected Wayland socket, set WAYLAND_DISPLAY to: wayland-0\n");
                    }
                } else {
                    // Display variables already set, just log them
                    if (currentDisplay) {
                        std::fprintf(stderr, "DISPLAY already set to: %s\n", currentDisplay);
                    }
                    if (currentWaylandDisplay) {
                        std::fprintf(stderr, "WAYLAND_DISPLAY already set to: %s\n", currentWaylandDisplay);
                    }
                }
                
                // For XAUTHORITY, if not set, try common locations
                if (!currentXauthority) {
                    // Common location is ~/.Xauthority
                    char xauthorityPath[512];
                    std::snprintf(xauthorityPath, sizeof(xauthorityPath), 
                                 "%s/.Xauthority", pwResult->pw_dir);
                    if (access(xauthorityPath, R_OK) == 0) {
                        ::setenv("XAUTHORITY", xauthorityPath, 1);
                        std::fprintf(stderr, "Set XAUTHORITY to: %s\n", xauthorityPath);
                    } else {
                        std::fprintf(stderr, "Note: XAUTHORITY not found, X11 apps may have permission issues\n");
                    }
                } else {
                    std::fprintf(stderr, "XAUTHORITY already set to: %s\n", currentXauthority);
                }
                
                std::fprintf(stderr, "Set HOME to: %s\n", pwResult->pw_dir);
                std::fprintf(stderr, "Set XDG_CACHE_HOME to: %s\n", xdgCacheHome);
                std::fprintf(stderr, "Set XDG_CONFIG_HOME to: %s\n", xdgConfigHome);
                std::fprintf(stderr, "Set XDG_DATA_HOME to: %s\n", xdgDataHome);
                std::fprintf(stderr, "Set XDG_RUNTIME_DIR to: %s\n", xdgRuntimeDir);
                std::fprintf(stderr, "Set DBUS_SESSION_BUS_ADDRESS to: %s\n", dbusSessionAddress);
            } else {
                std::fprintf(stderr, "WARNING: Could not retrieve original user information for UID: %s\n", originalUidStr);
            }
        }
    }
}

namespace {
    // Sound files in order of preference (Freedesktop sound theme)
    static const char* const SOUND_FILES[] = {
        "/usr/share/sounds/freedesktop/stereo/complete.oga",  // Completion notification
        "/usr/share/sounds/freedesktop/stereo/bell.oga",      // Bell/alert
        nullptr
    };
    
    // Find the first available sound file (cached, thread-safe)
    static const char* findSoundFile() {
        static const char* cachedSoundFile = nullptr;
        static std::once_flag soundFileOnce;
        
        std::call_once(soundFileOnce, []() {
            for (int i = 0; SOUND_FILES[i] != nullptr; i++) {
                if (access(SOUND_FILES[i], R_OK) == 0) {
                    cachedSoundFile = SOUND_FILES[i];
                    break;
                }
            }
        });
        return cachedSoundFile;
    }
    
    // Command availability cache to avoid repeated PATH searches
    enum class AudioCommand {
        CanberraGtkPlay,
        PwPlay,
        Aplay,
        Pactl,
        Beep,
        COUNT
    };
    
    // Thread-safe command availability cache using std::call_once per command
    static std::once_flag commandOnceFlags[static_cast<int>(AudioCommand::COUNT)];
    static bool commandExistsResult[static_cast<int>(AudioCommand::COUNT)] = {false};
    
    static bool commandExists(AudioCommand cmd) {
        int idx = static_cast<int>(cmd);
        std::call_once(commandOnceFlags[idx], [idx, cmd]() {
            const char* cmdName = nullptr;
            switch (cmd) {
                case AudioCommand::CanberraGtkPlay: cmdName = "canberra-gtk-play"; break;
                case AudioCommand::PwPlay: cmdName = "pw-play"; break;
                case AudioCommand::Aplay: cmdName = "aplay"; break;
                case AudioCommand::Pactl: cmdName = "pactl"; break;
                case AudioCommand::Beep: cmdName = "beep"; break;
                default: return;
            }
            commandExistsResult[idx] = !QStandardPaths::findExecutable(QString::fromLatin1(cmdName)).isEmpty();
        });
        return commandExistsResult[idx];
    }
}

bool isBeepAvailable() {
    // canberra-gtk-play uses the system sound theme - no file needed
    if (commandExists(AudioCommand::CanberraGtkPlay)) {
        return true;
    }
    
    // Other mechanisms need a sound file
    const char* soundFile = findSoundFile();
    if (soundFile) {
        if (commandExists(AudioCommand::PwPlay) || 
            commandExists(AudioCommand::Aplay) || 
            commandExists(AudioCommand::Pactl)) {
            return true;
        }
    }
    
    // PC speaker beep - no dependencies
    if (commandExists(AudioCommand::Beep)) {
        return true;
    }
    
    qDebug() << "No beep mechanism available on this Linux system";
    return false;
}

void beep() {
    // 1. canberra-gtk-play (XDG Sound Theme - best option, uses system theme)
    if (commandExists(AudioCommand::CanberraGtkPlay)) {
        if (QProcess::execute("canberra-gtk-play", {"--id=complete"}) == 0) {
            return;
        }
    }
    
    // Find a sound file for the remaining mechanisms (result is cached)
    const char* soundFile = findSoundFile();
    
    if (soundFile) {
        // 2. pw-play (PipeWire - default on modern distros including Raspberry Pi OS)
        if (commandExists(AudioCommand::PwPlay)) {
            if (QProcess::execute("pw-play", {soundFile}) == 0) {
                return;
            }
        }
        
        // 3. aplay (ALSA - widely available, but only supports WAV)
        if (commandExists(AudioCommand::Aplay)) {
            if (QProcess::execute("aplay", {"-q", soundFile}) == 0) {
                return;
            }
        }
        
        // 4. pactl (PulseAudio - legacy systems)
        if (commandExists(AudioCommand::Pactl)) {
            if (QProcess::execute("pactl", {"upload-sample", soundFile, "imager-beep"}) == 0) {
                QProcess::execute("pactl", {"play-sample", "imager-beep"});
                return;
            }
        }
    }
    
    // 5. PC speaker beep command
    if (commandExists(AudioCommand::Beep)) {
        if (QProcess::execute("beep", {}) == 0) {
            return;
        }
    }
    
    // 6. System bell via echo (rarely works in GUI environments, but worth trying)
    QProcess::execute("echo", {"-e", "\\a"});
    
    qDebug() << "Beep requested but no suitable audio mechanism found on this Linux system";
}

// Helper to validate network interface names for safe path construction
// Linux interface names can contain: letters, digits, hyphens, underscores, and dots
// but must not start with a dot or contain path traversal sequences
static bool isValidInterfaceName(const QString& name) {
    if (name.isEmpty() || name.length() > 15) {  // IFNAMSIZ is 16 including null
        return false;
    }
    // Must not start with dot (hidden files, path traversal)
    if (name.startsWith('.')) {
        return false;
    }
    // Check each character is allowed
    for (const QChar& c : name) {
        char ch = c.toLatin1();
        bool isAlnum = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || 
                       (ch >= '0' && ch <= '9');
        bool isAllowedSpecial = (ch == '-' || ch == '_' || ch == '.');
        if (!isAlnum && !isAllowedSpecial) {
            return false;
        }
    }
    return true;
}

bool hasNetworkConnectivity() {
    // Return cached value if valid (invalidated by netlink monitor on changes)
    if (g_networkConnectivityCacheValid.load(std::memory_order_relaxed)) {
        return g_cachedNetworkConnectivity.load(std::memory_order_relaxed);
    }
    
    // Check multiple indicators of network connectivity on Linux
    
    // Method 1: Check if any network interface (other than loopback) is up
    // This is fast (sysfs read) and avoids spawning processes
    QDir sysNet("/sys/class/net");
    if (sysNet.exists()) {
        QStringList interfaces = sysNet.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &iface : interfaces) {
            if (iface == "lo") continue; // Skip loopback
            
            // Security: Validate interface name to prevent path traversal attacks
            // A malicious interface name like "../../../etc/passwd" could read arbitrary files
            if (!isValidInterfaceName(iface)) {
                qWarning() << "Skipping invalid interface name:" << iface;
                continue;
            }
            
            // Check if interface is up
            QFile operstate(QString("/sys/class/net/%1/operstate").arg(iface));
            if (operstate.open(QIODevice::ReadOnly)) {
                QString state = QString::fromLatin1(operstate.readAll()).trimmed();
                operstate.close();
                if (state == "up") {
                    // Interface is up - cache and return
                    g_cachedNetworkConnectivity.store(true, std::memory_order_relaxed);
                    g_networkConnectivityCacheValid.store(true, std::memory_order_relaxed);
                    return true;
                }
            }
        }
    }
    
    // Method 2: Check NetworkManager (if available)
    // Only spawn nmcli if sysfs check didn't find connectivity
    // This is expensive (process spawn) so we do it last
    static std::once_flag nmcliOnce;
    static bool nmcliAvailable = false;
    std::call_once(nmcliOnce, []() {
        nmcliAvailable = !QStandardPaths::findExecutable("nmcli").isEmpty();
    });
    
    if (nmcliAvailable) {
        QProcess nmcli;
        nmcli.start("nmcli", QStringList() << "networking" << "connectivity" << "check");
        if (nmcli.waitForFinished(1000)) {
            QString output = QString::fromLatin1(nmcli.readAllStandardOutput()).trimmed();
            if (output == "full" || output == "limited") {
                g_cachedNetworkConnectivity.store(true, std::memory_order_relaxed);
                g_networkConnectivityCacheValid.store(true, std::memory_order_relaxed);
                return true;
            }
        }
    }
    
    // No connectivity found - cache the result
    g_cachedNetworkConnectivity.store(false, std::memory_order_relaxed);
    g_networkConnectivityCacheValid.store(true, std::memory_order_relaxed);
    return false;
}

bool isNetworkReady() {
    // First check basic connectivity
    if (!hasNetworkConnectivity()) {
        return false;
    }
    
    // Check if systemd-timesyncd has synchronized time
    // This is important for embedded systems where the RTC might not be set
    // systemd-timesyncd updates the timestamp of /var/lib/systemd/timesync/clock when synced
    QFile clockFile("/var/lib/systemd/timesync/clock");
    QFile timesyncBinary("/lib/systemd/systemd-timesyncd");
    
    // If systemd-timesyncd is not present, assume time is reliable
    if (!timesyncBinary.exists()) {
        return true;
    }
    
    // If clock file doesn't exist yet, time hasn't been synced
    if (!clockFile.exists()) {
        qDebug() << "systemd-timesyncd clock file does not exist - time not yet synchronized";
        return false;
    }
    
    // Check if clock file has been updated after the timesync binary was installed
    // This indicates that time synchronization has occurred
    QFileInfo clockInfo(clockFile);
    QFileInfo binaryInfo(timesyncBinary);
    
    bool timeIsSynced = clockInfo.lastModified() > binaryInfo.lastModified();
    if (!timeIsSynced) {
        qDebug() << "systemd-timesyncd has not yet synchronized time";
    }
    
    return timeIsSynced;
}

void startNetworkMonitoring(NetworkStatusCallback callback) {
    // Stop any existing monitoring
    stopNetworkMonitoring();
    
    // Set callback under mutex
    pthread_mutex_lock(&g_callbackMutex);
    g_networkCallback = callback;
    pthread_mutex_unlock(&g_callbackMutex);
    
    // Create netlink socket for routing/link messages
    // Use SOCK_CLOEXEC to prevent fd leak to child processes after fork/exec
    g_netlinkSocket = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (g_netlinkSocket < 0) {
        int savedErrno = errno;
        fprintf(stderr, "Failed to create netlink socket: %s (errno %d)\n", strerror(savedErrno), savedErrno);
        return;
    }
    
    // Bind to multicast group for link changes
    struct sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;  // Subscribe to link up/down events
    
    if (bind(g_netlinkSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        int savedErrno = errno;
        fprintf(stderr, "Failed to bind netlink socket: %s (errno %d)\n", strerror(savedErrno), savedErrno);
        close(g_netlinkSocket);
        g_netlinkSocket = -1;
        return;
    }
    
    // Create eventfd to signal thread to stop
    // eventfd is more efficient than pipe (single fd, 8-byte counter, lighter weight)
    // EFD_CLOEXEC prevents fd leak to child processes
    g_stopEventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_stopEventFd < 0) {
        int savedErrno = errno;
        fprintf(stderr, "Failed to create stop eventfd: %s (errno %d)\n", strerror(savedErrno), savedErrno);
        close(g_netlinkSocket);
        g_netlinkSocket = -1;
        return;
    }
    
    // Start monitoring thread
    g_monitorRunning.store(true, std::memory_order_release);
    
    // Create thread with explicit attributes for better control
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    int err = pthread_create(&g_monitorThread, &attr, netlinkMonitorThread, nullptr);
    pthread_attr_destroy(&attr);
    
    if (err != 0) {
        fprintf(stderr, "Failed to create monitor thread: %s (errno %d)\n", strerror(err), err);
        close(g_netlinkSocket);
        g_netlinkSocket = -1;
        close(g_stopEventFd);
        g_stopEventFd = -1;
        g_monitorRunning.store(false, std::memory_order_release);
        return;
    }
    
    fprintf(stderr, "Network monitoring started (netlink + poll)\n");
}

void stopNetworkMonitoring() {
    if (g_monitorRunning.load(std::memory_order_acquire)) {
        g_monitorRunning.store(false, std::memory_order_release);
        
        // Signal thread to stop via eventfd
        // Write any non-zero value to wake up poll()
        if (g_stopEventFd >= 0) {
            uint64_t val = 1;
            ssize_t written = write(g_stopEventFd, &val, sizeof(val));
            (void)written;  // Ignore errors - thread will exit on timeout anyway
        }
        
        // Wait for thread to finish
        pthread_join(g_monitorThread, nullptr);
        
        // Clean up file descriptors
        if (g_netlinkSocket >= 0) {
            close(g_netlinkSocket);
            g_netlinkSocket = -1;
        }
        if (g_stopEventFd >= 0) {
            close(g_stopEventFd);
            g_stopEventFd = -1;
        }
        
        fprintf(stderr, "Network monitoring stopped\n");
    }
    
    // Clear callback under mutex to prevent race with monitor thread
    pthread_mutex_lock(&g_callbackMutex);
    g_networkCallback = nullptr;
    pthread_mutex_unlock(&g_callbackMutex);
}

void bringWindowToForeground(void* windowHandle) {
    // No-op on Linux - window management is handled by the window manager
    // and applications cannot force themselves to the foreground
    Q_UNUSED(windowHandle);
}

bool hasElevatedPrivileges() {
    // Check if running as root (UID 0)
    return ::geteuid() == 0;
}

void attachConsole() {
    // No-op on Linux - console is already available
}

const char* getBundlePath() {
    // APPIMAGE is set by the AppImage runtime before our code runs
    return ::getenv("APPIMAGE");
}

bool isElevatableBundle() {
    const char* path = getBundlePath();
    // Sanity check: verify the file actually exists
    return path && access(path, F_OK) == 0;
}

// Internal helper to generate unique policy filename
static bool generatePolkitPolicyFilename(const char* appImagePath, char* buffer, size_t bufferSize) {
    if (!appImagePath || !buffer || bufferSize < 64) {
        return false;
    }
    
    // Generate a hash of the AppImage path to create a unique filename
    // This ensures each AppImage location gets its own policy file
    QByteArray pathBytes(appImagePath);
    QByteArray hash = QCryptographicHash::hash(pathBytes, QCryptographicHash::Md5).toHex();
    
    // Create filename: com.raspberrypi.rpi-imager.appimage-HASH.policy
    int written = std::snprintf(buffer, bufferSize, 
        "com.raspberrypi.rpi-imager.appimage-%s.policy",
        hash.left(12).constData());  // Use first 12 chars of hash
    
    return written > 0 && static_cast<size_t>(written) < bufferSize;
}

// Internal helper to XML-escape a string for safe embedding in XML content
static QString xmlEscape(const QString& input) {
    QString result;
    result.reserve(input.size() + 16);  // Reserve a bit extra for escapes
    
    for (const QChar& c : input) {
        switch (c.unicode()) {
            case '&':  result += QStringLiteral("&amp;");  break;
            case '<':  result += QStringLiteral("&lt;");   break;
            case '>':  result += QStringLiteral("&gt;");   break;
            case '"':  result += QStringLiteral("&quot;"); break;
            case '\'': result += QStringLiteral("&apos;"); break;
            default:
                // Also escape control characters (except tab, newline, carriage return)
                if (c.unicode() < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                    result += QString("&#x%1;").arg(c.unicode(), 0, 16);
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

// Internal helper to check if policy exists for a specific path
static bool hasPolkitPolicyForPath(const char* appImagePath) {
    if (!appImagePath) {
        return false;
    }
    
    char policyFilename[256];
    if (!generatePolkitPolicyFilename(appImagePath, policyFilename, sizeof(policyFilename))) {
        return false;
    }
    
    char policyPath[512];
    std::snprintf(policyPath, sizeof(policyPath), 
        "/usr/share/polkit-1/actions/%s", policyFilename);
    
    // Security: Removed redundant access() check to eliminate TOCTOU race condition.
    // The file could be swapped between access() and open(). Instead, just try
    // to open the file directly - if it doesn't exist, open() will fail.
    
    // Verify the policy file exists and contains the correct path
    // (in case the AppImage was moved but hash collision occurred)
    QFile policyFile(policyPath);
    if (!policyFile.open(QIODevice::ReadOnly)) {
        return false;  // File doesn't exist or can't be read
    }
    
    QByteArray content = policyFile.readAll();
    policyFile.close();
    
    // Check if the policy contains the exact AppImage path (XML-escaped form)
    // Since we now XML-escape the path when writing, we must search for the escaped form
    QString escapedPath = xmlEscape(QString::fromUtf8(appImagePath));
    QString searchPath = QString("org.freedesktop.policykit.exec.path\">%1</annotate>")
        .arg(escapedPath);
    
    return content.contains(searchPath.toUtf8());
}

bool hasElevationPolicyInstalled() {
    const char* bundlePath = getBundlePath();
    if (!bundlePath) {
        return false;
    }
    return hasPolkitPolicyForPath(bundlePath);
}

// Internal helper to install polkit policy for a specific path
static bool installPolkitPolicyForPath(const char* appImagePath) {
    if (!appImagePath || ::geteuid() != 0) {
        return false;
    }
    
    // Verify the AppImage path exists and is a regular file
    struct stat st;
    if (stat(appImagePath, &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    
    // Security: Validate the path doesn't contain suspicious characters
    // that could be used for XML injection attacks
    QString pathStr = QString::fromUtf8(appImagePath);
    
    // Reject paths with null bytes (could truncate strings in C code)
    if (pathStr.contains(QChar('\0'))) {
        // Log the path length and first portion for debugging (don't log full path - could be malicious)
        std::fprintf(stderr, "Security: Rejecting AppImage path with embedded null byte (length=%d, prefix=%.50s...)\n",
                     static_cast<int>(strlen(appImagePath)), appImagePath);
        return false;
    }
    
    char policyFilename[256];
    if (!generatePolkitPolicyFilename(appImagePath, policyFilename, sizeof(policyFilename))) {
        return false;
    }
    
    char policyPath[512];
    std::snprintf(policyPath, sizeof(policyPath), 
        "/usr/share/polkit-1/actions/%s", policyFilename);
    
    // Generate unique action ID based on path hash
    QByteArray pathBytes(appImagePath);
    QByteArray hash = QCryptographicHash::hash(pathBytes, QCryptographicHash::Md5).toHex();
    QString actionId = QString("com.raspberrypi.rpi-imager.appimage.%1").arg(QString::fromUtf8(hash.left(12)));
    
    // Security: XML-escape the AppImage path to prevent XML injection attacks
    // An attacker-controlled path like "</annotate><evil>..." could break the XML
    QString escapedPath = xmlEscape(pathStr);
    
    // Create policy XML with escaped path
    QString policyContent = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE policyconfig PUBLIC\n"
        " \"-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/PolicyKit/1.0/policyconfig.dtd\">\n"
        "<policyconfig>\n"
        "  <vendor>Raspberry Pi Ltd</vendor>\n"
        "  <vendor_url>https://www.raspberrypi.com/</vendor_url>\n"
        "  <action id=\"%1\">\n"
        "    <description>Run Raspberry Pi Imager</description>\n"
        "    <message>Authentication is required to run Raspberry Pi Imager</message>\n"
        "    <icon_name>rpi-imager</icon_name>\n"
        "    <defaults>\n"
        "      <allow_any>auth_admin</allow_any>\n"
        "      <allow_inactive>auth_admin</allow_inactive>\n"
        "      <allow_active>auth_admin_keep</allow_active>\n"
        "    </defaults>\n"
        "    <annotate key=\"org.freedesktop.policykit.exec.path\">%2</annotate>\n"
        "    <annotate key=\"org.freedesktop.policykit.exec.allow_gui\">true</annotate>\n"
        "  </action>\n"
        "</policyconfig>\n"
    ).arg(actionId, escapedPath);
    
    // Write policy file
    // O_CLOEXEC prevents fd leak to child processes after fork/exec
    int fd = open(policyPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        int savedErrno = errno;
        std::fprintf(stderr, "Failed to create policy file %s: %s\n", policyPath, strerror(savedErrno));
        return false;
    }
    
    QByteArray contentBytes = policyContent.toUtf8();
    ssize_t written = write(fd, contentBytes.constData(), static_cast<size_t>(contentBytes.size()));
    
    if (written < 0 || static_cast<size_t>(written) != static_cast<size_t>(contentBytes.size())) {
        int savedErrno = errno;
        std::fprintf(stderr, "Failed to write policy file: %s\n", strerror(savedErrno));
        close(fd);
        unlink(policyPath);
        return false;
    }
    
    // fsync to ensure data is flushed to disk before close
    // This prevents data loss if system crashes immediately after
    // Also sync the parent directory to ensure the directory entry is persisted
    if (fsync(fd) != 0) {
        int savedErrno = errno;
        std::fprintf(stderr, "Failed to fsync policy file: %s\n", strerror(savedErrno));
        close(fd);
        unlink(policyPath);
        return false;
    }
    
    close(fd);
    
    // fsync parent directory to ensure the directory entry is persisted
    // This is often overlooked but necessary for full durability
    int dirFd = open("/usr/share/polkit-1/actions", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirFd >= 0) {
        fsync(dirFd);
        close(dirFd);
    }
    
    return true;
}

bool installElevationPolicy() {
    const char* bundlePath = getBundlePath();
    return bundlePath ? installPolkitPolicyForPath(bundlePath) : false;
}

bool launchDetached(const QString& program, const QStringList& arguments) {
    // Use double-fork pattern to create a fully detached process
    // that will outlive the parent and not become a zombie.
    //
    // Note: We do NOT call setsid() because that would create a new session
    // and break D-Bus/display connections needed for GUI applications.
    
    pid_t pid = fork();
    
    if (pid < 0) {
        qWarning() << "launchDetached: fork failed:" << strerror(errno);
        return false;
    }
    
    if (pid == 0) {
        // First child - fork again to fully detach
        pid_t pid2 = fork();
        
        if (pid2 < 0) {
            _exit(1);
        }
        
        if (pid2 > 0) {
            // First child exits, orphaning the grandchild (adopted by init)
            _exit(0);
        }
        
        // Grandchild - build argv and exec
        
        // Clear AppImage environment before running external tools
        clearAppImageEnvironment();
        
        QByteArray programBytes = program.toUtf8();
        std::vector<QByteArray> argBytes;
        std::vector<char*> argv;
        
        argv.push_back(programBytes.data());
        for (const QString& arg : arguments) {
            argBytes.push_back(arg.toUtf8());
            argv.push_back(argBytes.back().data());
        }
        argv.push_back(nullptr);
        
        execvp(programBytes.constData(), argv.data());
        _exit(127);  // exec failed
    }
    
    // Parent - wait for first child to exit
    int status;
    waitpid(pid, &status, 0);
    
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool tryElevate(int argc, char** argv) {
    // Only attempt elevation if running from AppImage, not root, and have policy
    const char* appImagePath = getBundlePath();
    if (!appImagePath || ::geteuid() == 0) {
        return false;
    }
    
    if (access("/usr/bin/pkexec", X_OK) != 0 || !hasPolkitPolicyForPath(appImagePath)) {
        return false;
    }
    
    // Build argument list: pkexec --disable-internal-agent /path/to/appimage [args...]
    char** newArgv = new char*[argc + 3];
    int newArgc = 0;
    
    newArgv[newArgc++] = strdup("/usr/bin/pkexec");
    newArgv[newArgc++] = strdup("--disable-internal-agent");
    newArgv[newArgc++] = strdup(appImagePath);
    
    for (int i = 1; i < argc; i++) {
        newArgv[newArgc++] = strdup(argv[i]);
    }
    newArgv[newArgc] = nullptr;
    
    pid_t pid = fork();
    
    if (pid < 0) {
        for (int i = 0; i < newArgc; i++) free(newArgv[i]);
        delete[] newArgv;
        return false;
    }
    
    if (pid == 0) {
        execv("/usr/bin/pkexec", newArgv);
        _exit(127);
    }
    
    int status;
    waitpid(pid, &status, 0);
    
    for (int i = 0; i < newArgc; i++) free(newArgv[i]);
    delete[] newArgv;
    
    if (WIFEXITED(status)) {
        int exitCode = WEXITSTATUS(status);
        if (exitCode == 0) {
            _exit(0);  // Elevated process completed successfully
        } else if (exitCode == 126 || exitCode == 127) {
            return false;  // User cancelled or auth failed
        } else {
            _exit(exitCode);
        }
    } else if (WIFSIGNALED(status)) {
        _exit(128 + WTERMSIG(status));
    }
    
    return false;
}

bool runElevatedPolicyInstaller() {
    const char* bundlePath = getBundlePath();
    if (!bundlePath) {
        return false;
    }
    
    // Use pkexec to run ourselves with --install-elevation-policy
    QStringList args;
    args << "--disable-internal-agent";
    args << QString::fromUtf8(bundlePath);
    args << "--install-elevation-policy";
    
    QProcess process;
    process.start("/usr/bin/pkexec", args);
    
    if (!process.waitForStarted(5000) || !process.waitForFinished(60000)) {
        process.kill();
        return false;
    }
    
    return process.exitCode() == 0;
}

void execElevated(const QStringList& extraArgs) {
    const char* bundlePath = getBundlePath();
    if (!bundlePath) {
        return;
    }
    
    // Build argument list for exec
    std::vector<std::string> argStrings;
    argStrings.push_back("pkexec");
    argStrings.push_back("--disable-internal-agent");
    argStrings.push_back(bundlePath);
    
    for (const QString& arg : extraArgs) {
        argStrings.push_back(arg.toStdString());
    }
    
    std::vector<char*> argv;
    for (auto& s : argStrings) {
        argv.push_back(const_cast<char*>(s.c_str()));
    }
    argv.push_back(nullptr);
    
    fflush(stdout);
    fflush(stderr);
    execvp("pkexec", argv.data());
    
    // Only reached if exec failed
    qWarning() << "Failed to exec pkexec:" << strerror(errno);
}

bool isScrollInverted(bool qtInvertedFlag) {
    // On Linux, Qt's inverted flag behavior varies by desktop environment.
    // Most modern DEs (GNOME, KDE) correctly report it, so we pass through.
    return qtInvertedFlag;
}

QString getWriteDevicePath(const QString& devicePath) {
    // Linux uses the same device path for both buffered and direct I/O.
    // Direct I/O is controlled via O_DIRECT flag, not device path.
    return devicePath;
}

QString getEjectDevicePath(const QString& devicePath) {
    // No path transformation needed on Linux.
    return devicePath;
}

DiskResult unmountDisk(const QString& device) {
    QByteArray deviceBytes = device.toUtf8();
    const char* devicePath = deviceBytes.constData();
    
    // Verify device exists and is not a directory
    struct stat stats;
    if (stat(devicePath, &stats) != 0) {
        int savedErrno = errno;
        qWarning() << "unmountDisk: stat failed for" << device << "-" << strerror(savedErrno);
        return DiskResult::InvalidDrive;
    }
    if (S_ISDIR(stats.st_mode)) {
        qWarning() << "unmountDisk: path is a directory, not a device:" << device;
        return DiskResult::InvalidDrive;
    }
    
    // Find all mount points for this device and its partitions
    std::vector<std::string> mountDirs;
    
    FILE* procMounts = setmntent("/proc/mounts", "r");
    if (!procMounts) {
        int savedErrno = errno;
        qWarning() << "unmountDisk: couldn't read /proc/mounts -" << strerror(savedErrno);
        return DiskResult::Error;
    }
    
    struct mntent* mnt;
    struct mntent data;
    char mntBuf[4096 + 1024];  // Buffer for getmntent_r
    
    while ((mnt = getmntent_r(procMounts, &data, mntBuf, sizeof(mntBuf)))) {
        // Check if this mount is on the device or any of its partitions
        // Match exact device path or device path followed by a partition number (digit)
        // This prevents matching /dev/sda when we have /dev/sda_backup or similar
        size_t devicePathLen = strlen(devicePath);
        if (strncmp(mnt->mnt_fsname, devicePath, devicePathLen) == 0) {
            char nextChar = mnt->mnt_fsname[devicePathLen];
            // Accept exact match, partition number (digit), or 'p' followed by digit (nvme style)
            if (nextChar == '\0' || 
                (nextChar >= '0' && nextChar <= '9') ||
                (nextChar == 'p' && mnt->mnt_fsname[devicePathLen + 1] >= '0' && 
                 mnt->mnt_fsname[devicePathLen + 1] <= '9')) {
                qDebug() << "unmountDisk: found mount" << mnt->mnt_dir << "for" << mnt->mnt_fsname;
                mountDirs.push_back(mnt->mnt_dir);
            }
        }
    }
    endmntent(procMounts);
    
    if (mountDirs.empty()) {
        qDebug() << "unmountDisk: no mounts found for" << device;
        return DiskResult::Success;  // Nothing to unmount
    }
    
    // Unmount each mount point
    // Strategy:
    // 1. Try normal unmount first (no flags) - cleanest, ensures all data flushed
    // 2. MNT_EXPIRE (mark for expiry, second call unmounts if idle)
    // 3. MNT_DETACH (lazy unmount) - WARNING: can leave fs in inconsistent state
    //    if there are open files, use only after user has been warned
    // 4. MNT_FORCE - for truly stuck filesystems (usually network mounts)
    //
    // Note: We prefer MNT_DETACH over MNT_FORCE because MNT_FORCE can corrupt
    // data on some filesystem types, while MNT_DETACH is safer (waits for
    // open files to close before actual unmount).
    
    size_t unmountCount = 0;
    std::vector<std::string> failedMounts;
    
    for (const std::string& mountDir : mountDirs) {
        const char* mountPath = mountDir.c_str();
        bool unmounted = false;
        
        // First attempt: normal unmount (cleanest, waits for all I/O)
        if (umount(mountPath) == 0) {
            qDebug() << "unmountDisk: unmounted" << mountPath << "(normal)";
            unmounted = true;
        }
        // Second attempt: MNT_EXPIRE (mark for expiry)
        else if (umount2(mountPath, MNT_EXPIRE) == 0) {
            qDebug() << "unmountDisk: unmounted" << mountPath << "(MNT_EXPIRE first call)";
            unmounted = true;
        }
        // Third attempt: MNT_EXPIRE again (actually unmounts if still idle)
        else if (umount2(mountPath, MNT_EXPIRE) == 0) {
            qDebug() << "unmountDisk: unmounted" << mountPath << "(MNT_EXPIRE second call)";
            unmounted = true;
        }
        // Fourth attempt: MNT_DETACH (lazy unmount)
        // This makes the mount point unavailable immediately but actual unmount
        // happens when all open file handles are closed. Safe for our use case
        // since we're about to overwrite the device anyway.
        else if (umount2(mountPath, MNT_DETACH) == 0) {
            qDebug() << "unmountDisk: unmounted" << mountPath << "(MNT_DETACH/lazy)";
            unmounted = true;
        }
        // Last resort: MNT_FORCE (can cause data loss on some filesystems!)
        else if (umount2(mountPath, MNT_FORCE) == 0) {
            qWarning() << "unmountDisk: force-unmounted" << mountPath << "(MNT_FORCE - may cause data loss)";
            unmounted = true;
        }
        
        if (unmounted) {
            unmountCount++;
        } else {
            int savedErrno = errno;
            qWarning() << "unmountDisk: failed to unmount" << mountPath << ":" << strerror(savedErrno);
            failedMounts.push_back(mountDir);
        }
    }
    
    if (unmountCount == mountDirs.size()) {
        return DiskResult::Success;
    } else if (unmountCount == 0) {
        // All mounts failed - likely a permissions or busy issue
        qWarning() << "unmountDisk: all" << mountDirs.size() << "mounts failed for" << device;
        return DiskResult::Busy;
    } else {
        // Partial success - some mounts succeeded, some failed
        // This is still a failure but we should log what succeeded for debugging
        qWarning() << "unmountDisk: partial failure -" << unmountCount << "of" 
                   << mountDirs.size() << "mounts succeeded for" << device;
        qWarning() << "unmountDisk: failed mounts:" << failedMounts.size();
        for (const auto& failed : failedMounts) {
            qWarning() << "  -" << QString::fromStdString(failed);
        }
        return DiskResult::Busy;
    }
}

DiskResult ejectDisk(const QString& device) {
    // On Linux, ejecting is essentially the same as unmounting.
    // The kernel handles making the device safe to remove.
    // For true hardware eject (e.g., CD drives), we would use CDROMEJECT ioctl,
    // but for SD cards and USB drives, unmounting is sufficient.
    return unmountDisk(device);
}

const char* findCACertBundle()
{
    // Cache the result - this is called on every curl handle setup
    static const char* cachedPath = nullptr;
    static bool cacheInitialized = false;
    
    if (cacheInitialized) {
        return cachedPath;
    }
    
    // Common CA certificate bundle paths across Linux distributions.
    // AppImages and other portable distributions bundle libcurl with a
    // hardcoded CA certificate path from the build system. When run on a
    // different distribution, this path may not exist, causing SSL/TLS
    // connections to fail.
    //
    // Order matters: more common/modern paths first for faster lookup.
    static const char* caPaths[] = {
        "/etc/ssl/certs/ca-certificates.crt",                    // Debian, Ubuntu, Arch, Gentoo
        "/etc/pki/tls/certs/ca-bundle.crt",                      // Fedora, RHEL, CentOS
        "/etc/ssl/ca-bundle.pem",                                // OpenSUSE
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",     // CentOS/RHEL 7+
        "/etc/ssl/cert.pem",                                     // Alpine
        "/etc/pki/tls/cacert.pem",                               // OpenELEC
        "/etc/ca-certificates/extracted/tls-ca-bundle.pem",      // Arch with ca-certificates-utils
        "/usr/local/share/certs/ca-root-nss.crt",                // FreeBSD
        "/usr/share/ssl/certs/ca-bundle.crt",                    // Older RHEL
        nullptr
    };

    for (int i = 0; caPaths[i] != nullptr; i++)
    {
        if (access(caPaths[i], R_OK) == 0)
        {
            cachedPath = caPaths[i];
            break;
        }
    }
    
    cacheInitialized = true;
    return cachedPath;  // May be nullptr if not found, curl will use its compiled-in default
}

void clearAppImageEnvironment() {
    // AppImages set LD_LIBRARY_PATH and LD_PRELOAD to use bundled libraries.
    // External tools need system libraries instead, otherwise they may fail
    // due to symbol conflicts (e.g., PAM modules failing with "cannot open
    // session: Module is unknown", or KDE tools failing with Qt version
    // mismatches like "version `Qt_6.10' not found").
    //
    // This is safe because forked children running external tools don't need
    // our bundled libraries.
    unsetenv("LD_LIBRARY_PATH");
    unsetenv("LD_PRELOAD");
}

} // namespace PlatformQuirks