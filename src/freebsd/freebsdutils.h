#ifndef FREEBSDUTILS_H
#define FREEBSDUTILS_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <unistd.h>

namespace FreeBSDUtils
{
    // Check if the current process is running as root (UID 0)
    inline bool isRunningAsRoot()
    {
        return ::geteuid() == 0;
    }
}

#endif // FREEBSDUTILS_H
