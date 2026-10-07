// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "win_pipe_stream.h"

namespace rpi_imager::privileged::wire {

namespace {

// Per thread, as the contract asks: two threads can fail writes at once, and
// unlocking a mutex on the way out is free to overwrite GetLastError().
thread_local DWORD t_last_write_error = 0;

DWORD clampChunk(std::size_t n) {
    return static_cast<DWORD>(n > 0x7fffffffu ? 0x7fffffffu : n);
}

bool failWrite() {
    t_last_write_error = GetLastError();
    return false;
}

} // namespace

HANDLE openPipeClient(const std::wstring& name) {
    return CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
}

HANDLE createPipeServer(const std::wstring& name, SECURITY_ATTRIBUTES* sa) {
    return CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,                       // a single client (the launching GUI)
        64 * 1024, 64 * 1024,
        0,
        sa);
}

bool connectPipeServer(HANDLE pipe) {
    // An overlapped handle may not be connected with a null OVERLAPPED.
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        return false;
    }
    bool connected = false;
    if (ConnectNamedPipe(pipe, &ov)) {
        connected = true;
    } else {
        switch (GetLastError()) {
        case ERROR_PIPE_CONNECTED:   // the client arrived before we asked
            connected = true;
            break;
        case ERROR_IO_PENDING: {
            DWORD unused = 0;
            connected = GetOverlappedResult(pipe, &ov, &unused, TRUE) != FALSE;
            break;
        }
        default:
            break;
        }
    }
    CloseHandle(ov.hEvent);
    return connected;
}

WinPipeStream::WinPipeStream(HANDLE pipe)
    : pipe_(pipe),
      read_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      write_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      cancel_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

WinPipeStream::~WinPipeStream() {
    for (HANDLE h : {read_event_, write_event_, cancel_event_}) {
        if (h != nullptr) {
            CloseHandle(h);
        }
    }
}

bool WinPipeStream::write(const char* data, std::size_t len, int pass_fd) {
    if (pass_fd >= 0) {
        // Handles cross by DuplicateHandle, out of band (win_shared_memory.h).
        SetLastError(ERROR_NOT_SUPPORTED);
        return failWrite();
    }
    std::lock_guard<std::mutex> lk(write_mutex_);
    if (write_event_ == nullptr) {
        SetLastError(ERROR_INVALID_HANDLE);
        return failWrite();
    }
    std::size_t off = 0;
    while (off < len) {
        OVERLAPPED ov{};
        ov.hEvent = write_event_;
        if (!WriteFile(pipe_, data + off, clampChunk(len - off), nullptr, &ov)
            && GetLastError() != ERROR_IO_PENDING) {
            return failWrite();
        }
        DWORD wrote = 0;
        if (!GetOverlappedResult(pipe_, &ov, &wrote, TRUE)) {
            return failWrite();
        }
        if (wrote == 0) {
            SetLastError(ERROR_BROKEN_PIPE);
            return failWrite();
        }
        off += wrote;
    }
    return true;
}

StreamChunk WinPipeStream::read(char* buf, std::size_t cap) {
    StreamChunk out;
    std::lock_guard<std::mutex> lk(read_mutex_);
    if (cancelled_.load() || read_event_ == nullptr || cancel_event_ == nullptr) {
        return out;
    }
    OVERLAPPED ov{};
    ov.hEvent = read_event_;
    if (!ReadFile(pipe_, buf, clampChunk(cap), nullptr, &ov)
        && GetLastError() != ERROR_IO_PENDING) {
        return out;
    }
    // Waiting on the cancel event as well closes the gap between checking
    // cancelled_ above and the read being issued: a cancel() in that gap
    // still wakes this wait, where CancelIoEx alone would have missed it.
    HANDLE waits[] = {read_event_, cancel_event_};
    const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (woke != WAIT_OBJECT_0) {
        (void)CancelIoEx(pipe_, &ov);
    }
    // Retire the operation even when cancelling: until it completes, the
    // kernel may still write into buf and ov.
    DWORD got = 0;
    const BOOL done = GetOverlappedResult(pipe_, &ov, &got, TRUE);
    if (!done || got == 0 || woke != WAIT_OBJECT_0) {
        return out;
    }
    out.len = got;
    out.ok = true;
    return out;
}

void WinPipeStream::cancel() {
    cancelled_.store(true);
    if (cancel_event_ != nullptr) {
        SetEvent(cancel_event_);
    }
    // A write blocked on a full pipe has no cancel event to wait on.
    (void)CancelIoEx(pipe_, nullptr);
}

int WinPipeStream::lastError() const {
    return static_cast<int>(t_last_write_error);
}

} // namespace rpi_imager::privileged::wire
