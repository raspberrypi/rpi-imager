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
    // Returns true on clean Done, false on error/cancel.
    bool run(IUsbTransport& transport,
             const std::filesystem::path& firmwareDir,
             ProgressCallback progress,
             const std::atomic<bool>& cancelled,
             FileResolver resolver = nullptr);

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

    void parseMetadata(const std::string& filename,
                       const std::vector<uint8_t>& data);

public:
    // Default file resolver: read from the firmware directory
    static std::vector<uint8_t> readFileFromDisk(const std::filesystem::path& dir,
                                                  const std::string& filename);

    DeviceMetadata _metadata;
    std::string _lastError;
};

} // namespace rpiboot

#endif // RPIBOOT_FILE_SERVER_H
