// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Fuzz the JSON that decides which drives are offered as a write target.
//
// lsblk is a system tool rather than an attacker, but it is still a
// separate program whose output this parses, and the field types move
// between versions -- "ro" and "rm" arrive as booleans on some and as the
// strings "0" and "1" on others, which the parser already handles both ways.
// A device that disappears mid-enumeration, a filesystem label holding
// anything a user can name a partition, an enclosure that reports a size in
// a form nobody expected: all of it arrives here.
//
// What is at stake is the destination list. A row that should not be there
// is a disk a user can pick and erase; a row that is missing is the card
// they came to write.
#include "drivelist/drivelist.h"
#include "fuzz_silence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Drivelist::testing {
std::vector<DeviceDescriptor> parseLinuxBlockDevices(const std::string &jsonOutput,
                                                     bool embeddedMode);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Larger than any real lsblk answer; past this only the JSON parser is
    // being exercised, and that is Qt's rather than ours.
    if (size > 256 * 1024)
        return 0;

    // The first byte chooses embedded mode, which changes which devices are
    // kept -- a panel build offers the internal storage a desktop hides.
    const bool embedded = size > 0 && (data[0] & 1);
    const std::string json(reinterpret_cast<const char *>(data), size);

    const std::vector<Drivelist::DeviceDescriptor> devices =
        Drivelist::testing::parseLinuxBlockDevices(json, embedded);

    for (const auto &d : devices) {
        // A row with no device path is a row nothing can be written to, and
        // the parser skips those deliberately: offering one would put an
        // entry in the list that cannot be selected or explained.
        if (d.device.empty())
            __builtin_trap();

        // Nothing is asserted about the size, and the first draft of this
        // file was wrong to. It trapped above 2^62 and found an lsblk
        // answer of 6402325708023257088 within a minute -- absurd as a
        // drive, but inside a uint64 and faithfully reported. The parser's
        // contract is to say what lsblk said, not to decide what is
        // plausible; the model drops a zero and the capacity check weighs
        // the rest. A bound here would have been this harness's opinion
        // rather than the product's promise.
        //
        // The same goes for mountpoint length: a path longer than anyone
        // would mount is still the path that was reported.
    }

    // Parsing the same bytes twice has to give the same answer. The parser
    // reads a JSON document and builds a list; anything that made it depend
    // on state left from a previous call would show as a drive list that
    // changes while nothing was plugged in or out.
    const std::vector<Drivelist::DeviceDescriptor> again =
        Drivelist::testing::parseLinuxBlockDevices(json, embedded);
    if (again.size() != devices.size())
        __builtin_trap();
    for (size_t i = 0; i < devices.size(); ++i)
        if (again[i].device != devices[i].device
            || again[i].size != devices[i].size)
            __builtin_trap();

    return 0;
}
