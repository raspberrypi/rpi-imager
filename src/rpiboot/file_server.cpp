/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_server.h"

#include <cstring>
#include <fstream>

namespace rpiboot {

// Bulk endpoints used by the file server
constexpr uint8_t EP_OUT = 0x01;
constexpr uint8_t EP_IN  = 0x81;

bool FileServer::run(IUsbTransport& transport,
                      const std::filesystem::path& firmwareDir,
                      ProgressCallback progress,
                      const std::atomic<bool>& cancelled,
                      FileResolver resolver)
{
    // Use the disk-based resolver as default
    FileResolver resolverFn = resolver ? std::move(resolver)
        : [&firmwareDir](const std::string& name) {
              return readFileFromDisk(firmwareDir, name);
          };

    uint64_t filesServed = 0;

    while (!cancelled.load()) {
        // Read a FileMessage from the device
        FileMessage msg{};
        auto buf = std::span<uint8_t>(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));

        int bytesRead = transport.bulkRead(EP_IN, buf, 10000);
        if (bytesRead < static_cast<int>(sizeof(FileCommand))) {
            _lastError = "Failed to read FileMessage from device";
            return false;
        }

        std::string filename(msg.name());

        switch (msg.command) {
        case FileCommand::GetFileSize:
            if (progress)
                progress(filesServed, 0, "Querying: " + filename);

            if (!handleGetFileSize(transport, filename, resolverFn, firmwareDir))
                return false;
            break;

        case FileCommand::ReadFile:
            ++filesServed;
            if (progress)
                progress(filesServed, 0, "Sending: " + filename);

            if (!handleReadFile(transport, filename, resolverFn, firmwareDir, cancelled))
                return false;
            break;

        case FileCommand::Done:
            if (progress)
                progress(filesServed, filesServed, "Device boot complete");
            return true;

        default:
            _lastError = "Unknown file command: " + std::to_string(static_cast<int32_t>(msg.command));
            return false;
        }
    }

    _lastError = "Cancelled";
    return false;
}

bool FileServer::handleGetFileSize(IUsbTransport& transport,
                                    const std::string& filename,
                                    const FileResolver& resolver,
                                    const std::filesystem::path& firmwareDir)
{
    // Star-prefixed filenames are metadata -- the device is reporting info, not requesting a real file
    bool isMetadata = !filename.empty() && filename[0] == '*';

    std::vector<uint8_t> data;
    if (!isMetadata) {
        data = resolver(filename);
    }

    // Send the file size via a control transfer
    // Size is encoded the same way as in BootMessage: wValue = low 16 bits, wIndex = high 16 bits
    int32_t size = isMetadata ? 0 : static_cast<int32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(size & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((size >> 16) & 0xFFFF);

    // Send the size as the control transfer data payload as well
    auto sizeData = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&size), sizeof(size));

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, sizeData, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send file size for: " + filename;
        return false;
    }

    return true;
}

bool FileServer::handleReadFile(IUsbTransport& transport,
                                 const std::string& filename,
                                 const FileResolver& resolver,
                                 const std::filesystem::path& firmwareDir,
                                 const std::atomic<bool>& cancelled)
{
    // Star-prefixed filenames carry device metadata
    bool isMetadata = !filename.empty() && filename[0] == '*';

    std::vector<uint8_t> data = resolver(filename);

    if (isMetadata) {
        parseMetadata(filename, data);
        // Metadata files still get a zero-length response
        int32_t zero = 0;
        auto zd = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&zero), sizeof(zero));
        transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST, 0, 0, zd, DEFAULT_TIMEOUT_MS);
        return true;
    }

    if (data.empty()) {
        // File not found -- send zero size
        int32_t zero = 0;
        auto zd = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&zero), sizeof(zero));
        transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST, 0, 0, zd, DEFAULT_TIMEOUT_MS);
        return true;
    }

    // Send file size via control transfer
    int32_t totalSize = static_cast<int32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(totalSize & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);
    auto sizeData = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&totalSize), sizeof(totalSize));

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, sizeData, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send ReadFile size header for: " + filename;
        return false;
    }

    // Stream file data via bulk OUT in BULK_CHUNK_SIZE chunks
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load())
            return false;

        size_t chunkSize = std::min(BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = std::span<const uint8_t>(data.data() + offset, chunkSize);

        int transferred = transport.bulkWrite(EP_OUT, chunk, DEFAULT_TIMEOUT_MS);
        if (transferred < 0) {
            _lastError = "Bulk write failed sending file: " + filename + " at offset " + std::to_string(offset);
            return false;
        }
        offset += static_cast<size_t>(transferred);
    }

    return true;
}

void FileServer::parseMetadata(const std::string& filename,
                                const std::vector<uint8_t>& data)
{
    // Metadata filenames look like "*serial", "*mac", "*otp", "*board-rev"
    // The "data" returned by the resolver is typically empty for metadata;
    // the interesting part is in the filename itself (the device encodes
    // the value as part of the file path).

    // Strip the leading '*'
    std::string key = filename.substr(1);

    // The value may be appended after a '=' or encoded in the rest of the path
    std::string value;
    auto eqPos = key.find('=');
    if (eqPos != std::string::npos) {
        value = key.substr(eqPos + 1);
        key = key.substr(0, eqPos);
    } else if (!data.empty()) {
        value.assign(reinterpret_cast<const char*>(data.data()), data.size());
    }

    if (key == "serial" || key == "serialNumber")
        _metadata.serialNumber = value;
    else if (key == "mac" || key == "macAddress")
        _metadata.macAddress = value;
    else if (key == "otp" || key == "otpState")
        _metadata.otpState = value;
    else if (key == "board-rev" || key == "boardRevision") {
        try {
            _metadata.boardRevision = static_cast<uint32_t>(std::stoul(value, nullptr, 16));
        } catch (...) {
            // Ignore parse errors
        }
    }
}

std::vector<uint8_t> FileServer::readFileFromDisk(const std::filesystem::path& dir,
                                                    const std::string& filename)
{
    // Skip metadata requests -- they won't be on disk
    if (!filename.empty() && filename[0] == '*')
        return {};

    auto path = dir / filename;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};

    auto size = file.tellg();
    if (size <= 0)
        return {};

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

} // namespace rpiboot
