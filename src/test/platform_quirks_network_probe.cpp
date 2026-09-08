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
 * Deliberately constructs no QCoreApplication: this runs from a background
 * thread in production and must not depend on one.
 */

#include "platformquirks.h"

#include <cstdio>

int main()
{
    const bool online = PlatformQuirks::hasNetworkConnectivity();
    std::printf("CONNECTIVITY=%d\n", online ? 1 : 0);
    std::fflush(stdout);
    return 0;
}
