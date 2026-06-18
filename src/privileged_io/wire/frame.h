// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Length-prefixed framing for the named-pipe transport (§14.3).
//
// A named pipe is a byte stream; NSXPC framed messages for us on macOS, so
// the Windows backend has to delimit messages itself. Each frame is a 4-byte
// little-endian unsigned length N followed by N bytes of serialized protobuf
// (a proto::WireRequest on the client->helper direction, proto::WireResponse
// on the reply). Bulk write payloads never travel here - they live in the
// shared-memory plane (§6, §14.3).
//
// This codec is deliberately Qt-free and protobuf-free (it operates on
// already-serialized byte strings) so it can be shared verbatim between the
// unprivileged client and the Qt-free helper, and unit-tested on any
// platform. See doc/privileged-helper-plan.md §14.3.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rpi_imager::privileged::wire {

// The 4-byte little-endian length header that precedes every frame payload.
inline constexpr std::size_t kFrameHeaderBytes = 4;

// Upper bound on a single frame payload. The control plane only ever carries
// small structured messages (metadata, drive lists, stats); anything large is
// the bulk plane's job. A cap bounds memory growth against a peer that sends a
// bogus length prefix - the reader drops the connection rather than allocate.
inline constexpr std::uint32_t kMaxFrameBytes = 16u * 1024u * 1024u;

// Returns `payload` prefixed with its 4-byte little-endian length. The caller
// is responsible for ensuring `payload.size() <= kMaxFrameBytes`.
std::string encodeFrame(const std::string& payload);

// Streaming de-framer. Feed bytes as they arrive off the pipe via append();
// drain complete frame payloads via next(). Holding partial frames between
// reads is the whole point - pipe reads don't respect message boundaries.
class FrameAccumulator {
public:
    // Appends `len` bytes of freshly-read pipe data to the internal buffer.
    void append(const char* data, std::size_t len);

    // Extracts the next complete frame payload into `out`. Returns true if a
    // full frame was available (and consumes it from the buffer). Returns
    // false if more bytes are needed. If the advertised length exceeds
    // kMaxFrameBytes, sets `oversize = true` and returns false; the caller
    // must treat that as a protocol violation and close the connection.
    bool next(std::string& out, bool& oversize);

    // Bytes currently buffered (for diagnostics / back-pressure heuristics).
    std::size_t buffered() const { return buf_.size(); }

private:
    std::string buf_;
};

} // namespace rpi_imager::privileged::wire
