/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * A virtual hard disk, attached as a real physical drive.
 *
 * The Windows counterpart to loop_device.h, and for the same reason: most of
 * what the Windows backend does only happens against a physical drive.
 * \\.\PHYSICALDRIVE<n> is what DiskpartUtil is given, what WinFile locks and
 * dismounts, and what Drivelist enumerates -- and none of it can be reached
 * with a scratch file, so none of it was ever tested. An attached VHD is
 * indistinguishable from a disk at that layer: it has a physical path, a
 * partition table, volumes Windows mounts, and IOCTLs that behave.
 *
 * It is also the only safe way to test the destructive paths. cleanDisk()
 * wipes a partition table; pointed at a VHD it wipes one belonging to nobody.
 * Every case that calls a destructive function must pass this object's own
 * path() and nothing else -- see requirePathIsOurs() below, which is there to
 * make a copy-paste of a real drive path fail loudly rather than quietly.
 *
 * Attaching a VHD needs elevation, so unelevated runs get valid() == false and
 * a reason to print, exactly as LoopDevice does on a Linux host without
 * CAP_SYS_ADMIN.
 */
#ifndef RPI_TEST_VHD_DEVICE_H
#define RPI_TEST_VHD_DEVICE_H

#ifdef _WIN32

#include <QDir>
#include <QFile>
#include <QString>
#include <QUuid>

#include <windows.h>
#include <virtdisk.h>

namespace rpi_test {

class VhdDevice {
public:
    // Fixed rather than dynamically expanding: the write paths under test care
    // about the size the disk reports, and a dynamic VHD reports its maximum
    // while occupying almost nothing, which is the one way the two differ.
    explicit VhdDevice(quint64 megabytes = 64)
    {
        _file = QDir(QDir::tempPath()).filePath(
            QStringLiteral("rpi-imager-test-%1.vhd")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

        // Defined here rather than taken from virtdisk.h: DEFINE_GUID only
        // declares it unless INITGUID is set first, and setting that in a
        // header pulls every other GUID into whatever includes it.
        VIRTUAL_STORAGE_TYPE storageType{};
        storageType.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
        storageType.VendorId = GUID{0xEC984AEC, 0xA0F9, 0x47e9,
                                    {0x90, 0x1F, 0x71, 0x41, 0x5A, 0x66, 0x34, 0x5B}};

        CREATE_VIRTUAL_DISK_PARAMETERS params{};
        params.Version = CREATE_VIRTUAL_DISK_VERSION_1;
        params.Version1.UniqueId = GUID{};
        params.Version1.MaximumSize = megabytes * 1024ull * 1024ull;
        params.Version1.BlockSizeInBytes = 0;   // provider default
        params.Version1.SectorSizeInBytes = 0;  // provider default (512)
        params.Version1.ParentPath = nullptr;
        params.Version1.SourcePath = nullptr;

        const std::wstring path = _file.toStdWString();
        DWORD rc = ::CreateVirtualDisk(&storageType, path.c_str(),
                                       VIRTUAL_DISK_ACCESS_ALL, nullptr,
                                       CREATE_VIRTUAL_DISK_FLAG_FULL_PHYSICAL_ALLOCATION,
                                       0, &params, nullptr, &_handle);
        if (rc != ERROR_SUCCESS) {
            _reason = describe(QStringLiteral("CreateVirtualDisk"), rc);
            _handle = INVALID_HANDLE_VALUE;
            return;
        }

        // NO_DRIVE_LETTER keeps Explorer out of it. Without it Windows mounts
        // whatever volume the case just wrote and starts polling the device,
        // which both slows the run and raises "Please insert a disk" dialogs
        // the moment a case wipes the partition table underneath it.
        //
        // No PERMANENT_LIFETIME: the attachment is tied to this handle, so a
        // case that crashes or is killed leaves no VHD bound to the machine.
        rc = ::AttachVirtualDisk(_handle, nullptr,
                                 ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER,
                                 0, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) {
            _reason = describe(QStringLiteral("AttachVirtualDisk"), rc);
            ::CloseHandle(_handle);
            _handle = INVALID_HANDLE_VALUE;
            QFile::remove(_file);
            return;
        }
        _attached = true;

        wchar_t physical[MAX_PATH]{};
        ULONG bytes = sizeof(physical);
        rc = ::GetVirtualDiskPhysicalPath(_handle, &bytes, physical);
        if (rc != ERROR_SUCCESS) {
            _reason = describe(QStringLiteral("GetVirtualDiskPhysicalPath"), rc);
            return;
        }
        _path = QString::fromWCharArray(physical);
    }

    ~VhdDevice()
    {
        if (_attached)
            ::DetachVirtualDisk(_handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
        if (_handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(_handle);
        if (!_file.isEmpty())
            QFile::remove(_file);
    }

    VhdDevice(const VhdDevice&) = delete;
    VhdDevice& operator=(const VhdDevice&) = delete;

    bool valid() const { return !_path.isEmpty(); }

    // \\.\PhysicalDriveN, in the form DiskpartUtil and WinFile expect.
    QString path() const { return _path; }
    QByteArray pathBytes() const { return _path.toLatin1(); }

    // Why there is no device, for the SKIP message. A case that prints this
    // rather than "no VHD available" saves the next person working out whether
    // they forgot to elevate or the machine cannot do it at all.
    QString reason() const { return _reason; }

    // A destructive call must be aimed at this object and nothing else. Cheap
    // insurance against a path being pasted in from a bug report: the cases
    // below wipe partition tables, and the difference between this and a real
    // drive is one character.
    bool ownsPath(const QByteArray &device) const
    {
        return valid() && device == pathBytes();
    }

private:
    static QString describe(const QString &call, DWORD rc)
    {
        // 1314 is what AttachVirtualDisk actually returns unelevated, not the 5
        // one would expect: the call needs SeManageVolumePrivilege, and a
        // missing privilege is reported as such rather than as access denied.
        if (rc == ERROR_ACCESS_DENIED || rc == ERROR_PRIVILEGE_NOT_HELD)
            return call + QStringLiteral(" needs an elevated process (Windows "
                                         "error %1)").arg(rc);
        return call + QStringLiteral(" failed with Windows error %1").arg(rc);
    }

    HANDLE _handle = INVALID_HANDLE_VALUE;
    bool _attached = false;
    QString _file;
    QString _path;
    QString _reason;
};

} // namespace rpi_test

#endif // _WIN32

#endif // RPI_TEST_VHD_DEVICE_H
