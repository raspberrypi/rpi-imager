// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Which board a revision code names.
//
// The embedded build reads this out of /proc/cpuinfo at startup and filters
// the OS list by it, so a board is offered only images that will boot on it.
// Get it wrong one way and the list is empty; get it wrong the other and
// somebody is offered an image their board cannot run.
//
// Separated from the reading so it can be exercised against every code in
// the table, and against the ones that are not in it, on a machine that is
// not a Raspberry Pi.

#ifndef RPI_IMAGER_REVISION_CODE_H
#define RPI_IMAGER_REVISION_CODE_H

#include <QString>

#include <cstdint>
#include <unordered_map>

namespace rpi_imager {

// The device type a new-style revision code carries: eight bits at 4..11.
inline constexpr uint32_t deviceTypeOf(uint32_t revision)
{
    return (revision >> 4) & 0xFF;
}

// The board a device type names, or empty where it is not one we know.
//
// find(), not at(). The table stops at the newest board known when it was
// written and skips several codes in the middle, so at() throws
// std::out_of_range for the next Pi and for anything in a gap. Nothing
// catches it, and this runs during startup on the embedded build -- what a
// user meets on new hardware would be the imager not appearing at all.
inline QString hardwareNameForDeviceType(uint32_t deviceType)
{
    static const std::unordered_map<uint32_t, const char *> kBoards = {
        {0x00, "Raspberry Pi 1"},        // A
        {0x01, "Raspberry Pi 1"},        // B
        {0x02, "Raspberry Pi 1"},        // A+
        {0x03, "Raspberry Pi 1"},        // B+
        {0x04, "Raspberry Pi 2"},        // 2B
        {0x06, "Raspberry Pi 1"},        // CM1
        {0x08, "Raspberry Pi 3"},        // 3B
        {0x09, "Raspberry Pi Zero"},     // Zero
        {0x0a, "Raspberry Pi 3"},        // CM3
        {0x0c, "Raspberry Pi Zero"},     // Zero W
        {0x0d, "Raspberry Pi 3"},        // 3B+
        {0x0e, "Raspberry Pi 3"},        // 3A+
        {0x10, "Raspberry Pi 3"},        // CM3+
        {0x11, "Raspberry Pi 4"},        // 4B
        {0x12, "Raspberry Pi Zero 2 W"}, // Zero 2 W
        {0x13, "Raspberry Pi 4"},        // 400
        {0x14, "Raspberry Pi 4"},        // CM4
        {0x15, "Raspberry Pi 4"},        // CM4S
        {0x17, "Raspberry Pi 5"},        // 5B
        {0x18, "Raspberry Pi 5"},        // CM5
        {0x19, "Raspberry Pi 5"},        // 500
        {0x1a, "Raspberry Pi 5"},        // CM5 Lite
    };

    const auto found = kBoards.find(deviceType);
    if (found == kBoards.end())
        return {};
    return QString::fromLatin1(found->second);
}

// The board the revision field of /proc/cpuinfo names.
//
// `parsed` says whether the field was a hexadecimal number at all, which
// separates a line we could not read from a board we do not know: both give
// no name, and only the first means the format has changed under us.
inline QString hardwareNameForRevision(const QString &revision, bool *parsed = nullptr)
{
    bool ok = false;
    const uint32_t value = revision.toUInt(&ok, 16);
    if (parsed)
        *parsed = ok;
    if (!ok)
        return {};
    return hardwareNameForDeviceType(deviceTypeOf(value));
}

} // namespace rpi_imager

#endif // RPI_IMAGER_REVISION_CODE_H
