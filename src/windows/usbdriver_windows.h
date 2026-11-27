/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#ifndef USBDRIVER_WINDOWS_H
#define USBDRIVER_WINDOWS_H

#include <string>

namespace UsbDriverWindows {

enum class InstallResult { Installed, Declined, Failed };

// Runs pnputil elevated against the INF and blocks until it exits. On Failed,
// detail holds pnputil's exit code or the launch error.
InstallResult installRpibootDriver(const std::wstring &infPath, unsigned long &detail);

}

#endif // USBDRIVER_WINDOWS_H
