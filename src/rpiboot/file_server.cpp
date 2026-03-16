/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_server.h"

#include <QDebug>

#include <cstring>
#include <fstream>
#include <thread>

namespace rpiboot {

// Vendor IN request type for ep_read (device-to-host control transfer)
constexpr uint8_t VENDOR_REQUEST_TYPE_IN = VENDOR_REQUEST_TYPE | 0x80;  // 0xC0

// Timeout for reading FileMessages from the device.
// Upstream uses 20s for a single blocking read. We use a shorter timeout
// with a retry loop so the UI stays responsive during the wait.
constexpr int EP_READ_TIMEOUT_MS = 3000;

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
    int consecutiveErrors = 0;
    constexpr int MAX_RETRIES = 20;
    std::string lastFilename;

    if (progress)
        progress(0, 0, "Waiting for device file requests...");

    while (!cancelled.load()) {
        // Read a FileMessage from the device via vendor control transfer IN.
        // This matches upstream rpiboot's ep_read() protocol: the device
        // sends command messages through control transfers, not bulk IN.
        FileMessage msg{};
        auto buf = std::span<uint8_t>(reinterpret_cast<uint8_t*>(&msg), sizeof(msg));

        int bytesRead = transport.controlTransferIn(
            VENDOR_REQUEST_TYPE_IN, VENDOR_REQUEST,
            static_cast<uint16_t>(sizeof(msg) & 0xFFFF),
            static_cast<uint16_t>(sizeof(msg) >> 16),
            buf, EP_READ_TIMEOUT_MS);

        if (bytesRead < static_cast<int>(sizeof(FileCommand))) {
            ++consecutiveErrors;
            qDebug() << "rpiboot: FileServer controlTransferIn returned"
                     << bytesRead << "bytes (need" << sizeof(FileCommand)
                     << "), retry" << consecutiveErrors << "/" << MAX_RETRIES
                     << (lastFilename.empty() ? "" : (", last file: " + lastFilename).c_str());

            // Fatal USB errors — the device is gone, retrying is pointless.
            // Matches upstream rpiboot which breaks on NO_DEVICE and IO errors.
            // LIBUSB_ERROR_NO_DEVICE=-4, LIBUSB_ERROR_ACCESS=-3,
            // LIBUSB_ERROR_PIPE=-9, LIBUSB_ERROR_OVERFLOW=-8
            constexpr int LIBUSB_ERR_NO_DEVICE = -4;
            if (bytesRead == LIBUSB_ERR_NO_DEVICE) {
                _lastError = "Device disconnected (libusb error "
                    + std::to_string(bytesRead) + ")"
                    + (lastFilename.empty() ? "" : " after serving: " + lastFilename);
                return false;
            }

            if (consecutiveErrors > MAX_RETRIES) {
                _lastError = "Failed to read FileMessage from device (error "
                    + std::to_string(bytesRead) + " after "
                    + std::to_string(consecutiveErrors) + " retries"
                    + (lastFilename.empty() ? "" : "; last file: " + lastFilename)
                    + ")";
                return false;
            }
            if (progress)
                progress(filesServed, 0,
                    "Waiting for device (retry " + std::to_string(consecutiveErrors)
                    + "/" + std::to_string(MAX_RETRIES)
                    + ", error " + std::to_string(bytesRead) + ")...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        consecutiveErrors = 0;

        std::string filename(msg.name());
        lastFilename = filename;

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
            _lastError = "Unknown file command " + std::to_string(static_cast<int32_t>(msg.command))
                + " for file: " + filename;
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

    // Send the file size via a zero-data vendor control transfer.
    // Size is encoded in wValue (low 16) and wIndex (high 16), matching
    // the upstream pattern where ep_write(NULL, 0) or a direct
    // libusb_control_transfer sends no data payload.
    int32_t size = isMetadata ? 0 : static_cast<int32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(size & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((size >> 16) & 0xFFFF);

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, {}, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send file size for: " + filename;
        qDebug() << "rpiboot:" << _lastError.c_str() << "size=" << size;
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
    // Star-prefixed filenames carry device metadata — the value is encoded
    // in the filename itself, no file lookup needed.
    bool isMetadata = !filename.empty() && filename[0] == '*';

    if (isMetadata) {
        parseMetadata(filename, {});
        // Metadata: zero-length ep_write response (control transfer only, no bulk)
        transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                  0, 0, {}, DEFAULT_TIMEOUT_MS);
        return true;
    }

    std::vector<uint8_t> data = resolver(filename);

    if (data.empty()) {
        // File not found: zero-length ep_write response
        transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                  0, 0, {}, DEFAULT_TIMEOUT_MS);
        return true;
    }

    // Send file data via ep_write protocol:
    // 1. Control transfer announces the total size (no data payload)
    // 2. Bulk OUT sends the actual file data in BULK_CHUNK_SIZE chunks
    int32_t totalSize = static_cast<int32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(totalSize & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, {}, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send ReadFile size header for: " + filename;
        qDebug() << "rpiboot:" << _lastError.c_str() << "totalSize=" << totalSize;
        return false;
    }

    // Stream file data via bulk OUT
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load())
            return false;

        size_t chunkSize = std::min(BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = std::span<const uint8_t>(data.data() + offset, chunkSize);

        int transferred = transport.bulkWrite(transport.outEndpoint(), chunk, DEFAULT_TIMEOUT_MS);
        if (transferred < 0) {
            _lastError = "Bulk write failed sending file: " + filename
                + " at offset " + std::to_string(offset)
                + " of " + std::to_string(data.size())
                + " (libusb error " + std::to_string(transferred) + ")";
            qDebug() << "rpiboot:" << _lastError.c_str()
                     << "ep=0x" << Qt::hex << (int)transport.outEndpoint();
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
