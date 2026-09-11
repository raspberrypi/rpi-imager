/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for rpi_imager::secureSettingsFile() running as root.
 *
 * On Linux the settings file is very often owned by root rather than by the
 * person using Imager: an elevated run creates it in the user's own home,
 * because applyQuirks() has repointed HOME there. secureSettingsFile hands
 * it back before narrowing it, and that handover needs a real root to
 * exercise -- so the caller runs this inside `unshare -r --map-auto`, where
 * the invoking account is uid 0 and a range of other uids is mapped as well.
 */

#include "settings_permissions.h"

#include <QCoreApplication>
#include <QFile>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <path> <ownerUid> <ownerGid>\n", argv[0]);
        return 2;
    }

    const QString path = QString::fromLocal8Bit(argv[1]);
    const int ownerUid = std::atoi(argv[2]);
    const int ownerGid = std::atoi(argv[3]);

    const char* mode = argc > 4 ? argv[4] : "";

    if (std::strcmp(mode, "own") == 0 || std::strcmp(mode, "ownenv") == 0) {
        const int changed = std::strcmp(mode, "ownenv") == 0
                                ? rpi_imager::restoreUserOwnership(path)
                                : rpi_imager::restoreUserOwnership(path, ownerUid, ownerGid);
        std::printf("EUID=%u\n", (unsigned)::geteuid());
        std::printf("CHANGED=%d\n", changed);
        std::fflush(stdout);
        return 0;
    }

    const bool fromEnvironment =
        std::strcmp(mode, "env") == 0 || std::strcmp(mode, "newenv") == 0;

    if (std::strcmp(mode, "newenv") != 0) {
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::fprintf(stderr, "could not create %s\n", argv[1]);
                return 3;
            }
            f.write("[imagecustomization]\nhostname=raspberrypi\n");
        }
        ::chmod(argv[1], 0664);
    }

    const rpi_imager::SettingsPermissions r =
        fromEnvironment ? rpi_imager::secureSettingsFile(path)
                        : rpi_imager::secureSettingsFile(path, ownerUid, ownerGid);

    struct stat st{};
    ::stat(argv[1], &st);

    std::printf("EUID=%u\n", (unsigned)::geteuid());
    std::printf("OWNER=%u\n", (unsigned)st.st_uid);
    std::printf("MODE=%o\n", (unsigned)(st.st_mode & 07777));
    std::printf("REOWNED=%d\n", r.reowned ? 1 : 0);
    std::printf("TIGHTENED=%d\n", r.tightened ? 1 : 0);
    std::printf("SECURED=%d\n", r.secured ? 1 : 0);
    std::printf("FOREIGN=%d\n", r.foreignOwner ? 1 : 0);
    std::printf("CREATED=%d\n", r.created ? 1 : 0);
    std::fflush(stdout);
    return 0;
}
