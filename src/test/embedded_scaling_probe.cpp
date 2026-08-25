/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Probe for the embedded display-scaling matrix (embedded_scaling/run.sh).
 *
 * Runs the real PlatformQuirks::applyEmbeddedDisplayScaling() against whatever
 * the caller has bind-mounted over /sys/class/drm, and prints the resulting
 * QT_SCALE_FACTOR on stdout -- "unset" when the function chose to leave it
 * alone. Its own diagnostics go to stderr, where the runner keeps them for
 * failing cases.
 *
 * Deliberately constructs no QCoreApplication: the production call site in
 * main.cpp runs before QGuiApplication exists, so the code under test must
 * work with no application object, and this keeps the probe honest about that.
 */

#include "platformquirks.h"

#include <QByteArray>
#include <QtGlobal>

#include <cstdio>

int main()
{
    PlatformQuirks::applyEmbeddedDisplayScaling();

    const QByteArray scale = qgetenv("QT_SCALE_FACTOR");
    std::printf("%s\n", scale.isEmpty() ? "unset" : scale.constData());
    return 0;
}
