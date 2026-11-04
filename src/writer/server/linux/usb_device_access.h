// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Opens a Raspberry Pi USB boot device for the unprivileged client, which
// then speaks rpiboot and fastboot over the descriptor itself. Root's part
// is limited to opening the node and proving what it is.

#pragma once

#include "proto/imager.pb.h"

#include <cstdint>
#include <string>
#include <sys/stat.h>

namespace rpi_imager::writer::usb_access {

namespace proto = rpi_imager::privileged::proto;

struct Roots {
    std::string dev = "/dev/bus/usb";
    std::string sys = "/sys";
    // Whether an opened node is a USB device; tests substitute plain files.
    bool (*isUsbNode)(const struct stat&) = nullptr;
};

struct Opened {
    int fd = -1;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    proto::ErrorInfo error;
};

// Opens /dev/bus/usb/BBB/DDD if, and only if, the device there is a
// Raspberry Pi boot ROM or the rpi-fastbootd gadget. The caller owns fd.
Opened openRaspberryPiUsbDevice(std::uint32_t bus, std::uint32_t address,
                                const Roots& roots = {});

} // namespace rpi_imager::writer::usb_access
