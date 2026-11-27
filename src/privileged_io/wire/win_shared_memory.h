// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Windows shared-memory bulk-write plane (§6, §14.3).
//
// The macOS helper hands the client a shared region by passing a Mach
// port / file handle over NSXPC. The Windows equivalent is a file mapping
// (CreateFileMapping over the system paging file) whose HANDLE the helper
// duplicates into the client process with DuplicateHandle. protobuf can't
// express a handle, so - exactly like the macOS shared-memory handle - it
// travels out-of-band alongside the WIRE_MAP_BULK_BUFFER reply.
//
// Ownership model:
//   - The HELPER creates the mapping (createOwned), maps its own view, and
//     duplicates the section HANDLE into the client (duplicateInto).
//   - The CLIENT adopts the duplicated handle (adoptFromHandle) and maps its
//     own view of the same physical pages.
// Both sides MapViewOfFile the same section, so writes the client makes are
// visible to the helper's WriteFile without an intermediate copy.
//
// Windows-only translation unit: built only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set (CMake-gated, like the macOS XPC
// sources), so it carries no platform #ifdef of its own.
// See doc/privileged-helper-plan.md §14.3.

#pragma once

#include <cstddef>
#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rpi_imager::privileged::wire {

class WinSharedMemory {
public:
    WinSharedMemory() = default;
    ~WinSharedMemory();

    WinSharedMemory(const WinSharedMemory&) = delete;
    WinSharedMemory& operator=(const WinSharedMemory&) = delete;

    // Helper side: create a new anonymous section of `size_bytes`, backed by
    // the paging file, and map a writable view. Returns false on failure.
    bool createOwned(std::size_t size_bytes);

    // Helper side: duplicate the section handle into the client process
    // (identified by PID, obtained via GetNamedPipeClientProcessId). On
    // success `out_handle_value` carries the handle value as it is valid in
    // the *target* process; send it to the client in the WIRE_MAP_BULK_BUFFER
    // reply. Returns false on failure.
    bool duplicateInto(DWORD client_pid, std::uint64_t& out_handle_value) const;

    // Client side: adopt a section handle that the helper duplicated into
    // this process, and map a writable view of `size_bytes`. Takes ownership
    // of the handle (closed on release). Returns false on failure.
    bool adoptFromHandle(std::uint64_t handle_value, std::size_t size_bytes);

    // Tear down the view + handle (idempotent).
    void release();

    void*       base() const { return base_; }
    std::size_t size() const { return size_; }
    bool        valid() const { return base_ != nullptr; }

private:
    HANDLE      mapping_ = nullptr;
    void*       base_    = nullptr;
    std::size_t size_    = 0;
};

} // namespace rpi_imager::privileged::wire
