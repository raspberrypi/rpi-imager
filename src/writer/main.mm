// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// rpi-imager-writer: the privileged helper.
//
// Started by launchd in production (via SMAppService daemon registration;
// via a hand-installed plist for dev).
// Listens on a Mach service for NSXPC connections from the unprivileged
// rpi-imager client and performs privileged disk operations.

#include <Foundation/Foundation.h>

extern "C" int RpiImagerWriterServiceMain(int argc, const char* argv[]);

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        return RpiImagerWriterServiceMain(argc, argv);
    }
}
