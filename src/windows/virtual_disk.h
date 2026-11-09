// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Recognising and detaching an attached virtual disk (VHD/VHDX).
//
// A virtual disk is not removable media: IOCTL_STORAGE_EJECT_MEDIA has
// nothing to act on and fails. Detaching is what ejecting one means -- the
// volumes go, the drive stops being listed, and the file can be attached
// again. Both the in-process path and the elevated helper eject disks, and
// the helper links no Qt, so this is plain Win32 and header-only.
//
// Windows-only; callers link virtdisk.

#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <virtdisk.h>

namespace rpi_imager::virtual_disk {

// The file behind an attached virtual disk, or empty when the device is not
// one.
//
// Windows offers no way to ask a physical drive for its backing file; the
// dependency query is the documented route, and it answers only for a disk
// that has one, which is also how the device is recognised.
inline std::wstring backingFile(unsigned long deviceNumber) {
    const std::wstring physicalPath =
        L"\\\\.\\PhysicalDrive" + std::to_wstring(deviceNumber);
    HANDLE disk = CreateFileW(physicalPath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE)
        return {};

    // Typed enum rather than ULONG here, so the bitwise or has to be put back
    // into it explicitly.
    const GET_STORAGE_DEPENDENCY_FLAG flags = static_cast<GET_STORAGE_DEPENDENCY_FLAG>(
        GET_STORAGE_DEPENDENCY_FLAG_DISK_HANDLE | GET_STORAGE_DEPENDENCY_FLAG_HOST_VOLUMES);

    // Asked for its size first: the strings sit after the structure, so the
    // entry count alone does not give it.
    STORAGE_DEPENDENCY_INFO probe{};
    probe.Version = STORAGE_DEPENDENCY_INFO_VERSION_2;
    ULONG needed = 0;
    DWORD rc = GetStorageDependencyInformation(disk, flags, sizeof(probe), &probe, &needed);
    if (rc != ERROR_INSUFFICIENT_BUFFER || needed < sizeof(STORAGE_DEPENDENCY_INFO)) {
        CloseHandle(disk);
        return {};
    }

    std::vector<unsigned char> buffer(needed, 0);
    auto* info = reinterpret_cast<STORAGE_DEPENDENCY_INFO*>(buffer.data());
    info->Version = STORAGE_DEPENDENCY_INFO_VERSION_2;
    ULONG used = 0;
    rc = GetStorageDependencyInformation(disk, flags, needed, info, &used);
    CloseHandle(disk);
    if (rc != ERROR_SUCCESS || info->NumberEntries == 0)
        return {};

    const STORAGE_DEPENDENCY_INFO_TYPE_2& entry = info->Version2Entries[0];
    if (!entry.HostVolumeName || !entry.DependentVolumeRelativePath)
        return {};

    std::wstring host = entry.HostVolumeName;
    std::wstring relative = entry.DependentVolumeRelativePath;
    if (host.empty() || relative.empty())
        return {};

    // The volume name ends in a separator and the relative path begins with
    // one, so joining them unchanged gives a path that opens nothing.
    while (!host.empty() && host.back() == L'\\')
        host.pop_back();
    if (relative.front() != L'\\')
        relative.insert(relative.begin(), L'\\');
    return host + relative;
}

// Detaches the virtual disk backed by `file`. On failure, `error` holds the
// Win32 code from whichever step refused.
inline bool detach(const std::wstring& file, DWORD& error) {
    VIRTUAL_STORAGE_TYPE storageType{};
    storageType.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_UNKNOWN;
    storageType.VendorId = GUID{};  // VIRTUAL_STORAGE_TYPE_VENDOR_UNKNOWN

    OPEN_VIRTUAL_DISK_PARAMETERS params{};
    params.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    params.Version1.RWDepth = 1;  // OPEN_VIRTUAL_DISK_RW_DEPTH_DEFAULT

    HANDLE vhd = INVALID_HANDLE_VALUE;
    error = OpenVirtualDisk(&storageType, file.c_str(), VIRTUAL_DISK_ACCESS_DETACH,
                            OPEN_VIRTUAL_DISK_FLAG_NONE, &params, &vhd);
    if (error != ERROR_SUCCESS)
        return false;

    error = DetachVirtualDisk(vhd, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
    CloseHandle(vhd);
    return error == ERROR_SUCCESS;
}

} // namespace rpi_imager::virtual_disk
