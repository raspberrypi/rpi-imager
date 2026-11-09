// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "helper_maintenance.h"
#include "virtual_disk.h"
#include "../drivelist/drivelist.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <shlobj.h>

// The Windows 8 SDK added this; MinGW's winioctl.h still lacks it.
#ifndef IOCTL_DISK_ARE_VOLUMES_READY
#define IOCTL_DISK_ARE_VOLUMES_READY \
    CTL_CODE(IOCTL_DISK_BASE, 0x0087, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

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

// `ejected` reports whether the medium actually left: a dismounted volume
// whose reader refuses the eject still returns Success.
Result processDriveLetter(TCHAR driveLetter, ULONG targetDeviceNumber, bool doEject,
                          bool& ejected) {
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

    if (doEject && ejectMedia(volume)) {
        ejected = true;
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
    bool ejected = false;
    TCHAR driveLetter = L'A';
    while (drivesMask) {
        if (drivesMask & 1) {
            const Result letterResult = processDriveLetter(
                driveLetter, static_cast<ULONG>(deviceNumber), doEject, ejected);
            if (letterResult != Result::Success && result == Result::Success) {
                result = letterResult;
            }
        }
        ++driveLetter;
        drivesMask >>= 1;
    }

    // Nothing ejected through a volume, as after a raw write: eject the
    // media via the physical drive, as PlatformQuirks::ejectDisk does.
    if (doEject && !ejected) {
        HANDLE drive = CreateFileA(device.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE) {
            setError(GetLastError(), "could not open physical drive to eject");
            return Result::Error;
        }
        ejected = ejectMedia(drive);
        if (!ejected) {
            setError(GetLastError(), "the device refused to eject");
        }
        CloseHandle(drive);
    }

    // A fixed reader refuses to eject. Its volumes are dismounted, but
    // reporting success would have the client tell the user it is safe to
    // pull while Windows still has the disk.
    if (doEject && result == Result::Success && !ejected) {
        return Result::Error;
    }
    return result;
}

void notifyShellDriveRemoved(wchar_t driveLetter) {
    wchar_t root[4] = {driveLetter, L':', L'\\', 0};
    SHChangeNotify(SHCNE_MEDIAREMOVED, SHCNF_PATHW, root, nullptr);
    SHChangeNotify(SHCNE_DRIVEREMOVED, SHCNF_PATHW, root, nullptr);
}

// Port of DiskpartUtil::unmountVolumes, which the non-elevated client can no
// longer run.
void dismountVolumesOf(int deviceNumber, std::vector<void*>& held) {
    for (const auto& dev : ::Drivelist::ListStorageDevices()) {
        if (parseDeviceNumber(dev.device) != deviceNumber) {
            continue;
        }
        for (const auto& mountpoint : dev.mountpoints) {
            if (mountpoint.size() < 2 || mountpoint[1] != ':') {
                continue;
            }
            const wchar_t letter = static_cast<wchar_t>(mountpoint[0]);
            notifyShellDriveRemoved(letter);

            wchar_t volumePath[8];
            swprintf(volumePath, 8, L"\\\\.\\%c:", letter);
            HANDLE volume = CreateFileW(volumePath, GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, 0, nullptr);
            if (volume == INVALID_HANDLE_VALUE) {
                continue;
            }

            // Geometric backoff: Windows 11 25H2+ may hold handles longer.
            DWORD bytesReturned = 0;
            for (int attempt = 0, delayMs = 100; attempt < 8; ++attempt, delayMs *= 2) {
                if (DeviceIoControl(volume, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0,
                                    &bytesReturned, nullptr)) {
                    break;
                }
                Sleep(static_cast<DWORD>(delayMs));
            }
            (void)dismountVolume(volume);

            if (dev.isRemovable) {
                unlockVolume(volume);
                CloseHandle(volume);
                wchar_t root[4] = {letter, L':', L'\\', 0};
                (void)DeleteVolumeMountPointW(root);
            } else {
                held.push_back(volume);
            }
            notifyShellDriveRemoved(letter);
            Sleep(100);
        }
        break;
    }
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

    // A virtual disk takes no media eject; detaching it is the eject.
    const int deviceNumber = parseDeviceNumber(device_path);
    if (deviceNumber >= 0) {
        const std::wstring backing =
            virtual_disk::backingFile(static_cast<unsigned long>(deviceNumber));
        if (!backing.empty()) {
            (void)processPhysicalDrive(device_path, false);
            DWORD error = 0;
            if (virtual_disk::detach(backing, error)) {
                return Result::Success;
            }
            setError(error, "could not detach the virtual disk");
            return Result::Error;
        }
    }
    return processPhysicalDrive(device_path, true);
}

int physicalDriveNumber(const std::string& device_path) {
    return parseDeviceNumber(device_path);
}

void releaseVolumes(std::vector<void*>& held) {
    for (void* h : held) {
        unlockVolume(static_cast<HANDLE>(h));
        CloseHandle(static_cast<HANDLE>(h));
    }
    held.clear();
}

// Port of DiskpartUtil::rescanDisk.
Result rescanDisk(const std::string& device_path) {
    g_last_error = 0;
    g_last_detail.clear();
    if (parseDeviceNumber(device_path) < 0) {
        setError(0, "invalid PhysicalDrive path");
        return Result::InvalidDrive;
    }
    HANDLE disk = CreateFileA(device_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        setError(GetLastError(), "could not open disk for rescan");
        return Result::Error;
    }
    DWORD bytesReturned = 0;
    DeviceIoControl(disk, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0,
                    &bytesReturned, nullptr);
    if (!DeviceIoControl(disk, IOCTL_DISK_ARE_VOLUMES_READY, nullptr, 0, nullptr, 0,
                         &bytesReturned, nullptr)) {
        Sleep(500);
    }
    CloseHandle(disk);
    SHChangeNotify(SHCNE_DRIVEADD, SHCNF_IDLIST, nullptr, nullptr);
    SHChangeNotify(SHCNE_MEDIAINSERTED, SHCNF_IDLIST, nullptr, nullptr);
    return Result::Success;
}

// Port of DiskpartUtil::cleanDiskFast.
Result cleanDisk(const std::string& device_path, std::vector<void*>& held) {
    g_last_error = 0;
    g_last_detail.clear();

    const int deviceNumber = parseDeviceNumber(device_path);
    if (deviceNumber < 0) {
        setError(0, "invalid PhysicalDrive path");
        return Result::InvalidDrive;
    }

    dismountVolumesOf(deviceNumber, held);

    HANDLE disk = CreateFileA(device_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        setError(err, "could not open disk for cleaning");
        return err == ERROR_ACCESS_DENIED ? Result::AccessDenied : Result::Error;
    }

    DWORD bytesReturned = 0;
    DeviceIoControl(disk, FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0, nullptr, 0,
                    &bytesReturned, nullptr);

    Result result = Result::Success;
    if (!DeviceIoControl(disk, IOCTL_DISK_DELETE_DRIVE_LAYOUT, nullptr, 0, nullptr, 0,
                         &bytesReturned, nullptr)) {
        const DWORD err = GetLastError();
        // Neither means a layout survived: there was no partition table.
        if (err != ERROR_INVALID_FUNCTION && err != ERROR_FILE_NOT_FOUND) {
            LARGE_INTEGER zero = {};
            SetFilePointerEx(disk, zero, nullptr, FILE_BEGIN);
            char emptyMbr[512] = {0};
            DWORD written = 0;
            if (!WriteFile(disk, emptyMbr, sizeof(emptyMbr), &written, nullptr)
                || written != sizeof(emptyMbr)) {
                setError(GetLastError(), "could not clear partition table");
                result = Result::Error;
            }
        }
    }
    CloseHandle(disk);
    return result;
}

} // namespace rpi_imager::win_maint
