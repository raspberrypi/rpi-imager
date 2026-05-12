// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// XpcFileOperations - FileOperations implementation that routes all
// privileged I/O through the rpi-imager-writer helper via NSXPC,
// instead of opening /dev/rdiskN locally and doing pwrite() directly.
//
// This is the production write path that fixes the Tahoe authopen-FD
// bug: the helper opens the device with its own ::open() and holds
// the FD privately, so the kernel's per-write enforcement against
// borrowed FDs never has a chance to fire.
//
// Selected at runtime by `CreatePlatformFileOperations()` when the
// `RPI_IMAGER_USE_XPC_HELPER` environment variable is set, so that an
// A/B deployment can roll the helper-routed path out to a fraction of
// users without changing the binary.
//
// Phase 1b implementation note: writes go through the synchronous
// `writeChunk` path (bytes copied across the XPC boundary). The
// shared-memory bulk-write ring is wired and demonstrated by the
// diagnostic but not yet plumbed into the production write loop -
// that's a follow-up that lifts throughput without changing semantics.

#pragma once

#include "../file_operations.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace rpi_imager::privileged {
class IPrivilegedWriter;
namespace proto {
class SessionId;
}
}

namespace rpi_imager {

class XpcFileOperations : public FileOperations {
public:
    XpcFileOperations();
    ~XpcFileOperations() override;

    XpcFileOperations(const XpcFileOperations&) = delete;
    XpcFileOperations& operator=(const XpcFileOperations&) = delete;

    FileError OpenDevice(const std::string& path) override;
    FileError CreateTestFile(const std::string& path, std::uint64_t size) override;
    FileError WriteAtOffset(std::uint64_t offset,
                              const std::uint8_t* data,
                              std::size_t size) override;
    FileError GetSize(std::uint64_t& size) override;
    FileError Close() override;
    bool IsOpen() const override;

    FileError WriteSequential(const std::uint8_t* data, std::size_t size) override;
    FileError ReadSequential(std::uint8_t* data, std::size_t size,
                                std::size_t& bytes_read) override;

    // Async API - phase 1b: routes through the helper's shared-memory
    // bulk-write ring. SetAsyncQueueDepth(N) maps an N×slot_size ring
    // (via mapBulkBuffer); AsyncWriteSequential copies the caller's
    // buffer into a slot and submits via submitBulkWriteAsync, which
    // dispatches the pwrite on the helper side with bounded
    // parallelism. WaitForPendingWrites blocks until all in-flight
    // writes have completed.
    bool SetAsyncQueueDepth(int depth) override;
    int  GetAsyncQueueDepth() const override;
    bool IsAsyncIOSupported() const override;
    FileError AsyncWriteSequential(const std::uint8_t* data,
                                     std::size_t size,
                                     AsyncWriteCallback callback) override;
    int GetPendingWriteCount() const override;
    FileError WaitForPendingWrites() override;
    void CancelAsyncIO() override;
    void GetAsyncIOStats(uint32_t& wallClockMs, uint32_t& writeCount,
                          uint32_t& minLatencyUs, uint32_t& maxLatencyUs,
                          uint32_t& avgLatencyUs) const override;

    // Remaining pure-virtual obligations from the FileOperations
    // contract. The helper handles seeking implicitly per pwrite/pread
    // call, so most of these maintain client-side state to give the
    // existing callers what they expect.
    FileError Seek(std::uint64_t position) override;
    std::uint64_t Tell() const override;
    FileError ForceSync() override;
    FileError Flush() override;
    void PrepareForSequentialRead(std::uint64_t offset,
                                    std::uint64_t length) override;
    FileError PrepareDevice(std::uint64_t device_size,
                              bool zero_last_mb = true) override;
    FastVerifyResult FastVerifySha256(const std::uint8_t* prefix,
                                        std::size_t prefix_len,
                                        std::uint64_t device_offset,
                                        std::uint64_t length) override;

    // §7a SessionStats accessor. Populated by Close() from the helper's
    // closeSession reply. Read-only thereafter; cleared on next OpenDevice.
    HelperSessionStats GetLastSessionStats() const override;
    int GetHandle() const override { return -1; }
    int GetLastErrorCode() const override { return 0; }
    bool IsDirectIOEnabled() const override;
    FileError SetDirectIOEnabled(bool enabled) override;
    DirectIOInfo GetDirectIOInfo() const override;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rpi_imager
