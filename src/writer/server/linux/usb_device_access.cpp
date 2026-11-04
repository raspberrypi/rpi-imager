// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "usb_device_access.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace rpi_imager::writer::usb_access {

namespace {

constexpr unsigned kUsbDeviceMajor = 189;

// The devices 99-rpiboot.rules grants; keep the two lists the same.
constexpr std::uint16_t kBroadcomVid = 0x0a5c;
constexpr std::array<std::uint16_t, 4> kBootRomPids = {0x2763, 0x2764, 0x2711, 0x2712};
constexpr std::uint16_t kFastbootVid = 0x18d1;
constexpr std::uint16_t kFastbootPid = 0x4e40;
constexpr char kFastbootInterface[] = "fastbootd-provisioner";

proto::ErrorInfo error(proto::ErrorCode code, const std::string& detail, int err = 0) {
    proto::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    e.set_kernel_errno(err);
    return e;
}

bool defaultIsUsbNode(const struct stat& st) {
    return S_ISCHR(st.st_mode) && ::major(st.st_rdev) == kUsbDeviceMajor;
}

bool readSmallFile(const std::string& path, std::string& out) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    char buf[256];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (n <= 0) {
        return false;
    }
    out.assign(buf, static_cast<std::size_t>(n));
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\0')) {
        out.pop_back();
    }
    return true;
}

bool readNumber(const std::string& path, int base, unsigned long max, unsigned long& out) {
    std::string text;
    if (!readSmallFile(path, text) || text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    out = std::strtoul(text.c_str(), &end, base);
    return errno == 0 && end && *end == '\0' && out <= max;
}

// The gadget borrows Google's VID/PID, so an Android phone can match it; the
// interface string is what names the Pi.
bool hasFastbootInterface(const std::string& device_dir) {
    DIR* d = ::opendir(device_dir.c_str());
    if (!d) {
        return false;
    }
    bool found = false;
    while (const dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.find(':') == std::string::npos) {
            continue;
        }
        std::string iface;
        if (readSmallFile(device_dir + "/" + name + "/interface", iface)
            && iface == kFastbootInterface) {
            found = true;
            break;
        }
    }
    ::closedir(d);
    return found;
}

bool allowed(std::uint16_t vid, std::uint16_t pid, const std::string& device_dir) {
    if (vid == kBroadcomVid) {
        for (const auto p : kBootRomPids) {
            if (pid == p) {
                return true;
            }
        }
        return false;
    }
    return vid == kFastbootVid && pid == kFastbootPid && hasFastbootInterface(device_dir);
}

} // namespace

Opened openRaspberryPiUsbDevice(std::uint32_t bus, std::uint32_t address, const Roots& roots) {
    Opened out;
    if (bus < 1 || bus > 255 || address < 1 || address > 127) {
        out.error = error(proto::ERROR_DEVICE_PERMISSION, "usb: bus or address out of range");
        return out;
    }

    char rel[16];
    std::snprintf(rel, sizeof(rel), "/%03u/%03u", bus, address);
    const int fd = ::open((roots.dev + rel).c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NOCTTY);
    if (fd < 0) {
        out.error = error(errno == ENOENT ? proto::ERROR_DEVICE_NOT_FOUND
                                          : proto::ERROR_DEVICE_PERMISSION,
                          "usb: cannot open device node", errno);
        return out;
    }

    // Everything below is read for the node already open, through its
    // device number, so a path swapped after the open changes nothing.
    struct stat st {};
    const auto isUsbNode = roots.isUsbNode ? roots.isUsbNode : defaultIsUsbNode;
    if (::fstat(fd, &st) != 0 || !isUsbNode(st)) {
        ::close(fd);
        out.error = error(proto::ERROR_DEVICE_PERMISSION, "usb: not a USB device node");
        return out;
    }

    const std::string device_dir = roots.sys + "/dev/char/" + std::to_string(::major(st.st_rdev))
                                   + ":" + std::to_string(::minor(st.st_rdev));
    unsigned long vid = 0, pid = 0, busnum = 0, devnum = 0;
    if (!readNumber(device_dir + "/idVendor", 16, 0xffff, vid)
        || !readNumber(device_dir + "/idProduct", 16, 0xffff, pid)
        || !readNumber(device_dir + "/busnum", 10, 255, busnum)
        || !readNumber(device_dir + "/devnum", 10, 127, devnum)
        || busnum != bus || devnum != address) {
        ::close(fd);
        out.error = error(proto::ERROR_DEVICE_NOT_FOUND, "usb: device changed or vanished");
        return out;
    }

    if (!allowed(static_cast<std::uint16_t>(vid), static_cast<std::uint16_t>(pid), device_dir)) {
        ::close(fd);
        out.error = error(proto::ERROR_DEVICE_PERMISSION, "usb: not a Raspberry Pi boot device");
        return out;
    }

    out.fd = fd;
    out.vendor_id = static_cast<std::uint16_t>(vid);
    out.product_id = static_cast<std::uint16_t>(pid);
    return out;
}

} // namespace rpi_imager::writer::usb_access
