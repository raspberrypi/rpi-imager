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

qreal PlatformHelper::textScaleFactor() const
{
    // Check for user override in settings first
    QSettings settings("Raspberry Pi", "Raspberry Pi Imager");
    QVariant override = settings.value("textScaleFactor");
    if (override.isValid()) {
        bool ok = false;
        qreal factor = override.toReal(&ok);
        if (ok && factor >= 0.5 && factor <= 3.0) {
            return factor;
        }
    }
    return PlatformQuirks::detectTextScaleFactor();
}

qreal PlatformHelper::fontDpiCorrection() const
{
    return PlatformQuirks::fontDpiCorrection();
}

bool PlatformHelper::prefersReducedMotion() const
{
    return PlatformQuirks::prefersReducedMotion();
}
