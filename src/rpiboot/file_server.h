/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * Reactive file server for the rpiboot protocol.
 *
 * After the second-stage bootloader runs on the device, the device
 * requests files over USB.  This class responds to GetFileSize / ReadFile
 * commands and collects device metadata from star-prefixed requests.
 */

#ifndef RPIBOOT_FILE_SERVER_H
#define RPIBOOT_FILE_SERVER_H

#include "rpiboot_types.h"
#include "usb_transport.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rpiboot {

// Function type for resolving file content by name.
// Returns the file data, or an empty vector if not found.
using FileResolver = std::function<std::vector<uint8_t>(const std::string& filename)>;

class FileServer {
public:
    // Run the file-server loop until the device sends Done (or an error
    // occurs, or the operation is cancelled).
    //
    // resolver is called to look up each requested file.  The default
    // implementation reads from firmwareDir on disk.
    //
    // requireReEnumConfirmation distinguishes two ambiguous disconnect cases:
    //   - Device rebooted into the next stage (e.g. fastboot gadget, or
    //     rpiboot after an SBR recovery_reboot) without sending Done —
    //     looks identical at libusb to a yanked cable.
    //   - User physically unplugged the device mid-flow.
    // When true, on any disconnect-with-files-served the file server waits a
    // grace window (matched to the caller's scanner timeout, ~60 s — covers
    // the fastboot gadget's full Linux boot, which can take up to ~30 s) for
    // the `cancelled` flag to flip, signalling that the caller's scanner has
    // observed the next-stage device on the same port path.  Without that
    // confirmation by deadline, the disconnect is reported as a real
    // failure.  When false, any disconnect-with-files-served is treated as
    // successful completion — for callers that don't run a confirmation
    // scanner.
    //
    // Returns true on clean Done (or confirmed re-enumeration when
    // requireReEnumConfirmation is set), false on error/cancel.
    bool run(IUsbTransport& transport,
             const std::filesystem::path& firmwareDir,
             ProgressCallback progress,
             const std::atomic<bool>& cancelled,
             FileResolver resolver = nullptr,
             bool requireReEnumConfirmation = false);

    // Metadata collected during the file-server phase
    const DeviceMetadata& metadata() const { return _metadata; }

    const std::string& lastError() const { return _lastError; }

private:
    bool handleGetFileSize(IUsbTransport& transport,
                           const std::string& filename,
                           const FileResolver& resolver,
                           const std::filesystem::path& firmwareDir);

    bool handleReadFile(IUsbTransport& transport,
                        const std::string& filename,
                        const FileResolver& resolver,
                        const std::filesystem::path& firmwareDir,
                        const std::atomic<bool>& cancelled);

    // Parse a star-prefixed metadata request ("*PROPERTY*VALUE") and record
    // it in _metadata.  Mirrors upstream rpiboot's write_metadata_file().
    void parseMetadata(const std::string& filename);

    DeviceMetadata _metadata;
    std::string _lastError;

public:
    // Default file resolver: read from the firmware directory
    static std::vector<uint8_t> readFileFromDisk(const std::filesystem::path& dir,
                                                  const std::string& filename);
};

} // namespace rpiboot

#endif // RPIBOOT_FILE_SERVER_H
