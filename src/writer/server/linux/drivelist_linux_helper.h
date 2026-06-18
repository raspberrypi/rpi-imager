// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Qt-free Linux drive enumeration for the privileged helper (lsblk -J).

#pragma once

#include "../../drivelist/drivelist.h"

#include <vector>

namespace rpi_imager::writer::linux_drivelist {

std::vector<Drivelist::DeviceDescriptor> listStorageDevices();

} // namespace rpi_imager::writer::linux_drivelist
