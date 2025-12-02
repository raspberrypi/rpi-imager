/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "platformhelper.h"
#include "platformquirks.h"

bool PlatformHelper::hasNetworkConnectivity() const
{
    return PlatformQuirks::hasNetworkConnectivity();
}

void PlatformHelper::beep() const
{
    PlatformQuirks::beep();
}

bool PlatformHelper::isScrollInverted(bool qtInvertedFlag) const
{
    return PlatformQuirks::isScrollInverted(qtInvertedFlag);
}

