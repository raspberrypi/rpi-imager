/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 *
 * FreeBSD drive enumeration.
 */

#include "drivelist.h"
#include "embedded_config.h"

#include <fcntl.h>
#include <sys/mount.h>
#include <camlib.h>
#include <libgeom.h>
#include <sys/sysctl.h>
#include <algorithm>
#include <array>
#include <unordered_set>
#include <iostream>
#include <vector>
#include <optional>
#include <cassert>
#include <QString>
#include <QDebug>
#include <QStringList>

namespace Drivelist {

// ============================================================================
// Public API
// ============================================================================

std::vector<DeviceDescriptor> ListStorageDevices()
{
    std::vector<DeviceDescriptor> deviceList;
    return deviceList;
}

} // namespace Drivelist
