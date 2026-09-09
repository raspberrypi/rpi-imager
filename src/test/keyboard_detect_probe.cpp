/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for ImageWriter::detectPiKeyboard().
 *
 * On the Pi-booted (embedded) Imager this chooses the keyboard layout, from
 * either /proc/device-tree/chosen/rpi-country-code or the name of the device
 * node in /dev/input/by-id. Both paths are absolute, so the caller
 * bind-mounts a synthetic /dev/input over the real one inside an
 * unprivileged mount namespace and runs this inside -- the technique
 * embedded_scaling/run.sh uses for /sys/class/drm.
 *
 * It matters because the layout decides what the keys produce, and the first
 * thing anybody types on that keyboard is a Wi-Fi password. A wrong layout
 * means a board that cannot join the network, with nothing on screen
 * connecting the two.
 *
 * Prints KEYBOARD=<code>, empty when nothing was detected.
 */

#include "imagewriter.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    ImageWriter writer(nullptr);

    const QString keyboard = writer.detectPiKeyboard();
    std::printf("KEYBOARD=%s\n", keyboard.toUtf8().constData());
    std::fflush(stdout);
    return 0;
}
