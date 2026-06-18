// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Serialize / deserialize Drivelist::DeviceDescriptor arrays as JSON for the
// Win/Linux wire helpers. Keys match the macOS helper's listDrivesNow
// NSDictionary entries (service_impl.mm).

#pragma once

#include "../../drivelist/drivelist.h"

#include <string>
#include <vector>

namespace rpi_imager::privileged::wire {

std::string serializeDeviceDescriptors(
    const std::vector<Drivelist::DeviceDescriptor>& devices);

std::vector<Drivelist::DeviceDescriptor> deserializeDeviceDescriptors(
    const std::string& json);

void populateDriveInfoSummary(const Drivelist::DeviceDescriptor& d,
                              rpi_imager::privileged::proto::DriveInfo& out);

std::vector<Drivelist::DeviceDescriptor> deviceDescriptorsFromDriveList(
    const rpi_imager::privileged::proto::DriveList& list);

std::string fingerprintDriveList(
    const rpi_imager::privileged::proto::DriveList& list);

} // namespace rpi_imager::privileged::wire
