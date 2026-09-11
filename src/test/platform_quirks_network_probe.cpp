/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for PlatformQuirks::hasNetworkConnectivity().
 *
 * The answer decides whether Imager fetches the OS list or shows the offline
 * screen, and it is worked out by walking /sys/class/net and reading each
 * interface's operstate. That path is hardcoded, so the caller bind-mounts a
 * synthetic one over it inside an unprivileged mount namespace -- the same
 * technique embedded_scaling/run.sh uses for /sys/class/drm.
 *
 * Prints CONNECTIVITY=1 or CONNECTIVITY=0 on stdout. The function caches its
 * answer in process-wide state, so each case gets a fresh process.
 */

#include "platformquirks.h"

#include <QUrl>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <unistd.h>
#include <sys/resource.h>

namespace {

// How many descriptors this process currently holds.
int openDescriptors()
{
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    int n = 0;
    while (readdir(d))
        ++n;
    closedir(d);
    return n;
}

// Drive startNetworkMonitoring() with room for exactly one more descriptor,
// so the netlink socket is opened and the stop eventfd cannot be. Running
// out of descriptors is the ordinary way this fails on a busy desktop, and
// the arm that handles it -- closing the socket again rather than leaking it
// -- had never run.
int monitorWithoutDescriptors()
{
    const int inUse = openDescriptors();
    if (inUse < 0) {
        std::printf("NOFD=skip\n");
        return 0;
    }

    struct rlimit saved{};
    if (getrlimit(RLIMIT_NOFILE, &saved) != 0) {
        std::printf("NOFD=skip\n");
        return 0;
    }

    // Counting /proc/self/fd overcounts: readdir yields "." and ".." as well
    // as the directory's own descriptor. Rather than guess the offset, lower
    // the limit until exactly one descriptor can still be had.
    bool tightened = false;
    for (int slack = 0; slack <= 6 && !tightened; ++slack) {
        struct rlimit tight = saved;
        tight.rlim_cur = static_cast<rlim_t>(inUse - slack);
        if (tight.rlim_cur > saved.rlim_max)
            continue;
        if (setrlimit(RLIMIT_NOFILE, &tight) != 0)
            continue;

        const int first = dup(0);
        const int second = dup(0);
        if (first >= 0 && second < 0)
            tightened = true;   // room for one, not two: what we want
        if (second >= 0) close(second);
        if (first >= 0) close(first);
    }

    if (!tightened) {
        setrlimit(RLIMIT_NOFILE, &saved);
        std::printf("NOFD=skip\n");
        return 0;
    }

    PlatformQuirks::startNetworkMonitoring([](bool) {});
    PlatformQuirks::stopNetworkMonitoring();

    setrlimit(RLIMIT_NOFILE, &saved);

    // Returning at all is the result: the failure arm must clean up and
    // return, not abort or leave the socket open.
    std::printf("NOFD=survived\n");
    return 0;
}

// Drive startNetworkMonitoring() with the thread limit already reached, so
// the netlink socket and the stop eventfd are both opened and the monitor
// thread cannot be started. The arm that handles it closes both again and
// clears the running flag; leaking either would cost a descriptor per
// attempt, and leaving the flag set would make a later stop hang.
//
// RLIMIT_NPROC is checked against the user's process count, but the limit
// itself is per-process: lowering it here cannot stop anything else on the
// machine from forking.
int monitorWithoutThreads()
{
    struct rlimit saved{};
    if (getrlimit(RLIMIT_NPROC, &saved) != 0) {
        std::printf("NOTHREAD=skip\n");
        return 0;
    }

    struct rlimit tight = saved;
    tight.rlim_cur = 1;   // already exceeded, so any new thread is refused
    if (setrlimit(RLIMIT_NPROC, &tight) != 0) {
        std::printf("NOTHREAD=skip\n");
        return 0;
    }

    PlatformQuirks::startNetworkMonitoring([](bool) {});
    PlatformQuirks::stopNetworkMonitoring();

    setrlimit(RLIMIT_NPROC, &saved);
    std::printf("NOTHREAD=survived\n");
    return 0;
}

// openUrlExternally() falls back to running xdg-open when the desktop
// portal is not there, and fork refusing had never been exercised.
//
// The exec-failure arm below it is not reachable from here: emptying PATH
// does not hide xdg-open, because QStandardPaths::findExecutable falls back
// to a built-in default and finds /usr/bin/xdg-open anyway -- the same trap
// the connectivity cases document.
int openUrlWithoutFork()
{
    // Somewhere that is definitely not a bus. Without this, and now that the
    // probe links Qt6::DBus, sessionBus() tries to autolaunch a daemon --
    // which forks, and forking is the very thing being denied below, so the
    // probe hangs instead of reaching launchDetached.
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-rpi-imager-bus", 1);
    setenv("BROWSER", "/bin/true", 1);

    // One call before the limit drops, so QtDBus has already made whatever
    // threads it wants. Denying it those is not the point here and it does
    // not fail gracefully: the probe hangs rather than returning.
    PlatformQuirks::openUrlExternally(QUrl(QStringLiteral("https://example.invalid/")));

    struct rlimit saved{};
    if (getrlimit(RLIMIT_NPROC, &saved) != 0) {
        std::printf("NOFORK=skip\n");
        return 0;
    }
    struct rlimit tight = saved;
    tight.rlim_cur = 1;
    if (setrlimit(RLIMIT_NPROC, &tight) != 0) {
        std::printf("NOFORK=skip\n");
        return 0;
    }

    const bool ok = PlatformQuirks::openUrlExternally(
        QUrl(QStringLiteral("https://example.invalid/")));

    setrlimit(RLIMIT_NPROC, &saved);
    std::printf("NOFORK=%d\n", ok ? 1 : 0);
    return 0;
}

// openUrlExternally() prefers the xdg-desktop-portal OpenURI interface and
// only falls back to xdg-open when that fails. None of the portal attempt
// had ever run, because the suite has no session bus: without one the
// function returns before building the call at all.
//
// Run under dbus-run-session there is a bus but no portal on it, so the
// call is built, sent, and comes back an error -- which is the arm that
// matters, since a desktop without the portal installed is the common case.
int openUrlViaPortalAttempt()
{
    // The portal attempt is the point; the xdg-open fallback behind it is
    // not, and left alone it launches whatever browser the machine has.
    setenv("BROWSER", "/bin/true", 1);
    const bool ok = PlatformQuirks::openUrlExternally(
        QUrl(QStringLiteral("https://example.invalid/")));
    std::printf("PORTAL=%d\n", ok ? 1 : 0);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "portal") == 0) {
        const int rc = openUrlViaPortalAttempt();
        std::fflush(stdout);
        return rc;
    }
    if (argc > 1 && std::strcmp(argv[1], "nofork") == 0) {
        const int rc = openUrlWithoutFork();
        std::fflush(stdout);
        return rc;
    }
    if (argc > 1 && std::strcmp(argv[1], "nothread") == 0) {
        const int rc = monitorWithoutThreads();
        std::fflush(stdout);
        return rc;
    }
    if (argc > 1 && std::strcmp(argv[1], "nofd") == 0) {
        const int rc = monitorWithoutDescriptors();
        std::fflush(stdout);
        return rc;
    }
    if (argc > 1 && std::strcmp(argv[1], "ready") == 0) {
        const bool ready = PlatformQuirks::isNetworkReady();
        std::printf("READY=%d\n", ready ? 1 : 0);
    } else {
        const bool online = PlatformQuirks::hasNetworkConnectivity();
        std::printf("CONNECTIVITY=%d\n", online ? 1 : 0);
    }
    std::fflush(stdout);
    return 0;
}
