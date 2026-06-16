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
// This is the default macOS path, selected by
// `CreatePlatformFileOperations()`. Opt out to the legacy in-process
// path with `RPI_IMAGER_USE_LEGACY_AUTHOPEN=1` for diagnostic comparison.
//
// Write path: the production loop enables async I/O (SetAsyncQueueDepth
// > 1) and submits via AsyncWriteSequential, which dispatches each chunk to
// the helper's bulk-write path (submitBulkWriteAsync -> bulkWriteFromBuffer)
// from a shared-memory ring slot. The synchronous writeChunk path is the
// fallback for small/metadata writes, oversized chunks, and when the ring
// can't be mapped. See doc/privileged-helper-plan.md §6.
//
// Zero-copy: CreateWriteBufferProvider() can back the producer's write ring
// buffer directly with the helper's shared-memory ring (see
// write_buffer_provider.h), so the producer decompresses straight into
// device-write memory and AsyncWriteSequential submits the slot's offset
// with no intermediate copy. When the ring can't be mapped (e.g. the
// geometry exceeds the helper's bulk-buffer cap), the provider falls back to
// aligned-heap slots and the one-copy path is used. See §13.

#pragma once

#include "../file_operations.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rpi_imager::privileged {
class IPrivilegedWriter;
namespace proto {
class SessionId;
}
}

namespace rpi_imager {

class XpcWriteBufferProvider;

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

    // Async API: routes through the helper's shared-memory bulk-write
    // ring. SetAsyncQueueDepth(N) maps an N×slot_size ring
    // (via mapBulkBuffer); AsyncWriteSequential copies the caller's
    // buffer into a slot and submits via submitBulkWriteAsync, which
    // dispatches the pwrite on the helper side with bounded
    // parallelism. WaitForPendingWrites blocks until all in-flight
    // writes have completed.
    void SetMaxWriteSizeHint(std::size_t bytes) override;

    // Zero-copy seam: returns a provider that backs RingBuffer slots with the
    // helper's shared-memory bulk ring so the producer fills device-write
    // memory directly. Returns nullptr unless the device is open (a zero-copy
    // ring needs an established write session), in which case the caller uses
    // a heap provider. The returned provider falls back to heap internally if
    // the ring can't be mapped, so the write always proceeds.
    std::shared_ptr<WriteBufferProvider> CreateWriteBufferProvider() override;

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
    friend class XpcWriteBufferProvider;

    // Map a shared-memory ring of `count` × `slotSize` bytes for zero-copy
    // writes and hand back per-slot base pointers. Replaces any copy-ring
    // mapping established by SetAsyncQueueDepth. Returns false (and maps
    // nothing) if there's no open session, no XPC backend, the geometry
    // exceeds the helper's bulk-buffer cap, or mapping fails — the caller
    // then uses heap-backed slots. Used by XpcWriteBufferProvider.
    bool adoptZeroCopyRing(std::size_t count, std::size_t slotSize,
                           std::vector<void*>& outSlots);
    // Release the zero-copy ring mapping (idempotent). Used by the provider
    // and by Close().
    void releaseZeroCopyRing();

    struct State;
    std::unique_ptr<State> state_;
};

} // namespace rpi_imager
