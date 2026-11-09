// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// WindowsHelperFileOperations - FileOperations implementation that routes
// privileged disk I/O through the elevated rpi-imager-writer.exe helper
// (WindowsUacBackend) instead of opening \\.\PhysicalDriveN locally. This is
// the Windows analogue of macOS's XpcFileOperations (§14.6).
//
// Selected by CreatePlatformFileOperations() when the build option +
// RPI_IMAGER_USE_WINDOWS_HELPER opt-in are set. The default Windows path
// remains the in-process WindowsFileOperations.
//
// Windows-only translation unit: CMake compiles it only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set.

#pragma once

#include "../file_operations.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace rpi_imager {

class WindowsHelperFileOperations : public FileOperations {
public:
    WindowsHelperFileOperations();
    ~WindowsHelperFileOperations() override;

    WindowsHelperFileOperations(const WindowsHelperFileOperations&) = delete;
    WindowsHelperFileOperations& operator=(const WindowsHelperFileOperations&) = delete;

    FileError OpenDevice(const std::string& path) override;
    FileError CreateTestFile(const std::string& path, std::uint64_t size) override;
    FileError WriteAtOffset(std::uint64_t offset, const std::uint8_t* data,
                            std::size_t size) override;
    FileError GetSize(std::uint64_t& size) override;
    FileError Close() override;
    bool IsOpen() const override;

    FileError WriteSequential(const std::uint8_t* data, std::size_t size) override;
    FileError ReadSequential(std::uint8_t* data, std::size_t size,
                             std::size_t& bytes_read) override;

    void SetMaxWriteSizeHint(std::size_t bytes) override;

    bool SetAsyncQueueDepth(int depth) override;
    int GetAsyncQueueDepth() const override;
    bool IsAsyncIOSupported() const override;
    FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size,
                                   AsyncWriteCallback callback = nullptr) override;
    int GetPendingWriteCount() const override;
    FileError WaitForPendingWrites() override;
    void CancelAsyncIO() override;

    FileError Seek(std::uint64_t position) override;
    std::uint64_t Tell() const override;
    FileError ForceSync() override;
    FileError Flush() override;
    void PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) override;
    FileError PrepareDevice(std::uint64_t device_size,
                            bool zero_last_mb = true) override;

    int GetHandle() const override { return -1; }
    int GetLastErrorCode() const override;
    bool IsDirectIOEnabled() const override;
    FileError SetDirectIOEnabled(bool enabled) override;
    DirectIOInfo GetDirectIOInfo() const override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

std::unique_ptr<FileOperations> CreateWindowsHelperFileOperations();

} // namespace rpi_imager
