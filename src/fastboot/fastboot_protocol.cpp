/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "fastboot_protocol.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fastboot {

// ── Response reading ───────────────────────────────────────────────────

Response FastbootProtocol::readResponse(rpiboot::IUsbTransport& transport, int timeoutMs)
{
    // Fastboot responses are ASCII, max 256 bytes:
    //   "OKAY" [message]
    //   "FAIL" [message]
    //   "DATA" <8-hex-char size>
    //   "INFO" [message]
    //   "TEXT" [message]
    uint8_t buf[256];
    auto span = std::span<uint8_t>(buf);

    int bytesRead = transport.bulkRead(EP_IN, span, timeoutMs);
    if (bytesRead < 4) {
        return {Response::Fail, "Short or failed read from device", 0};
    }

    std::string raw(reinterpret_cast<const char*>(buf), static_cast<size_t>(bytesRead));
    std::string prefix = raw.substr(0, 4);
    std::string message = (bytesRead > 4) ? raw.substr(4) : "";

    Response resp;
    resp.message = message;

    if (prefix == "OKAY") {
        resp.type = Response::Okay;
    } else if (prefix == "FAIL") {
        resp.type = Response::Fail;
    } else if (prefix == "DATA") {
        resp.type = Response::Data;
        // Parse 8-char hex size
        if (message.size() >= 8) {
            resp.dataSize = static_cast<uint32_t>(std::stoul(message.substr(0, 8), nullptr, 16));
        }
    } else if (prefix == "INFO") {
        resp.type = Response::Info;
    } else if (prefix == "TEXT") {
        resp.type = Response::Text;
    } else {
        resp.type = Response::Fail;
        resp.message = "Unknown response prefix: " + prefix;
    }

    return resp;
}

// ── Command sending ────────────────────────────────────────────────────

Response FastbootProtocol::sendCommand(rpiboot::IUsbTransport& transport,
                                        std::string_view command,
                                        int timeoutMs)
{
    // Send command as ASCII via bulk OUT
    auto data = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(command.data()), command.size());

    int written = transport.bulkWrite(EP_OUT, data, timeoutMs);
    if (written < 0) {
        return {Response::Fail, "Failed to send command", 0};
    }

    // Read responses -- INFO/TEXT are intermediate, keep reading until OKAY/FAIL/DATA
    for (;;) {
        auto resp = readResponse(transport, timeoutMs);
        if (resp.type != Response::Info && resp.type != Response::Text) {
            return resp;
        }
        // INFO/TEXT: continue reading for the terminal response
    }
}

// ── Data transfer (shared by download and stage) ──────────────────────

bool FastbootProtocol::sendData(rpiboot::IUsbTransport& transport,
                                 std::string_view commandPrefix,
                                 std::span<const uint8_t> data,
                                 rpiboot::ProgressCallback progress,
                                 std::atomic<bool>& cancelled)
{
    _lastError.clear();

    // Note: sends the command via raw bulkWrite rather than sendCommand(),
    // so INFO/TEXT intermediates before the DATA response are not handled.
    // The RPi fastboot gadget does not emit INFO before DATA for download/stage.

    // Send "<prefix>:<size-hex>"
    char cmd[64];
    int cmdLen = snprintf(cmd, sizeof(cmd), "%.*s:%08x",
             static_cast<int>(commandPrefix.size()), commandPrefix.data(),
             static_cast<unsigned>(data.size()));
    if (cmdLen < 0 || static_cast<size_t>(cmdLen) >= sizeof(cmd)) {
        _lastError = "Command prefix too long for fastboot protocol buffer";
        return false;
    }

    auto cmdSpan = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(cmd), static_cast<size_t>(cmdLen));
    int written = transport.bulkWrite(EP_OUT, cmdSpan, 3000);
    if (written < 0) {
        _lastError = std::string("Failed to send ") + std::string(commandPrefix) + " command";
        return false;
    }

    // Read DATA response
    auto resp = readResponse(transport, 10000);
    if (resp.type != Response::Data) {
        _lastError = "Expected DATA response, got: " + resp.message;
        return false;
    }

    // Stream data to device
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load()) {
            _lastError = "Cancelled";
            return false;
        }

        size_t chunkSize = std::min<size_t>(rpiboot::BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = data.subspan(offset, chunkSize);

        int xfer = transport.bulkWrite(EP_OUT, chunk, 5000);
        if (xfer < 0) {
            _lastError = "Bulk write failed during " + std::string(commandPrefix)
                + " at offset " + std::to_string(offset);
            return false;
        }
        offset += static_cast<size_t>(xfer);

        if (progress)
            progress(offset, data.size(), "Transferring to device...");
    }

    // Read final OKAY
    auto finalResp = readResponse(transport, 30000);
    if (finalResp.type != Response::Okay) {
        _lastError = std::string(commandPrefix) + " failed: " + finalResp.message;
        return false;
    }

    return true;
}

bool FastbootProtocol::download(rpiboot::IUsbTransport& transport,
                                 std::span<const uint8_t> data,
                                 rpiboot::ProgressCallback progress,
                                 std::atomic<bool>& cancelled)
{
    return sendData(transport, "download", data, progress, cancelled);
}

bool FastbootProtocol::stage(rpiboot::IUsbTransport& transport,
                              std::span<const uint8_t> data,
                              rpiboot::ProgressCallback progress,
                              std::atomic<bool>& cancelled)
{
    return sendData(transport, "download", data, progress, cancelled);
}

// ── Upload (device → host) ────────────────────────────────────────────

std::vector<uint8_t> FastbootProtocol::upload(rpiboot::IUsbTransport& transport,
                                               rpiboot::ProgressCallback progress,
                                               std::atomic<bool>& cancelled)
{
    _lastError.clear();

    // Note: sends "upload" via raw bulkWrite rather than sendCommand(),
    // so INFO/TEXT intermediates before the DATA response are not handled.
    // The RPi fastboot gadget does not emit INFO before DATA for upload.

    // Send "upload" command
    const char* cmd = "upload";
    auto cmdSpan = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(cmd), std::strlen(cmd));
    int written = transport.bulkWrite(EP_OUT, cmdSpan, 3000);
    if (written < 0) {
        _lastError = "Failed to send upload command";
        return {};
    }

    // Read DATA response with size
    auto resp = readResponse(transport, 10000);
    if (resp.type != Response::Data) {
        _lastError = "Expected DATA response for upload, got: " + resp.message;
        return {};
    }

    // Read payload from bulk IN
    std::vector<uint8_t> result;
    result.reserve(resp.dataSize);
    size_t remaining = resp.dataSize;

    while (remaining > 0) {
        if (cancelled.load()) {
            _lastError = "Cancelled";
            return {};
        }

        uint8_t buf[rpiboot::BULK_CHUNK_SIZE];
        size_t toRead = std::min<size_t>(remaining, sizeof(buf));
        auto bufSpan = std::span<uint8_t>(buf, toRead);

        int bytesRead = transport.bulkRead(EP_IN, bufSpan, 5000);
        if (bytesRead <= 0) {
            _lastError = "Bulk read failed during upload at offset "
                + std::to_string(result.size());
            return {};
        }
        result.insert(result.end(), buf, buf + bytesRead);
        remaining -= static_cast<size_t>(bytesRead);

        if (progress)
            progress(result.size(), resp.dataSize, "Uploading from device...");
    }

    // Read final OKAY
    auto finalResp = readResponse(transport, 10000);
    if (finalResp.type != Response::Okay) {
        _lastError = "Upload failed: " + finalResp.message;
        return {};
    }

    return result;
}

// ── Device mount/umount ─────────────────────────────────────────────

bool FastbootProtocol::mountDevice(rpiboot::IUsbTransport& transport,
                                    std::string_view device,
                                    std::string_view mountpoint,
                                    std::string_view fstype)
{
    std::string cmd = "oem mount " + std::string(device) + " " + std::string(mountpoint);
    if (!fstype.empty())
        cmd += " " + std::string(fstype);

    auto resp = sendCommand(transport, cmd, 30000);
    if (resp.type != Response::Okay) {
        _lastError = "oem mount failed: " + resp.message;
        return false;
    }
    return true;
}

bool FastbootProtocol::umountDevice(rpiboot::IUsbTransport& transport,
                                     std::string_view mountpoint)
{
    auto resp = sendCommand(transport, "oem umount " + std::string(mountpoint), 30000);
    if (resp.type != Response::Okay) {
        _lastError = "oem umount failed: " + resp.message;
        return false;
    }
    return true;
}

// ── Device file transfer convenience methods ──────────────────────────

bool FastbootProtocol::writeDeviceFile(rpiboot::IUsbTransport& transport,
                                        std::string_view devicePath,
                                        std::span<const uint8_t> data,
                                        std::atomic<bool>& cancelled)
{
    _lastError.clear();

    if (!stage(transport, data, nullptr, cancelled))
        return false;

    std::string cmd = "oem download-file " + std::string(devicePath);
    auto resp = sendCommand(transport, cmd, 30000);
    if (resp.type != Response::Okay) {
        _lastError = "oem download-file failed for " + std::string(devicePath)
            + ": " + resp.message;
        return false;
    }
    return true;
}

std::vector<uint8_t> FastbootProtocol::readDeviceFile(rpiboot::IUsbTransport& transport,
                                                       std::string_view devicePath,
                                                       std::atomic<bool>& cancelled)
{
    _lastError.clear();

    std::string cmd = "oem upload-file " + std::string(devicePath);
    auto resp = sendCommand(transport, cmd, 30000);
    if (resp.type != Response::Okay) {
        _lastError = "oem upload-file failed for " + std::string(devicePath)
            + ": " + resp.message;
        return {};
    }

    return upload(transport, nullptr, cancelled);
}

// ── Flash ──────────────────────────────────────────────────────────────

bool FastbootProtocol::flash(rpiboot::IUsbTransport& transport,
                              std::string_view partition,
                              int timeoutMs)
{
    std::string cmd = "flash:" + std::string(partition);
    auto resp = sendCommand(transport, cmd, timeoutMs);
    if (resp.type != Response::Okay) {
        _lastError = "Flash failed: " + resp.message;
        return false;
    }
    return true;
}

// ── getvar ─────────────────────────────────────────────────────────────

std::optional<std::string> FastbootProtocol::getVar(rpiboot::IUsbTransport& transport,
                                                     std::string_view name)
{
    std::string cmd = "getvar:" + std::string(name);
    auto resp = sendCommand(transport, cmd, 3000);
    if (resp.type == Response::Okay)
        return resp.message;
    return std::nullopt;
}

// ── Combined flash ─────────────────────────────────────────────────────

bool FastbootProtocol::flashImage(rpiboot::IUsbTransport& transport,
                                   std::string_view partition,
                                   std::span<const uint8_t> data,
                                   rpiboot::ProgressCallback progress,
                                   std::atomic<bool>& cancelled)
{
    if (!download(transport, data, progress, cancelled))
        return false;
    return flash(transport, partition, 60000);
}

} // namespace fastboot
