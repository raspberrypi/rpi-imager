/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Making a volume lock succeed, and a flush fail.
 *
 * WinFile locks a volume before writing to it and syncs after. Neither
 * outcome can be arranged on a scratch file: FSCTL_LOCK_VOLUME is refused on
 * anything that is not a volume, and FlushFileBuffers on a small file does
 * not fail. So the code that runs when a lock is granted, and the refusal
 * when a sync cannot be completed, had never been reached.
 *
 * Only works in a target linked with
 *   -Wl,--wrap=__imp_DeviceIoControl -Wl,--wrap=__imp_FlushFileBuffers
 * and with winfile_fault.cpp. Both wraps apply to everything linked into
 * that target, so it wants a binary of its own.
 */

#ifndef RPI_TEST_WINFILE_FAULT_H
#define RPI_TEST_WINFILE_FAULT_H

namespace rpi_test {

// Answer the volume lock and unlock controls as though they were granted,
// rather than passing them to a handle that will refuse them. Every other
// control code goes through untouched.
void grantVolumeLocks(bool granted);

// Refuse every FlushFileBuffers, as a device that has gone does.
void failFlushes(bool failing);

}  // namespace rpi_test

#endif  // RPI_TEST_WINFILE_FAULT_H
