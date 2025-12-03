/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <QDebug>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <vector>

namespace PlatformQuirks {

void applyQuirks() {
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
            uid_t originalUid = static_cast<uid_t>(::atoi(originalUidStr));
            struct passwd* pw = ::getpwuid(originalUid);
            
            if (pw && pw->pw_dir) {
                // Use C-style strings since this happens before Qt is fully initialized
                char xdgCacheHome[512];
                char xdgConfigHome[512];
                char xdgDataHome[512];
                char xdgRuntimeDir[512];
                
                std::snprintf(xdgCacheHome, sizeof(xdgCacheHome), "%s/.cache", pw->pw_dir);
                std::snprintf(xdgConfigHome, sizeof(xdgConfigHome), "%s/.config", pw->pw_dir);
                std::snprintf(xdgDataHome, sizeof(xdgDataHome), "%s/.local/share", pw->pw_dir);
                std::snprintf(xdgRuntimeDir, sizeof(xdgRuntimeDir), "/run/user/%s", originalUidStr);
                
                std::fprintf(stderr, "Running as root via %s\n", elevationMethod);
                std::fprintf(stderr, "Original user: %s\n", pw->pw_name);
                std::fprintf(stderr, "Original UID: %s\n", originalUidStr);
                std::fprintf(stderr, "Original home directory: %s\n", pw->pw_dir);
                
                // Override HOME to point to the original user's home directory
                // This ensures QStandardPaths and QSettings use the correct user's directories
                ::setenv("HOME", pw->pw_dir, 1);
                
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
                                 "%s/.Xauthority", pw->pw_dir);
                    if (access(xauthorityPath, R_OK) == 0) {
                        ::setenv("XAUTHORITY", xauthorityPath, 1);
                        std::fprintf(stderr, "Set XAUTHORITY to: %s\n", xauthorityPath);
                    } else {
                        std::fprintf(stderr, "Note: XAUTHORITY not found, X11 apps may have permission issues\n");
                    }
                } else {
                    std::fprintf(stderr, "XAUTHORITY already set to: %s\n", currentXauthority);
                }
                
                std::fprintf(stderr, "Set HOME to: %s\n", pw->pw_dir);
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

void beep() {
    // Try multiple Linux beep mechanisms in order of preference
    
    // 1. Try pactl (PulseAudio) beep - most common on modern Linux desktop systems
    if (QProcess::execute("pactl", QStringList() << "upload-sample" << "/usr/share/sounds/alsa/Front_Left.wav" << "beep") == 0) {
        QProcess::execute("pactl", QStringList() << "play-sample" << "beep");
        return;
    }
    
    // 2. Try system bell via echo (works on most terminals)
    if (QProcess::execute("echo", QStringList() << "-e" << "\\a") == 0) {
        return;
    }
    
    // 3. Try beep command if available
    if (QProcess::execute("beep", QStringList()) == 0) {
        return;
    }
    
    // 4. Fallback: just log that beep was requested
    qDebug() << "Beep requested but no suitable audio mechanism found on this Linux system";
}

bool hasNetworkConnectivity() {
    // Check multiple indicators of network connectivity on Linux
    
    // Method 1: Check if any network interface (other than loopback) is up
    QDir sysNet("/sys/class/net");
    if (sysNet.exists()) {
        QStringList interfaces = sysNet.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &iface : interfaces) {
            if (iface == "lo") continue; // Skip loopback
            
            // Check if interface is up
            QFile operstate(QString("/sys/class/net/%1/operstate").arg(iface));
            if (operstate.open(QIODevice::ReadOnly)) {
                QString state = QString::fromLatin1(operstate.readAll()).trimmed();
                operstate.close();
                if (state == "up") {
                    // Interface is up - likely have connectivity
                    return true;
                }
            }
        }
    }
    
    // Method 2: Check NetworkManager (if available)
    QProcess nmcli;
    nmcli.start("nmcli", QStringList() << "networking" << "connectivity" << "check");
    if (nmcli.waitForFinished(1000)) {
        QString output = QString::fromLatin1(nmcli.readAllStandardOutput()).trimmed();
        if (output == "full" || output == "limited") {
            return true;
        }
    }
    
    // If we can't determine connectivity, assume offline for safety
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
    
    // Check if file exists
    if (access(policyPath, F_OK) != 0) {
        return false;
    }
    
    // Verify the policy file contains the correct path
    // (in case the AppImage was moved but hash collision occurred)
    QFile policyFile(policyPath);
    if (!policyFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QByteArray content = policyFile.readAll();
    policyFile.close();
    
    // Check if the policy contains the exact AppImage path
    QString searchPath = QString("org.freedesktop.policykit.exec.path\">%1</annotate>")
        .arg(QString::fromUtf8(appImagePath));
    
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
    
    // Create policy XML
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
    ).arg(actionId, QString::fromUtf8(appImagePath));
    
    // Write policy file
    int fd = open(policyPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
    
    QByteArray contentBytes = policyContent.toUtf8();
    ssize_t written = write(fd, contentBytes.constData(), contentBytes.size());
    close(fd);
    
    if (written != contentBytes.size()) {
        unlink(policyPath);
        return false;
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

} // namespace PlatformQuirks