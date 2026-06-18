// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "win_shared_memory.h"

namespace rpi_imager::privileged::wire {

WinSharedMemory::~WinSharedMemory() {
    release();
}

bool WinSharedMemory::createOwned(std::size_t size_bytes) {
    release();
    if (size_bytes == 0) {
        return false;
    }

    const DWORD size_hi = static_cast<DWORD>(
        (static_cast<std::uint64_t>(size_bytes) >> 32) & 0xFFFFFFFFull);
    const DWORD size_lo = static_cast<DWORD>(
        static_cast<std::uint64_t>(size_bytes) & 0xFFFFFFFFull);

    // INVALID_HANDLE_VALUE => backed by the system paging file (anonymous).
    // No name: the client never opens it by name, only via a duplicated
    // handle, so there is no namespace squatting surface.
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                  nullptr,
                                  PAGE_READWRITE,
                                  size_hi,
                                  size_lo,
                                  nullptr);
    if (mapping_ == nullptr) {
        return false;
    }

    base_ = MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, size_bytes);
    if (base_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    size_ = size_bytes;
    return true;
}

bool WinSharedMemory::duplicateInto(DWORD client_pid,
                                    std::uint64_t& out_handle_value) const {
    if (mapping_ == nullptr || client_pid == 0) {
        return false;
    }

    HANDLE client_proc = OpenProcess(PROCESS_DUP_HANDLE, FALSE, client_pid);
    if (client_proc == nullptr) {
        return false;
    }

    HANDLE dup = nullptr;
    const BOOL ok = DuplicateHandle(GetCurrentProcess(),
                                    mapping_,
                                    client_proc,
                                    &dup,
                                    0,
                                    FALSE,
                                    DUPLICATE_SAME_ACCESS);
    CloseHandle(client_proc);
    if (!ok) {
        return false;
    }

    out_handle_value = reinterpret_cast<std::uint64_t>(dup);
    return true;
}

bool WinSharedMemory::adoptFromHandle(std::uint64_t handle_value,
                                      std::size_t size_bytes) {
    release();
    if (handle_value == 0 || size_bytes == 0) {
        return false;
    }

    mapping_ = reinterpret_cast<HANDLE>(handle_value);
    base_ = MapViewOfFile(mapping_, FILE_MAP_WRITE, 0, 0, size_bytes);
    if (base_ == nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    size_ = size_bytes;
    return true;
}

void WinSharedMemory::release() {
    if (base_ != nullptr) {
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    size_ = 0;
}

} // namespace rpi_imager::privileged::wire
