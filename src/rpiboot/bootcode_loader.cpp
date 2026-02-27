/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootcode_loader.h"

#include <cstring>
#include <fstream>

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

    // Also try a .sig companion for signed-boot scenarios
    auto sigPath = std::filesystem::path(path).concat(".sig");

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

    // Send BootMessage header
    if (!sendBootMessage(transport, static_cast<int32_t>(bootcode.size()), cancelled))
        return false;

    if (cancelled.load())
        return false;

    // Send the bootcode payload
    if (!sendPayload(transport, bootcode, gen, cancelled))
        return false;

    return true;
}

bool BootcodeLoader::sendBootMessage(IUsbTransport& transport,
                                      int32_t payloadLength,
                                      const std::atomic<bool>& cancelled)
{
    if (cancelled.load())
        return false;

    BootMessage msg{};
    msg.length = payloadLength;
    // signature left zeroed (unsigned boot)

    // The protocol encodes the length in the control transfer's wValue/wIndex fields:
    //   wValue = length & 0xFFFF
    //   wIndex = length >> 16
    uint16_t wValue = static_cast<uint16_t>(payloadLength & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((payloadLength >> 16) & 0xFFFF);

    auto data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, data, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send BootMessage control transfer";
        return false;
    }

    return true;
}

bool BootcodeLoader::sendPayload(IUsbTransport& transport,
                                  const std::vector<uint8_t>& data,
                                  ChipGeneration gen,
                                  const std::atomic<bool>& cancelled)
{
    // Bulk OUT endpoint 1
    constexpr uint8_t EP_OUT = 0x01;

    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load())
            return false;

        size_t chunkSize = std::min(BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = std::span<const uint8_t>(data.data() + offset, chunkSize);

        int transferred = transport.bulkWrite(EP_OUT, chunk, DEFAULT_TIMEOUT_MS);
        if (transferred < 0) {
            _lastError = "Bulk write failed during bootcode upload at offset " + std::to_string(offset);
            return false;
        }
        offset += static_cast<size_t>(transferred);
    }

    // BCM2835 has a USB errata that sends a malformed return packet.
    // Read and discard it so it doesn't confuse later communication.
    if (gen == ChipGeneration::BCM2835) {
        uint8_t discard[512];
        auto buf = std::span<uint8_t>(discard);
        transport.bulkRead(EP_OUT | 0x80, buf, 500);
    }

    return true;
}

} // namespace rpiboot
