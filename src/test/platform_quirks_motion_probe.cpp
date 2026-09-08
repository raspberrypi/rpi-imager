/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for PlatformQuirks::prefersReducedMotion().
 *
 * The answer decides whether Imager animates its transitions. Somebody who
 * has turned animations off in their desktop's accessibility settings has
 * asked not to see them, and on some vestibular conditions the request is
 * not a preference.
 *
 * Three sources are consulted in turn: GNOME's enable-animations through
 * /usr/bin/gsettings, KDE's AnimationDurationFactor through
 * /usr/bin/kreadconfig6 or kreadconfig5, and ~/.config/gtk-3.0/settings.ini.
 * The first two are deliberately absolute -- this function can run as root
 * after pkexec, where a relative name would search a PATH the user controls
 * -- so they cannot be redirected by any means but a bind mount, and the
 * caller supplies one inside an unprivileged mount namespace.
 *
 * The GTK fallback follows HOME, which the caller points at an empty
 * directory so that only the source under test answers.
 *
 * Prints REDUCED=1 or REDUCED=0 on stdout.
 */

#include "platformquirks.h"

#include <cstdio>

int main()
{
    const bool reduced = PlatformQuirks::prefersReducedMotion();
    std::printf("REDUCED=%d\n", reduced ? 1 : 0);
    std::fflush(stdout);
    return 0;
}
