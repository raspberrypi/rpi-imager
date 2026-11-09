// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "drivelist_linux_helper.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

namespace rpi_imager::writer::linux_drivelist {

namespace {

std::string runLsblk() {
    // Same columns as drivelist_linux.cpp, key=value format for easy parsing.
    const char* cmd =
        "lsblk -dn -b -P -o NAME,PATH,TYPE,SIZE,RM,RO,MODEL,VENDOR,TRAN,"
        "MOUNTPOINT,FSTYPE,SUBSYSTEMS 2>/dev/null";
    FILE* pipe = ::popen(cmd, "r");
    if (!pipe) {
        return {};
    }
    std::ostringstream out;
    std::array<char, 8192> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        out << buf.data();
    }
    ::pclose(pipe);
    return out.str();
}

std::optional<std::string> parseQuotedValue(const std::string& line,
                                            const char* key) {
    const std::string needle = std::string(key) + "=\"";
    const std::size_t pos = line.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    std::size_t start = pos + needle.size();
    std::string val;
    for (; start < line.size(); ++start) {
        const char c = line[start];
        if (c == '"') {
            return val;
        }
        if (c == '\\' && start + 1 < line.size()) {
            val += line[++start];
            continue;
        }
        val += c;
    }
    return std::nullopt;
}

bool parseBoolField(const std::string& line, const char* key) {
    const auto v = parseQuotedValue(line, key);
    return v && (*v == "1" || *v == "true");
}

std::uint64_t parseU64Field(const std::string& line, const char* key) {
    const auto v = parseQuotedValue(line, key);
    if (!v || v->empty()) {
        return 0;
    }
    std::uint64_t out = 0;
    for (char c : *v) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return 0;
        }
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return out;
}

std::optional<Drivelist::DeviceDescriptor> parseLsblkLine(const std::string& line) {
    const auto name = parseQuotedValue(line, "NAME");
    const auto path = parseQuotedValue(line, "PATH");
    if (!name || !path || name->empty() || path->empty()) {
        return std::nullopt;
    }

    if (name->rfind("sr", 0) == 0 || name->rfind("ram", 0) == 0 ||
        name->rfind("zram", 0) == 0) {
        return std::nullopt;
    }
    if (name->find("boot") != std::string::npos && name->find("mmcblk") != std::string::npos) {
        return std::nullopt;
    }

    const auto type = parseQuotedValue(line, "TYPE");
    if (type && *type != "disk") {
        return std::nullopt;
    }

    Drivelist::DeviceDescriptor device;
    device.device = *path;
    device.raw = *path;

    const auto subsystems = parseQuotedValue(line, "SUBSYSTEMS").value_or("block");
    device.isVirtual = name->rfind("loop", 0) == 0 || subsystems == "block";
    if (subsystems.find("usb") != std::string::npos ||
        subsystems.find("mmc") != std::string::npos) {
        device.isVirtual = false;
    }

    device.isReadOnly = parseBoolField(line, "RO");
    device.isRemovable = parseBoolField(line, "RM");
    device.size = parseU64Field(line, "SIZE");
    device.blockSize = 512;
    device.logicalBlockSize = 512;

    device.isCard = subsystems.find("mmc") != std::string::npos;
    device.isUSB = subsystems.find("usb") != std::string::npos;
    device.isSCSI = subsystems.find("scsi") != std::string::npos && !device.isUSB;
    if (device.isCard || device.isUSB) {
        device.isRemovable = true;
    }
    device.isSystem = !device.isRemovable && !device.isVirtual;

    const auto vendor = parseQuotedValue(line, "VENDOR").value_or("");
    const auto model = parseQuotedValue(line, "MODEL").value_or("");
    if (!vendor.empty() && !model.empty()) {
        device.description = vendor + " " + model;
    } else if (!model.empty()) {
        device.description = model;
    } else {
        device.description = *path;
    }

    const auto mount = parseQuotedValue(line, "MOUNTPOINT");
    if (mount && !mount->empty()) {
        device.mountpoints.push_back(*mount);
        const auto fstype = parseQuotedValue(line, "FSTYPE").value_or("");
        device.mountpointLabels.push_back(fstype);
        if (*mount == "/" || *mount == "/boot" || *mount == "/home" ||
            *mount == "/usr" || *mount == "/var" ||
            mount->rfind("/snap/", 0) == 0) {
            device.isSystem = true;
        }
    }

    const auto tran = parseQuotedValue(line, "TRAN");
    device.busType = tran.value_or(subsystems);

    return device;
}

} // namespace

std::vector<Drivelist::DeviceDescriptor> listStorageDevices() {
    std::vector<Drivelist::DeviceDescriptor> devices;
    const std::string output = runLsblk();
    if (output.empty()) {
        Drivelist::DeviceDescriptor err;
        err.error = "Failed to enumerate drives: lsblk command failed or timed out";
        devices.push_back(std::move(err));
        return devices;
    }

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        if (auto d = parseLsblkLine(line)) {
            devices.push_back(std::move(*d));
        }
    }
    return devices;
}

} // namespace rpi_imager::writer::linux_drivelist
