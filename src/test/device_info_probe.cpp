/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for DeviceInfo, the hardware detection the embedded (kiosk) build
 * uses to decide which images a board is offered.
 *
 * It reads the revision code out of /proc/cpuinfo, and that path is
 * hardcoded, so the only way to ask it about a board other than the one it
 * is running on is to give it a different /proc/cpuinfo. The driver runs
 * this under `unshare -rm` with one bind-mounted over the real thing.
 */

#include "device_info.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>
#include <string>

int main(int argc, char *argv[])
{
    DeviceInfo info;
    info.determineHardware();

    std::printf("PI=%d\n", info.isRaspberryPi() ? 1 : 0);
    std::printf("NAME=%s\n", info.hardwareName().toUtf8().constData());
    std::printf("REVISION=%s\n", info.revision().toUtf8().constData());

    // With a device list, the tags for the detected board are what filter
    // the OS list. Passed as JSON so the driver decides what the list says.
    if (argc > 1) {
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(argv[1]));
        info.setHardwareTags(doc.array());
        std::printf("TAGS_SET=%d\n", info.hardwareTagsSet() ? 1 : 0);
        std::printf("TAGS=%s\n",
                    QJsonDocument(info.getHardwareTags())
                        .toJson(QJsonDocument::Compact).constData());
    }

    std::fflush(stdout);
    return 0;
}
