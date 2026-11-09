// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Named-pipe DuplexStream for the Windows helper and its client (§14.3).
//
// Windows serialises synchronous I/O on a handle, so a write queued behind a
// blocked read waits for it. The stream therefore needs a handle opened for
// overlapped I/O, and the functions below are the only way the pipe is
// opened, so that cannot be forgotten at one end.
//
// Windows-only translation unit: built only on Windows when
// RPI_IMAGER_ENABLE_WINDOWS_HELPER is set (CMake-gated), so it carries no
// platform #ifdef of its own.

#pragma once

#include "duplex_stream.h"

#include <atomic>
#include <mutex>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rpi_imager::privileged::wire {

// Client end. INVALID_HANDLE_VALUE on failure, with GetLastError() as
// CreateFileW leaves it (ERROR_FILE_NOT_FOUND before the server exists,
// ERROR_PIPE_BUSY while it is taken).
HANDLE openPipeClient(const std::wstring& name);

// Server end: one instance, byte mode, local clients only.
HANDLE createPipeServer(const std::wstring& name, SECURITY_ATTRIBUTES* sa);

// Waits for a client to connect to a pipe from createPipeServer().
bool connectPipeServer(HANDLE pipe);

// Does not own the handle; the caller closes it after the stream is done.
class WinPipeStream final : public DuplexStream {
public:
    explicit WinPipeStream(HANDLE pipe);
    ~WinPipeStream() override;

    WinPipeStream(const WinPipeStream&) = delete;
    WinPipeStream& operator=(const WinPipeStream&) = delete;

    bool write(const char* data, std::size_t len, int pass_fd) override;
    StreamChunk read(char* buf, std::size_t cap) override;
    void cancel() override;
    int lastError() const override;

private:
    HANDLE pipe_;
    HANDLE read_event_;
    HANDLE write_event_;
    HANDLE cancel_event_;
    std::atomic<bool> cancelled_{false};
    std::mutex read_mutex_;
    std::mutex write_mutex_;
};

} // namespace rpi_imager::privileged::wire
