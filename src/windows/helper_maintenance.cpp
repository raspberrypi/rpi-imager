// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "helper_maintenance.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>

namespace rpi_imager::win_maint {

namespace {

thread_local std::int32_t g_last_error = 0;
thread_local std::string g_last_detail;

void setError(DWORD err, const std::string& detail) {
    g_last_error = static_cast<std::int32_t>(err);
    g_last_detail = detail;
}

int parseDeviceNumber(const std::string& device) {
    int deviceId = -1;
    if (std::sscanf(device.c_str(), "\\\\.\\PhysicalDrive%d", &deviceId) == 1) {
        return deviceId;
    }
    if (std::sscanf(device.c_str(), "//./PhysicalDrive%d", &deviceId) == 1) {
        return deviceId;
    }
    char lower[256] = {0};
    const std::size_t n = std::min(device.size(), sizeof(lower) - 1);
    for (std::size_t i = 0; i < n; ++i) {
        lower[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(device[i])));
    }
    if (std::sscanf(lower, "\\\\.\\physicaldrive%d", &deviceId) == 1) {
        return deviceId;
    }
    return -1;
}

ULONG deviceNumberFromHandle(HANDLE volume) {
    STORAGE_DEVICE_NUMBER storageDeviceNumber {};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(volume, IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         nullptr, 0, &storageDeviceNumber,
                         sizeof(storageDeviceNumber), &bytesReturned, nullptr)) {
        return ULONG_MAX;
    }
    return storageDeviceNumber.DeviceNumber;
}

bool lockVolume(HANDLE volume) {
    DWORD bytesReturned = 0;
    for (int tries = 0; tries < 20; ++tries) {
        if (DeviceIoControl(volume, FSCTL_LOCK_VOLUME,
                          nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
            return true;
        }
        Sleep(500);
    }
    return false;
}

bool unlockVolume(HANDLE volume) {
    DWORD bytesReturned = 0;
    return DeviceIoControl(volume, FSCTL_UNLOCK_VOLUME,
                         nullptr, 0, nullptr, 0, &bytesReturned, nullptr) != FALSE;
}

bool dismountVolume(HANDLE volume) {
    DWORD bytesReturned = 0;
    return DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME,
                          nullptr, 0, nullptr, 0, &bytesReturned, nullptr) != FALSE;
}

bool isVolumeMounted(HANDLE volume) {
    DWORD bytesReturned = 0;
    return DeviceIoControl(volume, FSCTL_IS_VOLUME_MOUNTED,
                          nullptr, 0, nullptr, 0, &bytesReturned, nullptr) != FALSE;
}

bool ejectMedia(HANDLE volume) {
    DWORD bytesReturned = 0;
    PREVENT_MEDIA_REMOVAL buffer {};
    buffer.PreventMediaRemoval = FALSE;
    DeviceIoControl(volume, IOCTL_STORAGE_MEDIA_REMOVAL,
                    &buffer, sizeof(buffer), nullptr, 0, &bytesReturned, nullptr);
    for (int tries = 0; tries < 5; ++tries) {
        if (tries > 0) {
            Sleep(500);
        }
        if (DeviceIoControl(volume, IOCTL_STORAGE_EJECT_MEDIA,
                            nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
            return true;
        }
    }
    return false;
}

Result processDriveLetter(TCHAR driveLetter, ULONG targetDeviceNumber, bool doEject) {
    wchar_t volumePath[8];
    swprintf(volumePath, 8, L"\\\\.\\%c:", driveLetter);

    HANDLE volume = CreateFileW(volumePath, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) {
        return Result::Success;
    }

    const ULONG volumeDeviceNumber = deviceNumberFromHandle(volume);
    if (volumeDeviceNumber != targetDeviceNumber) {
        CloseHandle(volume);
        return Result::Success;
    }

    if (!isVolumeMounted(volume)) {
        CloseHandle(volume);
        return Result::Success;
    }

    if (!lockVolume(volume)) {
        CloseHandle(volume);
        setError(GetLastError(), "could not lock volume");
        return Result::Busy;
    }

    if (!dismountVolume(volume)) {
        unlockVolume(volume);
        CloseHandle(volume);
        setError(GetLastError(), "could not dismount volume");
        return Result::Error;
    }

    if (doEject) {
        (void)ejectMedia(volume);
    }

    unlockVolume(volume);
    CloseHandle(volume);
    return Result::Success;
}

Result processPhysicalDrive(const std::string& device, bool doEject) {
    const int deviceNumber = parseDeviceNumber(device);
    if (deviceNumber < 0) {
        setError(0, "invalid PhysicalDrive path");
        return Result::InvalidDrive;
    }

    DWORD drivesMask = GetLogicalDrives();
    if (drivesMask == 0) {
        setError(GetLastError(), "GetLogicalDrives failed");
        return Result::Error;
    }

    Result result = Result::Success;
    TCHAR driveLetter = L'A';
    while (drivesMask) {
        if (drivesMask & 1) {
            const Result letterResult =
                processDriveLetter(driveLetter, static_cast<ULONG>(deviceNumber), doEject);
            if (letterResult != Result::Success && result == Result::Success) {
                result = letterResult;
            }
        }
        ++driveLetter;
        drivesMask >>= 1;
    }
    return result;
}

} // namespace

std::int32_t lastWin32Error() {
    return g_last_error;
}

const std::string& lastDetail() {
    return g_last_detail;
}

Result unmountDisk(const std::string& device_path) {
    g_last_error = 0;
    g_last_detail.clear();
    return processPhysicalDrive(device_path, false);
}

Result ejectDisk(const std::string& device_path) {
    g_last_error = 0;
    g_last_detail.clear();
    return processPhysicalDrive(device_path, true);
}

} // namespace rpi_imager::win_maint
