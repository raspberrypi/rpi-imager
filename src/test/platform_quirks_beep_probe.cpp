/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for PlatformQuirks::isBeepAvailable() and beep().
 *
 * The chime is how a user who has looked away, or whose Imager window is
 * behind something else, learns that a write has finished. isBeepAvailable()
 * decides whether the "beep when finished" option is offered at all, and
 * beep() works down a list of ways to make a sound.
 */

#include "platformquirks.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "soundfile") == 0) {
        // Which sound file beep() would use. With /usr/share/sounds bound
        // over by an empty directory none of the system candidates is
        // readable, so the bundled chime is extracted instead -- the arm a
        // machine with no sound theme installed actually takes.
        const bool available = PlatformQuirks::isBeepAvailable();
        std::printf("AVAILABLE=%d\n", available ? 1 : 0);
        PlatformQuirks::beep();
        std::printf("DONE=1\n");
        std::fflush(stdout);
        return 0;
    }

    if (argc > 1 && std::strcmp(argv[1], "available") == 0) {
        std::printf("AVAILABLE=%d\n", PlatformQuirks::isBeepAvailable() ? 1 : 0);
    } else {
        PlatformQuirks::beep();
        std::printf("DONE=1\n");
    }
    std::fflush(stdout);
    return 0;
}
