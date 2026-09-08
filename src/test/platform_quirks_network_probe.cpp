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
 *
 * With "ready" as argv[1] it answers PlatformQuirks::isNetworkReady()
 * instead, printing READY=1 or READY=0. That one adds a check that
 * systemd-timesyncd has set the clock, from two more hardcoded paths
 * (/lib/systemd/systemd-timesyncd and /var/lib/systemd/timesync/clock) which
 * the caller binds over in the same namespace.
 *
 * Deliberately constructs no QCoreApplication: this runs from a background
 * thread in production and must not depend on one.
 */

#include "platformquirks.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
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
