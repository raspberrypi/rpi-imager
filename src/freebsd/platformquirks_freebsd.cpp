/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * FreeBSD platform-specific implementation.
 */

#include "../platformquirks.h"
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <libgeom.h>
#include <sys/eventfd.h>  // eventfd for modern thread signaling
#include <sys/utsname.h>   // uname() for kernel version logging
#include <poll.h>          // poll() instead of select()
#include <signal.h>        // Signal masking for worker thread
#include <ifaddrs.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/netlink_route.h>
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
#include <QSaveFile>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <filesystem>
#include <QUuid>
#include <vector>
#include <string>

#ifdef QT_DBUS_LIB
// xdg-desktop-portal OpenURI is only used by builds that link Qt DBus (the GUI
// app). QT_DBUS_LIB is defined automatically by Qt when the DBus module is
// linked, so the CLI build and the DBus-free PAL unit test exclude this cleanly.
#include <QVariant>
#include <QMap>
#include <QDBusConnection>
#include <QDBusMessage>
#endif

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
        return nullptr;
    }

    // Grant root access to the user's X11 display via xhost.
    // Must be run as the original (non-root) user who owns the display session,
    // since root doesn't yet have permission to connect.
    // Uses +SI:localuser:root (server-interpreted, narrowly scoped to root only).
    void grantRootDisplayAccess(uid_t uid, gid_t gid) {
    }
}

namespace PlatformQuirks {

void applyQuirks() {
}

namespace {
    // Command availability cache to avoid repeated PATH searches
    enum class AudioCommand {
        CanberraGtkPlay,
        PwPlay,
        Pactl,
        Beep,
        COUNT
    };
}

bool isBeepAvailable() {
    return false;
}

void beep() {
}

bool hasNetworkConnectivity() {
    return false;
}

bool isNetworkReady() {
    return false;
}

void startNetworkMonitoring(NetworkStatusCallback callback) {
}

void stopNetworkMonitoring() {
}

void bringWindowToForeground(void* windowHandle) {
}

bool hasElevatedPrivileges() {
    return false;
}

void attachConsole() {
}

const char* getBundlePath() {
    return nullptr;
}

bool isElevatableBundle() {
    return false;
}

bool hasElevationPolicyInstalled() {
    return false;
}

bool installElevationPolicy() {
    return false;
}

bool launchDetached(const QString& program, const QStringList& arguments) {
    return false;
}

bool tryElevate(int argc, char** argv) {
    return false;
}

bool runElevatedPolicyInstaller() {
    return false;
}

void execElevated(const QStringList& extraArgs) {
}

bool isScrollInverted(bool qtInvertedFlag) {
    return false;
}

bool prefersReducedMotion() {
    return false;
}

QString getWriteDevicePath(const QString& devicePath) {
    return devicePath;
}

QString getEjectDevicePath(const QString& devicePath) {
    return devicePath;
}

DiskResult unmountDisk(const QString& device) {
    return DiskResult::Success;
}

DiskResult ejectDisk(const QString& device) {
    return DiskResult::Success;
}

DiskResult refreshDiskView(const QString& device) {
    return DiskResult::Success;
}

const char* findCACertBundle()
{
    return nullptr;
}

void clearAppImageEnvironment() {
}

bool registerUriScheme() {
    return true;
}

qreal detectTextScaleFactor()
{
    return 1.0;
}

qreal fontDpiCorrection()
{
    return 72.0 / 96.0;
}

} // namespace PlatformQuirks
