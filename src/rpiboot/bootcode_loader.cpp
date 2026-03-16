/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootcode_loader.h"

#include <QDebug>

#include <cstring>
#include <fstream>
#include <thread>

namespace rpiboot {

std::string BootcodeLoader::bootcodeFilename(ChipGeneration gen)
{
    switch (gen) {
    case ChipGeneration::BCM2711:   return "bootcode4.bin";
    case ChipGeneration::BCM2712:   return "bootcode5.bin";
    default:                        return "bootcode.bin";
    }
}

bool BootcodeLoader::send(IUsbTransport& transport,
                           ChipGeneration gen,
                           const std::filesystem::path& firmwareDir,
                           const std::atomic<bool>& cancelled)
{
    auto filename = bootcodeFilename(gen);
    auto path = firmwareDir / filename;

    // Read the bootcode binary
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        _lastError = "Cannot open bootcode file: " + path.string();
        return false;
    }

    auto fileSize = file.tellg();
    if (fileSize <= 0) {
        _lastError = "Bootcode file is empty: " + path.string();
        return false;
    }

    std::vector<uint8_t> bootcode(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bootcode.data()), fileSize);
    file.close();

    // Prepare BootMessage header
    BootMessage msg{};
    msg.length = static_cast<int32_t>(bootcode.size());
    // signature left zeroed — BCM2711/BCM2712 do not use BootMessage signatures
    // (signed boot on those chips is handled inside the bootcode binary itself)

    // Send BootMessage header via ep_write protocol
    auto msgSpan = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    if (!epWrite(transport, msgSpan, cancelled)) {
        _lastError = "Failed to send BootMessage: " + _lastError;
        return false;
    }

    if (cancelled.load())
        return false;

    // Send bootcode payload via ep_write protocol
    if (!epWrite(transport, bootcode, cancelled)) {
        _lastError = "Failed to send bootcode payload: " + _lastError;
        return false;
    }

    // Read the return value from the device via vendor control transfer IN.
    // This matches upstream second_stage_boot(): sleep(1) then ep_read().
    // The device may have already reset by the time we issue this read
    // (especially BCM2711/2712), in which case the transfer fails harmlessly.
    // For BCM2835, this also consumes the errata return packet.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    uint8_t retcode[4] = {};
    auto retBuf = std::span<uint8_t>(retcode, sizeof(retcode));
    transport.controlTransferIn(
        VENDOR_REQUEST_TYPE | 0x80, VENDOR_REQUEST,
        static_cast<uint16_t>(sizeof(retcode) & 0xFFFF), 0,
        retBuf, 20000);
    // Result intentionally ignored — failure is expected when device resets quickly

    return true;
}

bool BootcodeLoader::epWrite(IUsbTransport& transport,
                              std::span<const uint8_t> data,
                              const std::atomic<bool>& cancelled)
{
    if (cancelled.load())
        return false;

    // Step 1: Announce the payload length via a zero-data vendor control transfer
    // This matches the upstream rpiboot ep_write() protocol exactly.
    auto len = static_cast<uint32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(len & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((len >> 16) & 0xFFFF);

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, {}, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Control transfer failed";
        qDebug() << "rpiboot: epWrite control transfer failed: len=" << len
                 << "wValue=0x" << Qt::hex << wValue << "wIndex=0x" << wIndex;
        return false;
    }

    // Step 2: Send the actual data via bulk transfer in BULK_CHUNK_SIZE chunks
    const uint8_t EP_OUT = transport.outEndpoint();
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load())
            return false;

        size_t chunkSize = std::min(BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = std::span<const uint8_t>(data.data() + offset, chunkSize);

        int transferred = transport.bulkWrite(EP_OUT, chunk, DEFAULT_TIMEOUT_MS);
        if (transferred <= 0) {
            _lastError = "Bulk write stalled at offset " + std::to_string(offset)
                + " of " + std::to_string(data.size())
                + " (libusb error " + std::to_string(transferred) + ")";
            qDebug() << "rpiboot:" << _lastError.c_str()
                     << "ep=0x" << Qt::hex << (int)EP_OUT;
            return false;
        }
        offset += static_cast<size_t>(transferred);
    }

    return true;
}

} // namespace rpiboot
