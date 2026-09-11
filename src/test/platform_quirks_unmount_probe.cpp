/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for PlatformQuirks::unmountDisk(), which is what takes a card's
 * partitions offline before the write opens the device.
 *
 * It calls umount(2) in the calling process, so the existing cases for it
 * skip unless the whole suite runs as root -- which it does not, and should
 * not. Inside `unshare -rm` this process is uid 0 over a mount namespace of
 * its own, which is enough: the mounts it makes and unmakes are invisible
 * outside and no real device is involved.
 */

#include "platformquirks.h"

#include <QString>

#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

const char *kRoot      = "/tmp/rpi-imager-unmount-probe";
const char *kDisk      = "/tmp/rpi-imager-unmount-probe/disk";
const char *kPartition = "/tmp/rpi-imager-unmount-probe/disk1";
const char *kMountDir  = "/tmp/rpi-imager-unmount-probe/mnt";

// Count the /proc/mounts entries whose source is our fake partition.
int mountsForPartition()
{
    FILE *f = std::fopen("/proc/mounts", "r");
    if (!f)
        return -1;
    int n = 0;
    char line[4096];
    while (std::fgets(line, sizeof(line), f))
        if (std::strncmp(line, kPartition, std::strlen(kPartition)) == 0
            && line[std::strlen(kPartition)] == ' ')
            ++n;
    std::fclose(f);
    return n;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "clean";

    ::mkdir(kRoot, 0700);
    ::mkdir(kMountDir, 0700);

    // The "disk" the caller names. A regular file: unmountDisk stats it and
    // refuses a directory, and asks nothing else of it.
    const int diskFd = ::open(kDisk, O_CREAT | O_RDWR, 0600);
    if (diskFd < 0) {
        std::fprintf(stderr, "could not create the fake disk\n");
        return 2;
    }
    ::close(diskFd);

    int held = -1;
    if (mode != "none") {
        if (::mount(kPartition, kMountDir, "tmpfs", 0, "size=1M") != 0) {
            std::fprintf(stderr, "could not mount the fake partition: %s\n", strerror(errno));
            return 2;
        }
        if (mode == "busy") {
            // An open file keeps the first three attempts from succeeding.
            const std::string busyFile = std::string(kMountDir) + "/held-open";
            held = ::open(busyFile.c_str(), O_CREAT | O_RDWR, 0600);
            if (held < 0) {
                std::fprintf(stderr, "could not hold a file open on the mount\n");
                return 2;
            }
        }
    }

    std::printf("MOUNTS_BEFORE=%d\n", mountsForPartition());
    // Reported so the caller can tell a cascade that had to work from one
    // whose first attempt simply succeeded.
    std::printf("HELD_OPEN=%d\n", held >= 0 ? 1 : 0);
    std::fflush(stdout);

    const auto result = PlatformQuirks::unmountDisk(QString::fromUtf8(kDisk));
    std::printf("RESULT=%d\n", static_cast<int>(result));
    std::printf("SUCCESS=%d\n", result == PlatformQuirks::DiskResult::Success ? 1 : 0);
    std::printf("MOUNTS_AFTER=%d\n", mountsForPartition());

    // A device path that is not there at all: reported rather than treated
    // as nothing to do, because writing to it would be next.
    const auto missing = PlatformQuirks::unmountDisk(
        QStringLiteral("/tmp/rpi-imager-unmount-probe/not-a-disk"));
    std::printf("MISSING_RESULT=%d\n", static_cast<int>(missing));

    // And a directory, which is the shape of a mistake in the caller.
    const auto dir = PlatformQuirks::unmountDisk(QString::fromUtf8(kMountDir));
    std::printf("DIRECTORY_RESULT=%d\n", static_cast<int>(dir));

    std::fflush(stdout);

    if (held >= 0)
        ::close(held);
    ::umount2(kMountDir, MNT_DETACH);
    return 0;
}
