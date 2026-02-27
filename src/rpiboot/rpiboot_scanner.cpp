/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "rpiboot_scanner.h"
#include "libusb_transport.h"

#include <sstream>

namespace rpiboot {

std::string portPathToString(const std::vector<uint8_t>& portPath)
{
    std::string result;
    for (size_t i = 0; i < portPath.size(); ++i) {
        if (i > 0) result += '.';
        result += std::to_string(portPath[i]);
    }
    return result;
}

std::vector<Drivelist::DeviceDescriptor> scanRpibootDevices()
{
    std::vector<Drivelist::DeviceDescriptor> result;

    try {
        LibusbContext ctx;
        auto usbDevices = ctx.scanBootDevices();

        for (const auto& dev : usbDevices) {
            Drivelist::DeviceDescriptor dd;

            // Synthetic device path for rpiboot devices
            std::string portStr = portPathToString(dev.portPath);
            dd.device = "rpiboot://" + std::to_string(dev.busNumber) + ":"
                      + std::to_string(dev.deviceAddress) + ":" + portStr;

            dd.description = deviceDescription(dev.chipGeneration);
            dd.size = 0;           // Not a block device yet
            dd.isRemovable = true;
            dd.isUSB = true;
            dd.isReadOnly = false;
            dd.isSystem = false;
            dd.isVirtual = false;
            dd.isCard = false;
            dd.isSCSI = false;
            dd.isRpiboot = true;
            dd.rpibootPid = dev.productId;
            dd.usbPortPath = dev.portPath;
            dd.rpibootChipName = std::string(chipGenerationName(dev.chipGeneration));

            result.push_back(std::move(dd));
        }
    } catch (...) {
        // If libusb initialization fails (no USB support, etc.), just return empty
    }

    return result;
}

} // namespace rpiboot
