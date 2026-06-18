// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "helper_maintenance.h"

#include <cerrno>
#include <cstring>
#include <sys/mount.h>
#include <sys/stat.h>
#include <vector>

namespace rpi_imager::linux_maint {

namespace {

thread_local std::int32_t g_last_errno = 0;
thread_local std::string g_last_detail;

void setError(int err, const std::string& detail) {
    g_last_errno = err;
    g_last_detail = detail;
}

Result unmountImpl(const std::string& device) {
    struct stat stats {};
    if (::stat(device.c_str(), &stats) != 0) {
        setError(errno, "stat failed: " + device);
        return Result::InvalidDrive;
    }
    if (S_ISDIR(stats.st_mode)) {
        setError(0, "path is a directory, not a device: " + device);
        return Result::InvalidDrive;
    }

    std::vector<std::string> mountDirs;
    FILE* procMounts = ::setmntent("/proc/mounts", "r");
    if (!procMounts) {
        setError(errno, "could not read /proc/mounts");
        return Result::Error;
    }

    struct mntent data {};
    char mntBuf[4096 + 1024];
    while (struct mntent* mnt = ::getmntent_r(procMounts, &data, mntBuf, sizeof(mntBuf))) {
        const char* devicePath = device.c_str();
        const std::size_t devicePathLen = device.size();
        if (std::strncmp(mnt->mnt_fsname, devicePath, devicePathLen) != 0) {
            continue;
        }
        const char nextChar = mnt->mnt_fsname[devicePathLen];
        if (nextChar == '\0' ||
            (nextChar >= '0' && nextChar <= '9') ||
            (nextChar == 'p' && mnt->mnt_fsname[devicePathLen + 1] >= '0' &&
             mnt->mnt_fsname[devicePathLen + 1] <= '9')) {
            mountDirs.emplace_back(mnt->mnt_dir);
        }
    }
    ::endmntent(procMounts);

    if (mountDirs.empty()) {
        return Result::Success;
    }

    std::size_t unmountCount = 0;
    for (const std::string& mountDir : mountDirs) {
        const char* mountPath = mountDir.c_str();
        bool unmounted = false;
        if (::umount(mountPath) == 0 ||
            ::umount2(mountPath, MNT_EXPIRE) == 0 ||
            ::umount2(mountPath, MNT_DETACH) == 0 ||
            ::umount2(mountPath, MNT_FORCE) == 0) {
            unmounted = true;
        }
        if (unmounted) {
            ++unmountCount;
        } else {
            setError(errno, std::string("failed to unmount ") + mountPath);
        }
    }

    if (unmountCount == mountDirs.size()) {
        return Result::Success;
    }
    if (unmountCount == 0) {
        return Result::Busy;
    }
    return Result::Busy;
}

} // namespace

std::int32_t lastKernelErrno() {
    return g_last_errno;
}

const std::string& lastDetail() {
    return g_last_detail;
}

Result unmountDisk(const std::string& device_path) {
    g_last_errno = 0;
    g_last_detail.clear();
    return unmountImpl(device_path);
}

Result ejectDisk(const std::string& device_path) {
    return unmountDisk(device_path);
}

} // namespace rpi_imager::linux_maint
