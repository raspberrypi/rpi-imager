// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// WindowsHelperFileOperations - FileOperations implementation that routes
// privileged disk I/O through the elevated rpi-imager-writer.exe helper
// (WindowsUacBackend) instead of opening \\.\PhysicalDriveN locally. This is
// the Windows analogue of macOS's XpcFileOperations (§14.6).

#pragma once

#include "../file_operations.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rpi_imager {

class WindowsWriteBufferProvider;

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
    std::shared_ptr<WriteBufferProvider> CreateWriteBufferProvider() override;

    bool SetAsyncQueueDepth(int depth) override;
    int GetAsyncQueueDepth() const override;
    bool IsAsyncIOSupported() const override;
    FileError AsyncWriteSequential(const std::uint8_t* data, std::size_t size,
                                   AsyncWriteCallback callback = nullptr) override;
    int GetPendingWriteCount() const override;
    FileError WaitForPendingWrites() override;
    void CancelAsyncIO() override;
    void GetAsyncIOStats(uint32_t& wallClockMs, uint32_t& writeCount,
                         uint32_t& minLatencyUs, uint32_t& maxLatencyUs,
                         uint32_t& avgLatencyUs) const override;
    void GetZeroCopyWriteStats(bool& engaged, std::uint64_t& zeroCopySubmits,
                               std::uint64_t& copySubmits) const override;

    FileError Seek(std::uint64_t position) override;
    std::uint64_t Tell() const override;
    FileError ForceSync() override;
    FileError Flush() override;
    void PrepareForSequentialRead(std::uint64_t offset, std::uint64_t length) override;
    FileError PrepareDevice(std::uint64_t device_size,
                            bool zero_last_mb = true) override;
    FastVerifyResult FastVerifySha256(const std::uint8_t* prefix,
                                      std::size_t prefix_len,
                                      std::uint64_t device_offset,
                                      std::uint64_t length) override;
    HelperSessionStats GetLastSessionStats() const override;

    int GetHandle() const override { return -1; }
    int GetLastErrorCode() const override;
    bool IsDirectIOEnabled() const override;
    FileError SetDirectIOEnabled(bool enabled) override;
    DirectIOInfo GetDirectIOInfo() const override;

private:
    friend class WindowsWriteBufferProvider;

    bool adoptZeroCopyRing(std::size_t count, std::size_t slotSize,
                           std::vector<void*>& outSlots);
    void releaseZeroCopyRing();

    struct State;
    std::unique_ptr<State> state_;
};

std::unique_ptr<FileOperations> CreateWindowsHelperFileOperations();

} // namespace rpi_imager
