/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "file_server.h"

#include <QDebug>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <utility>

namespace rpiboot {

// Vendor IN request type for ep_read (device-to-host control transfer)
constexpr uint8_t VENDOR_REQUEST_TYPE_IN = VENDOR_REQUEST_TYPE | 0x80;  // 0xC0

// Timeout for reading FileMessages from the device, matching upstream
// rpiboot's ep_read() (20s blocking control IN).
//
// This MUST be long enough to cover the device's longest legitimate pause
// between messages — most notably the multi-second EEPROM erase/write after
// it pulls pieeprom.bin, and the gaps between the metadata fields it streams
// back at the end of provisioning (USER_SERIAL_NUM, MAC_ADDR, EEPROM_UPDATE,
// SECURE_BOOT_PROVISION, ...).
//
// A short timeout here is actively harmful: the file-server protocol is a
// strict host-polled ping-pong (device sends a message in response to our
// control IN, waits for our control-OUT ack, then waits for the next IN).
// If we cancel the IN mid-pause and reissue, the device's eventual response
// lands misaligned on the next transfer — we read the tail of the previous
// message as a bogus command, never send the ack the device is waiting for,
// and the handshake stalls (the device goes silent and we time out forever).
// Re-enumeration on reboot still returns promptly because libusb reports
// NO_DEVICE/IO immediately, so the long timeout doesn't cost us in the
// common completion path.
constexpr int EP_READ_TIMEOUT_MS = 20000;

bool FileServer::run(IUsbTransport& transport,
                      const std::filesystem::path& firmwareDir,
                      ProgressCallback progress,
                      const std::atomic<bool>& cancelled,
                      FileResolver resolver,
                      bool requireReEnumConfirmation)
{
    // Use the disk-based resolver as default
    FileResolver resolverFn = resolver ? std::move(resolver)
        : [&firmwareDir](const std::string& name) {
              return readFileFromDisk(firmwareDir, name);
          };

    uint64_t filesServed = 0;
    int consecutiveErrors = 0;
    // With a 20s blocking read (EP_READ_TIMEOUT_MS), each retry already waits
    // out any legitimate device pause, so a small budget is enough.  Real
    // reboots/disconnects short-circuit immediately via NO_DEVICE/IO below and
    // don't consume this budget; this only bounds the genuinely-mute-but-
    // present case (~3 × 20s) and a misaligned-read garbage flood.
    constexpr int MAX_RETRIES = 3;
    std::string lastFilename;

    if (progress)
        progress(0, 0, "Waiting for device file requests...");

    // On a disconnect-with-files-served (NO_DEVICE, IO, exhausted retries, or
    // a flood of garbage messages), libusb can't tell us whether the device
    // rebooted into the next stage or was physically unplugged.  This helper
    // resolves the ambiguity by waiting for an external confirmation signal:
    //   - Fastboot: a scanner thread polls for the fastboot gadget on the
    //     same port path and sets the cancellation flag once it appears.
    //     The gadget is a full Linux instance — boot can take up to ~30 s,
    //     occasionally longer, so the grace window must cover that worst
    //     case.  Aligned with the scanner's own 60 s timeout in
    //     RpibootThread::pollForFastbootDevice() so file_server doesn't
    //     give up before the scanner does.
    //   - SecureBootRecovery: a similar scanner watches for the device
    //     returning to rpiboot after the EEPROM write (the recovery
    //     config.txt is rewritten by FirmwareManager to ensure
    //     set_boot_order=0x3 + recovery_reboot=1 are present in the right
    //     order — see ensureSbrReenumerates).  Same 60 s timeout.
    // The progress callback is driven during the wait so the caller's
    // cancel-bridge lambda (which polls fastbootFound and updates the
    // cancellation flag) has a chance to fire — without that, the flag
    // would never flip even after re-enumeration.
    auto resolveDisconnect = [&](const char* reason) -> bool {
        if (!requireReEnumConfirmation) {
            qDebug() << "rpiboot:" << reason << "after serving"
                     << filesServed << "file(s) — treating as successful completion";
            if (progress)
                progress(filesServed, filesServed, "Device rebooted into next stage");
            return true;
        }

        constexpr int GRACE_WINDOW_MS = 60000;  // matches pollForFastbootDevice
        constexpr int POLL_INTERVAL_MS = 200;
        qDebug() << "rpiboot:" << reason << "after serving" << filesServed
                 << "file(s) — waiting up to" << GRACE_WINDOW_MS
                 << "ms for next-stage device confirmation"
                 << "(gadget Linux boot can take ~30s)";

        for (int waited = 0; waited < GRACE_WINDOW_MS; waited += POLL_INTERVAL_MS) {
            if (cancelled.load()) {
                qDebug() << "rpiboot: next-stage device confirmed by caller after"
                         << waited << "ms (" << reason << ")";
                if (progress)
                    progress(filesServed, filesServed, "Device rebooted into next stage");
                return true;
            }
            if (progress)
                progress(filesServed, 0,
                    "Confirming device re-enumeration ("
                    + std::to_string(waited / 1000 + 1) + "s of "
                    + std::to_string(GRACE_WINDOW_MS / 1000) + "s)...");
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
        }

        _lastError = std::string(reason) + " after serving "
                   + std::to_string(filesServed)
                   + " file(s) and never re-enumerated as next-stage device"
                   + (lastFilename.empty() ? "" : "; last file: " + lastFilename)
                   + " — check the USB cable and try again";
        qWarning() << "rpiboot:" << QString::fromStdString(_lastError);
        return false;
    };

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
            int maxRetries = MAX_RETRIES;
            qDebug() << "rpiboot: FileServer controlTransferIn returned"
                     << bytesRead << "bytes (need" << sizeof(FileCommand)
                     << "), retry" << consecutiveErrors << "/" << maxRetries
                     << (lastFilename.empty() ? "" : (", last file: " + lastFilename).c_str());

            // Fatal USB errors — the device is gone, retrying is pointless.
            // Matches upstream rpiboot which breaks on NO_DEVICE and IO.
            constexpr int LIBUSB_ERR_IO        = -1;
            constexpr int LIBUSB_ERR_NO_DEVICE = -4;
            if (bytesRead == LIBUSB_ERR_NO_DEVICE || bytesRead == LIBUSB_ERR_IO) {
                if (filesServed > 0) {
                    return resolveDisconnect(
                        bytesRead == LIBUSB_ERR_NO_DEVICE
                            ? "device disconnected (NO_DEVICE)"
                            : "device disconnected (IO)");
                }
                _lastError = "Device disconnected (libusb error "
                    + std::to_string(bytesRead) + ")"
                    + (lastFilename.empty() ? "" : " after serving: " + lastFilename);
                return false;
            }

            if (consecutiveErrors > maxRetries) {
                if (filesServed > 0)
                    return resolveDisconnect("max retries reached");
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
                    + "/" + std::to_string(maxRetries)
                    + ", error " + std::to_string(bytesRead) + ")...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        // Validate the message before resetting the error counter.
        // After the device reboots (e.g. into fastboot), reads may return
        // garbage bytes — we must not reset consecutiveErrors, interpret
        // the filename, or pass garbage to std::filesystem::path.
        auto isGarbage = [&msg]() {
            // Invalid command?
            if (msg.command != FileCommand::GetFileSize &&
                msg.command != FileCommand::ReadFile &&
                msg.command != FileCommand::Done)
                return true;
            // Valid command but garbage filename?  rpiboot filenames are
            // always printable ASCII (or '*'-prefixed metadata).  If the
            // filename contains non-printable or non-ASCII bytes the
            // entire message is corrupted.
            for (auto ch : msg.name()) {
                auto uch = static_cast<unsigned char>(ch);
                if (uch < 0x20 || uch > 0x7E)
                    return true;
            }
            return false;
        };
        if (isGarbage()) {
            ++consecutiveErrors;
            int maxRetries = MAX_RETRIES;
            qDebug() << "rpiboot: garbage message (cmd="
                     << static_cast<int32_t>(msg.command)
                     << "0x" << Qt::hex << static_cast<uint32_t>(msg.command) << Qt::dec
                     << ") retry" << consecutiveErrors << "/" << maxRetries;
            if (consecutiveErrors > maxRetries) {
                if (filesServed > 0)
                    return resolveDisconnect("garbage messages flooded");
                _lastError = "Unknown file command "
                    + std::to_string(static_cast<int32_t>(msg.command));
                return false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        consecutiveErrors = 0;

        std::string filename(msg.name());
        lastFilename = filename;

        // Upstream rpiboot exits on empty filename (any command value).
        // The device sends this as the "done" signal after all files are served.
        if (filename.empty()) {
            // Acknowledge with a zero-length response, matching upstream
            transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                      0, 0, {}, DEFAULT_TIMEOUT_MS);
            if (progress)
                progress(filesServed, filesServed, "Device boot complete");
            return true;
        }

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
            break;  // unreachable — validated above
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
        parseMetadata(filename);
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
    // 2. Bulk OUT sends the entire file in a single transfer
    int32_t totalSize = static_cast<int32_t>(data.size());
    uint16_t wValue = static_cast<uint16_t>(totalSize & 0xFFFF);
    uint16_t wIndex = static_cast<uint16_t>((totalSize >> 16) & 0xFFFF);

    if (!transport.controlTransfer(VENDOR_REQUEST_TYPE, VENDOR_REQUEST,
                                    wValue, wIndex, {}, DEFAULT_TIMEOUT_MS)) {
        _lastError = "Failed to send ReadFile size header for: " + filename;
        qDebug() << "rpiboot:" << _lastError.c_str() << "totalSize=" << totalSize;
        return false;
    }

    if (cancelled.load())
        return false;

    // Chunk the bulk transfer into BULK_CHUNK_SIZE (16 KB) pieces with a
    // generous per-chunk timeout, matching upstream usbboot's ep_write().
    // A single 56 MB libusb_bulk_transfer call is rejected by the macOS
    // USB stack (LIBUSB_ERROR_IO -1) for oversized requests; the upstream
    // tool avoids that by issuing many small calls in sequence.
    constexpr int CHUNK_TIMEOUT_MS = 5000;
    const uint8_t outEp = transport.outEndpoint();
    size_t offset = 0;
    while (offset < data.size()) {
        if (cancelled.load())
            return false;

        size_t chunkSize = std::min(BULK_CHUNK_SIZE, data.size() - offset);
        auto chunk = std::span<const uint8_t>(data.data() + offset, chunkSize);

        int transferred = transport.bulkWrite(outEp, chunk, CHUNK_TIMEOUT_MS);
        if (transferred < 0 || static_cast<size_t>(transferred) != chunkSize) {
            _lastError = "Bulk write failed sending file: " + filename
                + " (transferred " + std::to_string(offset + std::max(transferred, 0))
                + " of " + std::to_string(data.size()) + " bytes";
            if (transferred < 0)
                _lastError += ", libusb error " + std::to_string(transferred);
            _lastError += ")";
            qDebug() << "rpiboot:" << _lastError.c_str()
                     << "ep=0x" << Qt::hex << (int)outEp;
            return false;
        }
        offset += static_cast<size_t>(transferred);
    }

    return true;
}

namespace {

// ── DUID C40 decode ─────────────────────────────────────────────────────
// Ported verbatim from usbboot's decode_duid.c so that FACTORY_UUID values
// are reported in the same human-readable form as upstream rpiboot.

constexpr int DUID_LENGTH = 36;

int charToC40(char val)
{
    if (val >= 'a' && val <= 'z')
        val = static_cast<char>(val - 32);
    if (val >= '0' && val <= '9')
        return 4 + val - '0';
    if (val >= 'A' && val <= 'Z')
        return 14 + val - 'A';
    return -1;
}

char c40ToChar(int val)
{
    if (val >= charToC40('0') && val <= charToC40('9'))
        return static_cast<char>('0' + val - charToC40('0'));
    if (val >= charToC40('A') && val <= charToC40('Z'))
        return static_cast<char>('A' + val - charToC40('A'));
    return '\0';
}

void decodeHalfWord(int halfWord, int* c40List, int& index)
{
    c40List[index] = (halfWord - 1) / 1600;
    halfWord -= c40List[index++] * 1600;
    c40List[index] = (halfWord - 1) / 40;
    halfWord -= c40List[index++] * 40;
    c40List[index++] = halfWord - 1;
}

// Decode an underscore-separated list of hex words into the C40 string.
// Returns false on invalid input (matches upstream's -1 return).
bool decodeDuidC40(const std::string& wordsIn, std::string& out)
{
    int c40List[DUID_LENGTH];
    int i = 0;

    std::string buf = wordsIn;  // strtok mutates its input
    for (char* tok = std::strtok(buf.data(), "_"); tok != nullptr;
         tok = std::strtok(nullptr, "_")) {
        uint32_t word = static_cast<uint32_t>(std::strtoul(tok, nullptr, 16));
        if (word == 0)
            break;
        // Each half-word emits 3 C40 values; guard the fixed-size buffer.
        if (i + 3 > DUID_LENGTH)
            return false;
        decodeHalfWord(static_cast<int>(word & 0xFFFF), c40List, i);

        uint16_t msig = static_cast<uint16_t>(word >> 16);
        if (msig > 0) {
            if (i + 3 > DUID_LENGTH)
                return false;
            decodeHalfWord(msig, c40List, i);
        }
    }

    out.clear();
    for (int c = 0; c < i; ++c) {
        char ch = c40ToChar(c40List[c]);
        if (ch == '\0')
            return false;
        out.push_back(ch);
    }
    return true;
}

} // namespace

void FileServer::parseMetadata(const std::string& filename)
{
    // The device encodes metadata as "*PROPERTY*VALUE" in the requested
    // filename — e.g. "*USER_SERIAL_NUM*b91b3ce7", "*EEPROM_UPDATE*success".
    // Mirror upstream's write_metadata_file(): split on '*' into a property
    // (first token) and value (second token), and record every pair.
    if (filename.size() < 2 || filename[0] != '*')
        return;

    const std::string body = filename.substr(1);  // strip leading '*'
    const auto sep = body.find('*');
    std::string property = (sep == std::string::npos) ? body : body.substr(0, sep);
    std::string value;
    if (sep != std::string::npos) {
        // Upstream uses strtok(NULL, "*"), i.e. the token up to the next '*'.
        const auto end = body.find('*', sep + 1);
        value = body.substr(sep + 1, end == std::string::npos
                                         ? std::string::npos
                                         : end - (sep + 1));
    }

    if (property.empty())
        return;

    // FACTORY_UUID is C40-encoded; decode it to match upstream's output.
    if (property == "FACTORY_UUID" && !value.empty()) {
        std::string decoded;
        if (decodeDuidC40(value, decoded))
            value = std::move(decoded);
        else
            qWarning() << "rpiboot: failed to decode FACTORY_UUID metadata:"
                       << QString::fromStdString(value);
    }

    _metadata.fields.emplace_back(property, value);
    qDebug() << "rpiboot metadata:" << QString::fromStdString(property)
             << "=" << QString::fromStdString(value);

    // Populate typed convenience accessors from the upstream property names.
    if (property == "USER_SERIAL_NUM")
        _metadata.serialNumber = value;
    else if (property == "MAC_ADDR")
        _metadata.macAddress = value;
    else if (property == "USER_BOARDREV") {
        try {
            _metadata.boardRevision =
                static_cast<uint32_t>(std::stoul(value, nullptr, 16));
        } catch (...) {
            // Ignore parse errors — keep the raw value in fields regardless.
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
