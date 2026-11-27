// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "drive_descriptor_json.h"

#include "proto/imager.pb.h"

#include <charconv>
#include <cctype>
#include <sstream>

namespace rpi_imager::privileged::wire {

namespace proto = rpi_imager::privileged::proto;

namespace {

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

void appendQuoted(std::ostringstream& os, const char* key, const std::string& val) {
    os << '"' << key << "\":\"" << jsonEscape(val) << '"';
}

void appendBool(std::ostringstream& os, const char* key, bool val) {
    os << '"' << key << "\":" << (val ? "true" : "false");
}

void appendU64(std::ostringstream& os, const char* key, std::uint64_t val) {
    os << '"' << key << "\":" << val;
}

void appendU32(std::ostringstream& os, const char* key, std::uint32_t val) {
    os << '"' << key << "\":" << val;
}

void appendStringArray(std::ostringstream& os, const char* key,
                       const std::vector<std::string>& vals) {
    os << '"' << key << "\":[";
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (i) os << ',';
        os << '"' << jsonEscape(vals[i]) << '"';
    }
    os << ']';
}

void skipWs(const std::string& s, std::size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
}

bool expectChar(const std::string& s, std::size_t& i, char c) {
    skipWs(s, i);
    if (i >= s.size() || s[i] != c) return false;
    ++i;
    return true;
}

bool parseString(const std::string& s, std::size_t& i, std::string& out) {
    skipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            const char e = s[i++];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += e; break;
            }
            continue;
        }
        out += c;
    }
    return false;
}

bool parseBool(const std::string& s, std::size_t& i, bool& out) {
    skipWs(s, i);
    if (s.compare(i, 4, "true") == 0) {
        i += 4;
        out = true;
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        i += 5;
        out = false;
        return true;
    }
    return false;
}

bool parseU64(const std::string& s, std::size_t& i, std::uint64_t& out) {
    skipWs(s, i);
    std::size_t start = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-')) {
        ++i;
    }
    if (start == i) return false;
    const auto r = std::from_chars(s.data() + start, s.data() + i, out);
    return r.ec == std::errc{};
}

bool parseU32(const std::string& s, std::size_t& i, std::uint32_t& out) {
    std::uint64_t tmp = 0;
    if (!parseU64(s, i, tmp)) return false;
    out = static_cast<std::uint32_t>(tmp);
    return true;
}

bool parseStringArray(const std::string& s, std::size_t& i,
                      std::vector<std::string>& out) {
    if (!expectChar(s, i, '[')) return false;
    out.clear();
    skipWs(s, i);
    if (expectChar(s, i, ']')) return true;
    for (;;) {
        std::string item;
        if (!parseString(s, i, item)) return false;
        out.push_back(std::move(item));
        skipWs(s, i);
        if (expectChar(s, i, ']')) return true;
        if (!expectChar(s, i, ',')) return false;
    }
}

bool parseObjectField(const std::string& s, std::size_t& i,
                      Drivelist::DeviceDescriptor& d) {
    std::string key;
    if (!parseString(s, i, key)) return false;
    if (!expectChar(s, i, ':')) return false;

    if (key == "device") return parseString(s, i, d.device);
    if (key == "raw") return parseString(s, i, d.raw);
    if (key == "description") return parseString(s, i, d.description);
    if (key == "busType") return parseString(s, i, d.busType);
    if (key == "busVersion") return parseString(s, i, d.busVersion);
    if (key == "enumerator") return parseString(s, i, d.enumerator);
    if (key == "devicePath") return parseString(s, i, d.devicePath);
    if (key == "parentDevice") return parseString(s, i, d.parentDevice);
    if (key == "mountpoints") return parseStringArray(s, i, d.mountpoints);
    if (key == "mountpointLabels") return parseStringArray(s, i, d.mountpointLabels);
    if (key == "childDevices") return parseStringArray(s, i, d.childDevices);
    if (key == "size") return parseU64(s, i, d.size);
    if (key == "blockSize") return parseU32(s, i, d.blockSize);
    if (key == "logicalBlockSize") return parseU32(s, i, d.logicalBlockSize);
    if (key == "isReadOnly") return parseBool(s, i, d.isReadOnly);
    if (key == "isSystem") return parseBool(s, i, d.isSystem);
    if (key == "isVirtual") return parseBool(s, i, d.isVirtual);
    if (key == "isRemovable") return parseBool(s, i, d.isRemovable);
    if (key == "isCard") return parseBool(s, i, d.isCard);
    if (key == "isSCSI") return parseBool(s, i, d.isSCSI);
    if (key == "isUSB") return parseBool(s, i, d.isUSB);
    if (key == "isUAS") return parseBool(s, i, d.isUAS);
    if (key == "busVersionNull") return parseBool(s, i, d.busVersionNull);
    if (key == "devicePathNull") return parseBool(s, i, d.devicePathNull);
    if (key == "isUASNull") return parseBool(s, i, d.isUASNull);

    // Unknown field: skip value (string, bool, number, array, object).
    skipWs(s, i);
    if (i >= s.size()) return false;
    if (s[i] == '"') {
        std::string dummy;
        return parseString(s, i, dummy);
    }
    if (s[i] == '[') {
        std::vector<std::string> dummy;
        return parseStringArray(s, i, dummy);
    }
    if (s[i] == '{') {
        ++i;
        skipWs(s, i);
        while (i < s.size() && s[i] != '}') {
            std::string k, v;
            if (!parseString(s, i, k) || !expectChar(s, i, ':')) return false;
            if (!parseString(s, i, v)) return false;
            skipWs(s, i);
            if (i < s.size() && s[i] == ',') ++i;
        }
        return expectChar(s, i, '}');
    }
    if (s.compare(i, 4, "true") == 0) {
        i += 4;
        return true;
    }
    if (s.compare(i, 5, "false") == 0) {
        i += 5;
        return true;
    }
    std::uint64_t n = 0;
    return parseU64(s, i, n);
}

bool parseObject(const std::string& s, std::size_t& i, Drivelist::DeviceDescriptor& d) {
    if (!expectChar(s, i, '{')) return false;
    skipWs(s, i);
    if (expectChar(s, i, '}')) return true;
    for (;;) {
        if (!parseObjectField(s, i, d)) return false;
        skipWs(s, i);
        if (expectChar(s, i, '}')) return true;
        if (!expectChar(s, i, ',')) return false;
    }
}

} // namespace

std::string serializeDeviceDescriptors(
    const std::vector<Drivelist::DeviceDescriptor>& devices) {
    std::ostringstream os;
    os << '[';
    bool first = true;
    for (const auto& d : devices) {
        if (!d.error.empty()) continue;
        if (!first) os << ',';
        first = false;
        os << '{';
        appendQuoted(os, "device", d.device); os << ',';
        appendQuoted(os, "raw", d.raw); os << ',';
        appendQuoted(os, "description", d.description); os << ',';
        appendU64(os, "size", d.size); os << ',';
        appendU32(os, "blockSize", d.blockSize); os << ',';
        appendU32(os, "logicalBlockSize", d.logicalBlockSize); os << ',';
        appendQuoted(os, "busType", d.busType); os << ',';
        appendQuoted(os, "busVersion", d.busVersion); os << ',';
        appendQuoted(os, "enumerator", d.enumerator); os << ',';
        appendQuoted(os, "devicePath", d.devicePath); os << ',';
        appendQuoted(os, "parentDevice", d.parentDevice); os << ',';
        appendStringArray(os, "mountpoints", d.mountpoints); os << ',';
        appendStringArray(os, "mountpointLabels", d.mountpointLabels); os << ',';
        appendStringArray(os, "childDevices", d.childDevices); os << ',';
        appendBool(os, "isReadOnly", d.isReadOnly); os << ',';
        appendBool(os, "isSystem", d.isSystem); os << ',';
        appendBool(os, "isVirtual", d.isVirtual); os << ',';
        appendBool(os, "isRemovable", d.isRemovable); os << ',';
        appendBool(os, "isCard", d.isCard); os << ',';
        appendBool(os, "isSCSI", d.isSCSI); os << ',';
        appendBool(os, "isUSB", d.isUSB); os << ',';
        appendBool(os, "isUAS", d.isUAS); os << ',';
        appendBool(os, "busVersionNull", d.busVersionNull); os << ',';
        appendBool(os, "devicePathNull", d.devicePathNull); os << ',';
        appendBool(os, "isUASNull", d.isUASNull);
        os << '}';
    }
    os << ']';
    return os.str();
}

std::vector<Drivelist::DeviceDescriptor> deserializeDeviceDescriptors(
    const std::string& json) {
    std::vector<Drivelist::DeviceDescriptor> out;
    if (json.empty()) return out;

    std::size_t i = 0;
    if (!expectChar(json, i, '[')) return out;
    skipWs(json, i);
    if (expectChar(json, i, ']')) return out;
    for (;;) {
        Drivelist::DeviceDescriptor d;
        if (!parseObject(json, i, d)) break;
        out.push_back(std::move(d));
        skipWs(json, i);
        if (expectChar(json, i, ']')) break;
        if (!expectChar(json, i, ',')) break;
    }
    return out;
}

void populateDriveInfoSummary(const Drivelist::DeviceDescriptor& d,
                              proto::DriveInfo& out) {
    out.set_device_path(d.device);
    out.set_display_name(d.description);
    out.set_size_bytes(d.size);
    out.set_is_removable(d.isRemovable);
    out.set_is_usb(d.isUSB);
    out.set_is_system_drive(d.isSystem);
    out.set_logical_block_size(d.logicalBlockSize);
    for (std::size_t i = 0; i < d.mountpoints.size(); ++i) {
        auto* mv = out.add_mounted_volumes();
        mv->set_mount_path(d.mountpoints[i]);
        if (i < d.mountpointLabels.size()) {
            mv->set_filesystem(d.mountpointLabels[i]);
        }
    }
}

std::vector<Drivelist::DeviceDescriptor> deviceDescriptorsFromDriveList(
    const proto::DriveList& list) {
    if (!list.rich_descriptors_json().empty()) {
        return deserializeDeviceDescriptors(list.rich_descriptors_json());
    }
    std::vector<Drivelist::DeviceDescriptor> out;
    out.reserve(list.drives_size());
    for (const auto& info : list.drives()) {
        Drivelist::DeviceDescriptor d;
        d.device = info.device_path();
        d.raw = info.device_path();
        d.description = info.display_name();
        d.size = info.size_bytes();
        d.isRemovable = info.is_removable();
        d.isUSB = info.is_usb();
        d.isSystem = info.is_system_drive();
        d.logicalBlockSize = info.logical_block_size();
        d.blockSize = info.logical_block_size() ? info.logical_block_size() : 512;
        for (const auto& mv : info.mounted_volumes()) {
            d.mountpoints.push_back(mv.mount_path());
            d.mountpointLabels.push_back(mv.filesystem());
        }
        out.push_back(std::move(d));
    }
    return out;
}

std::string fingerprintDriveList(const proto::DriveList& list) {
    std::ostringstream os;
    for (const auto& info : list.drives()) {
        os << info.device_path() << ':' << info.size_bytes() << ';';
    }
    return os.str();
}

} // namespace rpi_imager::privileged::wire
