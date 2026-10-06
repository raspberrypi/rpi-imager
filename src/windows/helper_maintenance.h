// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free unmount/eject for the Windows privileged helper.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpi_imager::win_maint {

enum class Result {
    Success,
    InvalidDrive,
    AccessDenied,
    Busy,
    Error,
};

std::int32_t lastWin32Error();
const std::string& lastDetail();

Result unmountDisk(const std::string& device_path);
Result ejectDisk(const std::string& device_path);

// -1 unless device_path is \\.\PhysicalDriveN.
int physicalDriveNumber(const std::string& device_path);

// Dismounts every volume on the disk, then deletes its partition table.
// Volumes on fixed disks stay locked and open in `held` until released, so
// Windows cannot re-mount them before the drive is opened for writing;
// removable disks have their mount points deleted instead (#1665).
Result cleanDisk(const std::string& device_path, std::vector<void*>& held);
void releaseVolumes(std::vector<void*>& held);

// Re-reads the partition table and waits for the volumes to come online.
Result rescanDisk(const std::string& device_path);

} // namespace rpi_imager::win_maint
