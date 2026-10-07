// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Byte stream between the client and the privileged helper, as the duplex
// transport needs it. Each platform supplies one in its own translation unit
// (win_pipe_stream, linux_socket_stream), so the transport above carries no
// platform #ifdef.
//
// The contract a transport relies on, and that a synchronous Windows pipe
// handle does not give: a write must complete while another thread is
// blocked in read(). The reader waits for the peer, the peer waits for the
// write, and a stream that serialises the two deadlocks on the first RPC.

#pragma once

#include <cstddef>

namespace rpi_imager::privileged::wire {

struct StreamChunk {
    std::size_t len = 0;
    int ancillary_fd = -1;   // a descriptor the peer attached, where supported
    bool ok = false;         // false on end of stream, error or cancel()
};

class DuplexStream {
public:
    virtual ~DuplexStream() = default;

    // Writes all of `len` bytes. Concurrent writes never interleave, and a
    // write never waits behind a read. `pass_fd` (>= 0) attaches a descriptor
    // on transports that can carry one; elsewhere it makes the write fail.
    virtual bool write(const char* data, std::size_t len, int pass_fd) = 0;

    // Blocks until at least one byte arrives. One reader at a time.
    virtual StreamChunk read(char* buf, std::size_t cap) = 0;

    // Makes a blocked read(), and every later one, return ok == false, so a
    // reader thread can be joined. Safe from any thread; idempotent.
    virtual void cancel() = 0;

    // The OS error code behind the calling thread's last failed write().
    virtual int lastError() const = 0;
};

} // namespace rpi_imager::privileged::wire
