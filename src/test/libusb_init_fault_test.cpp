/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What the rpiboot scan does on a machine where libusb will not start.
 *
 * No USB support, or no permission to use it. The scan is called while the
 * drive list is being built, so an exception escaping here would take the
 * list with it -- the user would get no drives at all, on a machine whose
 * drives are fine, because a USB library they are not using did not load.
 *
 * libusb is linked statically here, so the symbol is the plain name and the
 * wrap target is a function rather than the pointer a DLL import needs.
 * Its own binary: the wrap applies to everything linked with it.
 */

#include <catch2/catch_test_macros.hpp>

#include "rpiboot/rpiboot_scanner.h"

#include <libusb.h>

namespace {
bool g_failInit = false;
}

extern "C" {

int __real_libusb_init(libusb_context **ctx);

int __wrap_libusb_init(libusb_context **ctx)
{
    if (g_failInit)
        return LIBUSB_ERROR_OTHER;
    return __real_libusb_init(ctx);
}

}  // extern "C"

TEST_CASE("A scan on a machine without libusb offers nothing, and does not throw",
          "[rpiboot][scanner][fault]")
{
    g_failInit = true;
    std::vector<Drivelist::DeviceDescriptor> devices;
    CHECK_NOTHROW(devices = rpiboot::scanRpibootDevices());
    CHECK(devices.empty());
    g_failInit = false;
}

TEST_CASE("A scan where libusb starts does not answer for the failure",
          "[rpiboot][scanner][fault]")
{
    // Guards the case above from passing because the scan always returns
    // nothing. With libusb working the call still has to complete without
    // throwing; what it finds depends on what is plugged in, so that is not
    // asserted.
    g_failInit = false;
    CHECK_NOTHROW((void)rpiboot::scanRpibootDevices());
}
