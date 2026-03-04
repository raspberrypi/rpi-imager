/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "libusb_transport.h"
#include <libusb.h>
#include <cstring>
#include <stdexcept>
#include <QDebug>

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
        info.serialNumberIndex = desc.iSerialNumber;

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
    // Read the active config descriptor to determine the correct interface
    // and bulk endpoints, matching the upstream rpiboot Initialize_Device() logic.
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(libusb_get_device(_handle), &config) == LIBUSB_SUCCESS && config) {
        if (config->bNumInterfaces == 1) {
            _interface = 0;
            _outEp = 0x01;
            _inEp = 0x82;
        } else {
            _interface = 1;
            _outEp = 0x03;
            _inEp = 0x84;
        }
        _initDiag += QStringLiteral("ifaces=%1 if=%2 ep=0x%3; ")
            .arg(config->bNumInterfaces).arg(_interface).arg(_outEp, 2, 16, QLatin1Char('0'));
        qDebug() << "rpiboot: USB config numInterfaces=" << config->bNumInterfaces
                 << "interface=" << _interface
                 << "outEp=0x" << Qt::hex << (int)_outEp;
        libusb_free_config_descriptor(config);
    } else {
        _initDiag += QStringLiteral("ifaces=? (get_config failed); ");
        qDebug() << "rpiboot: get_active_config_descriptor failed; using defaults"
                 << "interface=" << _interface << "outEp=0x" << Qt::hex << (int)_outEp;
    }

    // Enable automatic kernel driver detach on Linux if a driver is attached
    // when claim_interface is called.  This is a no-op on macOS.
    libusb_set_auto_detach_kernel_driver(_handle, 1);

#ifdef __APPLE__
    // macOS IOKit only enables bulk transfers beyond the first Full Speed
    // packet (64 bytes) after USBSetConfiguration() is called by a user-space
    // process.  Root processes bypass this; our GUI app runs unprivileged so
    // we must call it explicitly.  On Linux this is unnecessary — the kernel
    // USB stack does not impose this restriction — and calling it causes an
    // avoidable bus reset (device already in config 1).
    // NOTE: do NOT call libusb_detach_kernel_driver before this; a failed
    // detach (LIBUSB_ERROR_ACCESS) corrupts IOKit state and causes
    // LIBUSB_ERROR_IO on subsequent bulk writes even when claim succeeds.
    int sc = libusb_set_configuration(_handle, 1);
    if (sc != LIBUSB_SUCCESS) {
        _initDiag += QStringLiteral("set_cfg=%1; ").arg(libusb_strerror(static_cast<libusb_error>(sc)));
        qDebug() << "rpiboot: set_configuration(1) failed:"
                 << libusb_strerror(static_cast<libusb_error>(sc));
    } else {
        _initDiag += QStringLiteral("set_cfg=OK; ");
        qDebug() << "rpiboot: set_configuration(1) OK";
    }
#endif

    int ci = libusb_claim_interface(_handle, _interface);
    if (ci == LIBUSB_SUCCESS) {
        _interfaceClaimed = true;
        _initDiag += QStringLiteral("claim=OK");
        qDebug() << "rpiboot: claim_interface" << _interface << "OK";
    } else {
        _initDiag += QStringLiteral("claim=%1").arg(libusb_strerror(static_cast<libusb_error>(ci)));
        qDebug() << "rpiboot: claim_interface" << _interface << "failed:"
                 << libusb_strerror(static_cast<libusb_error>(ci));
    }
}

LibusbTransport::~LibusbTransport()
{
    if (_handle) {
        if (_interfaceClaimed)
            libusb_release_interface(_handle, _interface);
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

    if (rc < 0)
        qDebug() << "controlTransfer OUT failed: req=0x" << Qt::hex << request
                 << "wValue=" << wValue << "wIndex=" << wIndex
                 << "->" << libusb_strerror(static_cast<libusb_error>(rc));

    return rc >= 0;
}

int LibusbTransport::controlTransferIn(uint8_t requestType, uint8_t request,
                                        uint16_t wValue, uint16_t wIndex,
                                        std::span<uint8_t> buffer,
                                        int timeoutMs)
{
    if (!_handle)
        return -1;

    int rc = libusb_control_transfer(
        _handle, requestType, request, wValue, wIndex,
        buffer.data(),
        static_cast<uint16_t>(buffer.size()),
        static_cast<unsigned int>(timeoutMs));

    return rc;  // libusb returns bytes transferred on success, negative on error
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

    if (rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_TIMEOUT) {
        if (transferred < static_cast<int>(data.size()))
            qDebug() << "rpiboot: bulkWrite partial: requested" << (int)data.size()
                     << "transferred" << transferred
                     << "(ep=0x" << Qt::hex << (int)endpoint << ")";
        return transferred;
    }

    // Return the negative libusb error code so callers can report it
    return rc;
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

    // Return the negative libusb error code so callers can report it
    // (matches bulkWrite behaviour — e.g. LIBUSB_ERROR_NO_DEVICE = -4)
    return rc;
}

bool LibusbTransport::isOpen() const
{
    return _handle != nullptr;
}

} // namespace rpiboot
