// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Unix-domain socket helpers for the Linux privileged-helper control plane.
// Framed protobuf messages use wire/frame.h; bulk bytes use memfd (§6).
//
// Linux-only translation unit: CMake compiles it only when
// RPI_IMAGER_ENABLE_LINUX_HELPER is set.

#pragma once

#include <cstddef>
#include <string>

namespace rpi_imager::privileged::wire {

// Writes the full buffer, looping over partial writes.
bool writeAll(int fd, const char* data, std::size_t len);

// Reads until `len` bytes are available or EOF/error.
bool readAll(int fd, char* data, std::size_t len);

// Sends a framed payload, optionally passing `pass_fd` (>= 0) via SCM_RIGHTS.
// When pass_fd < 0 no ancillary data is attached.
bool sendFrame(int fd, const std::string& frame, int pass_fd = -1);

// Receives one framed payload into `out_frame`. If the peer attached an fd via
// SCM_RIGHTS, `out_pass_fd` is set (otherwise -1). Uses recvmsg in a loop
// until the frame bytes are complete.
bool recvFrame(int fd, std::string& out_frame, int& out_pass_fd);

} // namespace rpi_imager::privileged::wire
