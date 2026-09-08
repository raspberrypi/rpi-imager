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
 *
 * Both consult the audio tools by name through QStandardPaths and QProcess,
 * so PATH decides what is found -- but each answer is cached for the life of
 * the process with std::call_once, which is why this cannot be driven
 * in-process. One fresh process per case, with PATH pointed at a directory
 * of the caller's making.
 *
 * PATH is pointed at that directory rather than unset: QStandardPaths::
 * findExecutable falls back to a built-in default when PATH is empty and
 * would then find the machine's real tools.
 *
 * With "available" it prints AVAILABLE=0|1. Otherwise it calls beep() and
 * prints DONE=1; what actually ran is for the caller to see from the marks
 * its fake tools leave.
 */

#include "platformquirks.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "available") == 0) {
        std::printf("AVAILABLE=%d\n", PlatformQuirks::isBeepAvailable() ? 1 : 0);
    } else {
        PlatformQuirks::beep();
        std::printf("DONE=1\n");
    }
    std::fflush(stdout);
    return 0;
}
