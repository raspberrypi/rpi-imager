/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifndef DISKPART_UTIL_H
#define DISKPART_UTIL_H

#include <QString>
#include <QByteArray>
#include <QObject>
#include <chrono>
#include <functional>
#include <vector>

namespace DiskpartUtil {

enum class VolumeHandling {
    SkipUnmounting,
    UnmountFirst
};

struct DiskpartResult {
    bool success;
    QString errorMessage;
};

/**
 * RAII holder for volume handles that have been locked (FSCTL_LOCK_VOLUME) and
 * dismounted (FSCTL_DISMOUNT_VOLUME) but deliberately kept OPEN.
 *
 * Holding the lock keeps Windows from re-mounting (and Explorer from re-grabbing)
 * the volume while we wipe the partition table and write the raw image to the
 * physical drive. This replaces the previous approach of calling
 * DeleteVolumeMountPoint, which permanently removed the drive-letter binding from
 * the Mount Manager and stranded card readers that Windows treats as fixed disks
 * (they came back with no drive letter). See issue #1665.
 *
 * When these handles are released (unlock + close), Windows re-mounts the volume
 * once the physical-drive handle is also closed, and — because the Mount Manager
 * binding was never deleted — the drive letter is reassigned normally.
 *
 * The handles are stored as void* so this header does not need <windows.h>.
 */
class LockedVolumes {
public:
    LockedVolumes() = default;
    ~LockedVolumes();

    LockedVolumes(LockedVolumes&& other) noexcept;
    LockedVolumes& operator=(LockedVolumes&& other) noexcept;
    LockedVolumes(const LockedVolumes&) = delete;
    LockedVolumes& operator=(const LockedVolumes&) = delete;

    /** Unlock and close every held handle. Safe to call more than once. */
    void release();

    /** Take ownership of a locked+dismounted, still-open volume handle. */
    void adopt(void* handle) { _handles.push_back(handle); }

    int count() const { return static_cast<int>(_handles.size()); }
    bool empty() const { return _handles.empty(); }

private:
    std::vector<void*> _handles;
};

/**
 * Timing callback for performance instrumentation
 * Parameters: eventName, durationMs, success
 */
using TimingCallback = std::function<void(const QString&, quint32, bool)>;

/**
 * Cleans a Windows physical drive using diskpart utility (legacy method)
 * 
 * @param device - Windows physical drive path (e.g., "\\\\.\\PHYSICALDRIVE0")
 * @param timeout - Timeout for diskpart operation (default: 60 seconds)
 * @param maxRetries - Maximum number of retry attempts (default: 3)
 * @param volumeHandling - How to handle mounted volumes before diskpart (default: UnmountFirst)
 * @return DiskpartResult with success status and error message if failed
 */
DiskpartResult cleanDisk(const QByteArray &device, std::chrono::milliseconds timeout = std::chrono::seconds(60), int maxRetries = 3, VolumeHandling volumeHandling = VolumeHandling::UnmountFirst);

/**
 * Cleans a Windows physical drive using direct IOCTL calls (faster method)
 * 
 * This method is significantly faster than diskpart because it:
 * 1. Avoids spawning an external process
 * 2. Uses direct DeviceIoControl calls for partition table removal
 * 3. Has reduced sleep times (adaptive rather than fixed)
 * 
 * @param device - Windows physical drive path (e.g., "\\\\.\\PHYSICALDRIVE0")
 * @param timingCallback - Optional callback for performance event reporting
 * @return DiskpartResult with success status and error message if failed
 */
DiskpartResult cleanDiskFast(const QByteArray &device, TimingCallback timingCallback = nullptr);

/**
 * Unmount and lock all volumes on a physical drive.
 *
 * Each volume is locked (FSCTL_LOCK_VOLUME) and dismounted (FSCTL_DISMOUNT_VOLUME),
 * then its handle is kept open and adopted into @p locked. The caller must keep
 * @p locked alive until the physical drive has been opened for writing; releasing
 * it (or letting it go out of scope) unlocks the volumes so Windows can re-mount
 * them and reassign their drive letters once the raw write completes.
 *
 * Unlike the previous implementation, this does NOT call DeleteVolumeMountPoint,
 * so the drive-letter binding survives the write. See issue #1665.
 *
 * @param device - Windows physical drive path (e.g., "\\\\.\\PHYSICALDRIVE0")
 * @param locked - Receives the locked+dismounted volume handles to hold open
 * @param timingCallback - Optional callback for performance event reporting
 * @return DiskpartResult with success status and error message if failed
 */
DiskpartResult unmountVolumes(const QByteArray &device, LockedVolumes &locked, TimingCallback timingCallback = nullptr);

/**
 * Force Windows to re-read the partition table and re-enumerate volumes on a
 * physical drive. Used to refresh the OS view of a disk after a raw write
 * (whether successful or aborted) so that drive letters get reassigned and
 * Explorer stops showing the disk as missing. Safe to call on a path that
 * isn't a Windows physical drive — it is a no-op in that case.
 *
 * @param device - Windows physical drive path (e.g., "\\\\.\\PHYSICALDRIVE0")
 * @param timingCallback - Optional callback for performance event reporting
 * @return DiskpartResult with success status and error message if failed
 */
DiskpartResult rescanDisk(const QByteArray &device, TimingCallback timingCallback = nullptr);

} // namespace DiskpartUtil

#endif // DISKPART_UTIL_H 