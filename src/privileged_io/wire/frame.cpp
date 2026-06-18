// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "frame.h"

namespace rpi_imager::privileged::wire {

std::string encodeFrame(const std::string& payload) {
    const std::uint32_t n = static_cast<std::uint32_t>(payload.size());
    std::string frame;
    frame.reserve(kFrameHeaderBytes + payload.size());
    // Little-endian length prefix. Explicit byte assembly keeps this
    // endianness-correct regardless of host byte order.
    frame.push_back(static_cast<char>(n & 0xFF));
    frame.push_back(static_cast<char>((n >> 8) & 0xFF));
    frame.push_back(static_cast<char>((n >> 16) & 0xFF));
    frame.push_back(static_cast<char>((n >> 24) & 0xFF));
    frame.append(payload);
    return frame;
}

void FrameAccumulator::append(const char* data, std::size_t len) {
    buf_.append(data, len);
}

bool FrameAccumulator::next(std::string& out, bool& oversize) {
    oversize = false;
    if (buf_.size() < kFrameHeaderBytes) {
        return false;
    }

    const std::uint32_t n =
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buf_[0]))) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buf_[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buf_[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buf_[3])) << 24);

    if (n > kMaxFrameBytes) {
        oversize = true;
        return false;
    }

    if (buf_.size() < kFrameHeaderBytes + n) {
        return false;  // header seen, body still arriving
    }

    out.assign(buf_, kFrameHeaderBytes, n);
    buf_.erase(0, kFrameHeaderBytes + n);
    return true;
}

} // namespace rpi_imager::privileged::wire
