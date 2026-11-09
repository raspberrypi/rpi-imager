// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free DiskArbitration wrappers, used by the privileged helper.
// Mirrors the small set of operations PlatformQuirks::unmountDisk /
// ejectDisk expose on the unprivileged side, but without dragging Qt
// into the helper binary (see design doc §8a).

#pragma once

#include <cstdint>
#include <string>

namespace rpi_imager::writer::da {

enum class Result {
    Success,
    InvalidDrive,
    AccessDenied,
    Busy,
    Error,
};

// Retrieve the underlying DAReturn / kernel status code from the most
// recent operation on this thread. Returns 0 when the previous call
// was not a DA failure (e.g. invalid path that never reached DA).
std::int32_t lastKernelStatus();

// Unmount all volumes on the given /dev/diskN BSD device. `device_path`
// must include the /dev/ prefix; helper validates and rejects paths
// outside that namespace.
Result unmountDisk(const std::string& device_path);

// Eject the device after unmounting. Useful at end-of-write.
Result ejectDisk(const std::string& device_path);

// Human-readable detail for the last error on this thread; empty when
// the last operation succeeded.
std::string lastErrorDetail();

} // namespace rpi_imager::writer::da
