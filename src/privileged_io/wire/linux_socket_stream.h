// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Unix-domain socket DuplexStream for the Linux helper and its client. A
// socket is full duplex already, so this adds no machinery: it is the read,
// write and shutdown the transport used directly, behind the interface.
//
// Linux-only translation unit: CMake compiles it only when
// RPI_IMAGER_ENABLE_LINUX_HELPER is set.

#pragma once

#include "duplex_stream.h"

#include <atomic>
#include <mutex>

namespace rpi_imager::privileged::wire {

// Does not own the descriptor; the caller closes it after the stream is done.
class LinuxSocketStream final : public DuplexStream {
public:
    explicit LinuxSocketStream(int fd);

    LinuxSocketStream(const LinuxSocketStream&) = delete;
    LinuxSocketStream& operator=(const LinuxSocketStream&) = delete;

    bool write(const char* data, std::size_t len, int pass_fd) override;
    StreamChunk read(char* buf, std::size_t cap) override;
    void cancel() override;
    int lastError() const override;

private:
    int fd_;
    std::atomic<bool> cancelled_{false};
    std::mutex write_mutex_;
};

} // namespace rpi_imager::privileged::wire
