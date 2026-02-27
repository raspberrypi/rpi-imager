/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Scans for Broadcom devices in USB boot mode and returns them as
 * DeviceDescriptor entries that can be merged into the drive list.
 */

#ifndef RPIBOOT_SCANNER_H
#define RPIBOOT_SCANNER_H

#include "rpiboot_types.h"
#include "../drivelist/drivelist.h"

#include <vector>

namespace rpiboot {

// Scan the USB bus for Broadcom boot-mode devices and return them
// as DeviceDescriptor structs suitable for the drive list model.
std::vector<Drivelist::DeviceDescriptor> scanRpibootDevices();

// Format a USB port path as a dotted string (e.g. "1.2.3")
std::string portPathToString(const std::vector<uint8_t>& portPath);

} // namespace rpiboot

#endif // RPIBOOT_SCANNER_H
