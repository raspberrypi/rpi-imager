/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "libusb_transport.h"
#include <libusb.h>
#include <cstring>
#include <stdexcept>

namespace rpiboot {

// ── LibusbContext ──────────────────────────────────────────────────────

LibusbContext::LibusbContext()
{
    int rc = libusb_init(&_ctx);
    if (rc != LIBUSB_SUCCESS)
        throw std::runtime_error(std::string("libusb_init failed: ") + libusb_strerror(static_cast<libusb_error>(rc)));
}

LibusbContext::~LibusbContext()
{
    if (_ctx)
        libusb_exit(_ctx);
}

std::vector<UsbDeviceInfo> LibusbContext::scanBootDevices() const
{
    std::vector<UsbDeviceInfo> result;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(_ctx, &list);
    if (count < 0)
        return result;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0)
            continue;

        if (desc.idVendor != BROADCOM_VID)
            continue;

        auto gen = chipGenerationFromPid(desc.idProduct);
        if (!gen)
            continue;

        UsbDeviceInfo info;
        info.busNumber = libusb_get_bus_number(list[i]);
        info.deviceAddress = libusb_get_device_address(list[i]);
        info.vendorId = desc.idVendor;
        info.productId = desc.idProduct;
        info.chipGeneration = *gen;

        // Get USB port path for multi-device tracking
        uint8_t ports[7];
        int portCount = libusb_get_port_numbers(list[i], ports, sizeof(ports));
        if (portCount > 0)
            info.portPath.assign(ports, ports + portCount);

        result.push_back(std::move(info));
    }

    libusb_free_device_list(list, 1);
    return result;
}

// Fastboot VID/PID (Google's Android fastboot interface)
static constexpr uint16_t FASTBOOT_VID = 0x18d1;
static constexpr uint16_t FASTBOOT_PID = 0x4ee0;

std::vector<UsbDeviceInfo> LibusbContext::scanFastbootDevices() const
{
    std::vector<UsbDeviceInfo> result;

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(_ctx, &list);
    if (count < 0)
        return result;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0)
            continue;

        if (desc.idVendor != FASTBOOT_VID || desc.idProduct != FASTBOOT_PID)
            continue;

        UsbDeviceInfo info;
        info.busNumber = libusb_get_bus_number(list[i]);
        info.deviceAddress = libusb_get_device_address(list[i]);
        info.vendorId = desc.idVendor;
        info.productId = desc.idProduct;
        info.chipGeneration = ChipGeneration::BCM2711;  // Default; not meaningful for fastboot

        // Get USB port path for correlation with the original rpiboot device
        uint8_t ports[7];
        int portCount = libusb_get_port_numbers(list[i], ports, sizeof(ports));
        if (portCount > 0)
            info.portPath.assign(ports, ports + portCount);

        result.push_back(std::move(info));
    }

    libusb_free_device_list(list, 1);
    return result;
}

std::unique_ptr<LibusbTransport> LibusbContext::openDevice(const UsbDeviceInfo& info) const
{
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(_ctx, &list);
    if (count < 0)
        return nullptr;

    libusb_device_handle* handle = nullptr;

    for (ssize_t i = 0; i < count; ++i) {
        if (libusb_get_bus_number(list[i]) == info.busNumber &&
            libusb_get_device_address(list[i]) == info.deviceAddress) {

            int rc = libusb_open(list[i], &handle);
            if (rc != LIBUSB_SUCCESS)
                handle = nullptr;
            break;
        }
    }

    libusb_free_device_list(list, 1);

    if (!handle)
        return nullptr;

    return std::make_unique<LibusbTransport>(handle);
}

// ── LibusbTransport ────────────────────────────────────────────────────

LibusbTransport::LibusbTransport(libusb_device_handle* handle)
    : _handle(handle)
{
    // Detach any kernel driver on interface 0 so we can claim it
#ifdef LIBUSB_HAS_CAPABILITY
    if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
#endif
        libusb_detach_kernel_driver(_handle, 0);
#ifdef LIBUSB_HAS_CAPABILITY
    }
#endif

    int rc = libusb_claim_interface(_handle, 0);
    if (rc == LIBUSB_SUCCESS)
        _interfaceClaimed = true;
}

LibusbTransport::~LibusbTransport()
{
    if (_handle) {
        if (_interfaceClaimed)
            libusb_release_interface(_handle, 0);
        libusb_close(_handle);
    }
}

bool LibusbTransport::controlTransfer(uint8_t requestType, uint8_t request,
                                       uint16_t wValue, uint16_t wIndex,
                                       std::span<const uint8_t> data,
                                       int timeoutMs)
{
    if (!_handle)
        return false;

    int rc = libusb_control_transfer(
        _handle, requestType, request, wValue, wIndex,
        const_cast<uint8_t*>(data.data()),
        static_cast<uint16_t>(data.size()),
        static_cast<unsigned int>(timeoutMs));

    return rc >= 0;
}

int LibusbTransport::bulkWrite(uint8_t endpoint,
                                std::span<const uint8_t> data,
                                int timeoutMs)
{
    if (!_handle)
        return -1;

    int transferred = 0;
    int rc = libusb_bulk_transfer(
        _handle, endpoint,
        const_cast<uint8_t*>(data.data()),
        static_cast<int>(data.size()),
        &transferred,
        static_cast<unsigned int>(timeoutMs));

    if (rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_TIMEOUT)
        return transferred;
    return -1;
}

int LibusbTransport::bulkRead(uint8_t endpoint,
                               std::span<uint8_t> buffer,
                               int timeoutMs)
{
    if (!_handle)
        return -1;

    int transferred = 0;
    int rc = libusb_bulk_transfer(
        _handle, endpoint | LIBUSB_ENDPOINT_IN,
        buffer.data(),
        static_cast<int>(buffer.size()),
        &transferred,
        static_cast<unsigned int>(timeoutMs));

    if (rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_TIMEOUT)
        return transferred;
    return -1;
}

bool LibusbTransport::isOpen() const
{
    return _handle != nullptr;
}

} // namespace rpiboot
