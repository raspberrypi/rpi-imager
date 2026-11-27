// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// The root helper's only USB job: open a Raspberry Pi boot device for the
// unprivileged client, and nothing else. Run against a fake /dev and /sys,
// with a plain file standing in for the device node.

#include <catch2/catch_test_macros.hpp>

#include "writer/server/linux/usb_device_access.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;
namespace usb = rpi_imager::writer::usb_access;
namespace proto = rpi_imager::privileged::proto;

namespace {

bool plainFileIsUsb(const struct stat& st) {
    return S_ISREG(st.st_mode);
}

// A plain file's st_rdev is 0, so its sysfs entry is /sys/dev/char/0:0.
struct FakeBus {
    fs::path root;
    usb::Roots roots;

    FakeBus() {
        char tmpl[] = "/tmp/usb-access-test-XXXXXX";
        root = ::mkdtemp(tmpl);
        roots.dev = (root / "dev").string();
        roots.sys = (root / "sys").string();
        roots.isUsbNode = plainFileIsUsb;
    }
    ~FakeBus() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void device(unsigned bus, unsigned addr, const char* vid, const char* pid,
                const char* interface = nullptr) {
        char rel[16];
        std::snprintf(rel, sizeof(rel), "%03u/%03u", bus, addr);
        fs::create_directories((fs::path(roots.dev) / rel).parent_path());
        std::ofstream(fs::path(roots.dev) / rel) << "";
        const fs::path dir = fs::path(roots.sys) / "dev/char/0:0";
        fs::create_directories(dir);
        std::ofstream(dir / "idVendor") << vid << "\n";
        std::ofstream(dir / "idProduct") << pid << "\n";
        std::ofstream(dir / "busnum") << bus << "\n";
        std::ofstream(dir / "devnum") << addr << "\n";
        if (interface) {
            fs::create_directories(dir / "1-1:1.0");
            std::ofstream(dir / "1-1:1.0/interface") << interface << "\n";
        }
    }
};

usb::Opened open(const FakeBus& bus, unsigned b, unsigned a) {
    auto r = usb::openRaspberryPiUsbDevice(b, a, bus.roots);
    if (r.fd >= 0) {
        ::close(r.fd);
    }
    return r;
}

} // namespace

TEST_CASE("A Compute Module in USB boot mode is opened", "[usb][helper]")
{
    for (const char* pid : {"2763", "2764", "2711", "2712"}) {
        FakeBus bus;
        bus.device(1, 5, "0a5c", pid);
        const auto r = open(bus, 1, 5);
        CHECK(r.fd >= 0);
        CHECK(r.vendor_id == 0x0a5c);
    }
}

TEST_CASE("The Pi's fastboot gadget is opened", "[usb][helper]")
{
    FakeBus bus;
    bus.device(3, 7, "18d1", "4e40", "fastbootd-provisioner");
    const auto r = open(bus, 3, 7);
    CHECK(r.fd >= 0);
    CHECK(r.product_id == 0x4e40);
}

TEST_CASE("A phone on the gadget's VID and PID is refused", "[usb][helper]")
{
    FakeBus bus;
    bus.device(3, 7, "18d1", "4e40", "Android Fastboot");
    const auto r = open(bus, 3, 7);
    CHECK(r.fd < 0);
    CHECK(r.error.code() == proto::ERROR_DEVICE_PERMISSION);

    FakeBus no_interface;
    no_interface.device(3, 7, "18d1", "4e40");
    CHECK(open(no_interface, 3, 7).fd < 0);
}

TEST_CASE("Any other USB device is refused", "[usb][helper]")
{
    FakeBus bus;
    bus.device(1, 2, "046d", "c31c");
    const auto r = open(bus, 1, 2);
    CHECK(r.fd < 0);
    CHECK(r.error.code() == proto::ERROR_DEVICE_PERMISSION);

    FakeBus other_broadcom;
    other_broadcom.device(1, 2, "0a5c", "21e8");
    CHECK(open(other_broadcom, 1, 2).fd < 0);
}

TEST_CASE("A bus or address that cannot exist is refused before anything is opened",
          "[usb][helper]")
{
    FakeBus bus;
    for (auto [b, a] : {std::pair{0u, 1u}, {256u, 1u}, {1u, 0u}, {1u, 128u}}) {
        const auto r = open(bus, b, a);
        CHECK(r.fd < 0);
        CHECK(r.error.code() == proto::ERROR_DEVICE_PERMISSION);
    }
}

TEST_CASE("A device that is not there is reported as not found", "[usb][helper]")
{
    FakeBus bus;
    const auto r = open(bus, 1, 9);
    CHECK(r.fd < 0);
    CHECK(r.error.code() == proto::ERROR_DEVICE_NOT_FOUND);
}

TEST_CASE("A node whose sysfs entry names another bus or address is refused", "[usb][helper]")
{
    FakeBus bus;
    bus.device(1, 5, "0a5c", "2711");
    std::ofstream(fs::path(bus.roots.sys) / "dev/char/0:0/devnum") << "6\n";
    CHECK(open(bus, 1, 5).fd < 0);
}

TEST_CASE("A symlink in place of the node is not followed", "[usb][helper]")
{
    FakeBus bus;
    bus.device(1, 5, "0a5c", "2711");
    const fs::path node = fs::path(bus.roots.dev) / "001/005";
    fs::rename(node, fs::path(bus.roots.dev) / "001/real");
    fs::create_symlink("real", node);
    CHECK(open(bus, 1, 5).fd < 0);
}

TEST_CASE("Something that is not a USB device node is refused", "[usb][helper]")
{
    FakeBus bus;
    bus.device(1, 5, "0a5c", "2711");
    bus.roots.isUsbNode = nullptr;  // the real check: character device, major 189
    const auto r = open(bus, 1, 5);
    CHECK(r.fd < 0);
    CHECK(r.error.code() == proto::ERROR_DEVICE_PERMISSION);
}
