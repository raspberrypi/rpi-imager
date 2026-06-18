// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#pragma once

#include "wire/drive_descriptor_json.h"
#include "proto/imager.pb.h"
#include "drivelist/drivelist.h"

#include <vector>

namespace rpi_imager::writer {

namespace proto = rpi_imager::privileged::proto;

inline proto::DriveList buildDriveList(
    const std::vector<Drivelist::DeviceDescriptor>& devices) {
    proto::DriveList list;
    std::vector<Drivelist::DeviceDescriptor> rich;
    rich.reserve(devices.size());
    for (const auto& d : devices) {
        if (!d.error.empty()) {
            continue;
        }
        rich.push_back(d);
        auto* info = list.add_drives();
        privileged::wire::populateDriveInfoSummary(d, *info);
    }
    list.set_rich_descriptors_json(privileged::wire::serializeDeviceDescriptors(rich));
    return list;
}

} // namespace rpi_imager::writer
