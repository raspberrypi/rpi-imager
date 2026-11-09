// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Linux memfd bulk-write plane (§6, phase 2).
//
// The macOS helper passes a Mach port over NSXPC; Windows duplicates a file-
// mapping HANDLE into the client. On Linux the helper creates an anonymous
// memfd (memfd_create), maps it, and passes the fd to the client with
// SCM_RIGHTS over the Unix-domain control socket. protobuf cannot express an
// fd, so it travels out-of-band alongside the WIRE_MAP_BULK_BUFFER reply -
// exactly like the Windows section_handle (§14.3 analogue).
//
// Linux-only translation unit: CMake compiles it only when
// RPI_IMAGER_ENABLE_LINUX_HELPER is set.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rpi_imager::privileged::wire {

class LinuxSharedMemory {
public:
    LinuxSharedMemory() = default;
    ~LinuxSharedMemory();

    LinuxSharedMemory(const LinuxSharedMemory&) = delete;
    LinuxSharedMemory& operator=(const LinuxSharedMemory&) = delete;

    // Helper side: memfd_create + ftruncate + mmap a writable region.
    bool createOwned(std::size_t size_bytes);

    // Returns the memfd fd for SCM_RIGHTS transfer. Valid only after
    // createOwned(); remains owned by this object until release().
    int ownedFd() const { return fd_; }

    // Client side: mmap an fd received via SCM_RIGHTS (takes ownership).
    bool adoptFromFd(int fd, std::size_t size_bytes);

    void release();

    void*       base() const { return base_; }
    std::size_t size() const { return size_; }
    bool        valid() const { return base_ != nullptr; }

private:
    int         fd_   = -1;
    void*       base_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace rpi_imager::privileged::wire
