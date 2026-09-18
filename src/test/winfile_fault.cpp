/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * See winfile_fault.h. A call into a DLL is an indirect call through
 * __imp_<name>, a data symbol holding the address, so what --wrap redirects
 * is a pointer; the real one arrives as __real___imp_<name>.
 */

#include "winfile_fault.h"

#include <windows.h>
#include <winioctl.h>

#include <atomic>

namespace {
std::atomic<bool> g_grantLocks{false};
std::atomic<bool> g_failFlushes{false};
}  // namespace

namespace rpi_test {

void grantVolumeLocks(bool granted) { g_grantLocks.store(granted); }
void failFlushes(bool failing) { g_failFlushes.store(failing); }

}  // namespace rpi_test

extern "C" {

using PfnDeviceIoControl = BOOL(WINAPI *)(HANDLE, DWORD, LPVOID, DWORD, LPVOID,
                                          DWORD, LPDWORD, LPOVERLAPPED);
using PfnFlushFileBuffers = BOOL(WINAPI *)(HANDLE);

extern PfnDeviceIoControl __real___imp_DeviceIoControl;
extern PfnFlushFileBuffers __real___imp_FlushFileBuffers;

PfnDeviceIoControl __wrap___imp_DeviceIoControl = nullptr;
PfnFlushFileBuffers __wrap___imp_FlushFileBuffers = nullptr;

static BOOL WINAPI interposedDeviceIoControl(HANDLE h, DWORD code, LPVOID inBuf,
                                             DWORD inSize, LPVOID outBuf,
                                             DWORD outSize, LPDWORD returned,
                                             LPOVERLAPPED overlapped)
{
    if (g_grantLocks.load() &&
        (code == FSCTL_LOCK_VOLUME || code == FSCTL_UNLOCK_VOLUME)) {
        // The caller reads the byte count, so it has to be set even though
        // neither control returns anything.
        if (returned)
            *returned = 0;
        return TRUE;
    }
    return __real___imp_DeviceIoControl(h, code, inBuf, inSize, outBuf, outSize,
                                        returned, overlapped);
}

static BOOL WINAPI interposedFlushFileBuffers(HANDLE h)
{
    if (g_failFlushes.load()) {
        // What a device that has been pulled out answers.
        ::SetLastError(ERROR_DEVICE_NOT_CONNECTED);
        return FALSE;
    }
    return __real___imp_FlushFileBuffers(h);
}

namespace {
struct Installer {
    Installer()
    {
        __wrap___imp_DeviceIoControl = interposedDeviceIoControl;
        __wrap___imp_FlushFileBuffers = interposedFlushFileBuffers;
    }
};
Installer g_installer;
}  // namespace

}  // extern "C"
