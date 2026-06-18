// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "wire_outbound.h"

#include "wire/linux_socket_io.h"
#include "wire/server_message.h"

namespace rpi_imager::writer {

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace wire = rpi_imager::privileged::wire;

#if defined(_WIN32)
WireOutbound::WireOutbound(void* pipe_handle) : handle_(pipe_handle) {}
#else
WireOutbound::WireOutbound(int fd) : handle_(fd) {}
#endif

bool WireOutbound::sendFramePayload(const std::string& frame, int pass_fd) {
    std::lock_guard<std::mutex> lk(write_mutex_);
#if defined(_WIN32)
    (void)pass_fd;
    DWORD wrote = 0;
    return WriteFile(static_cast<HANDLE>(handle_), frame.data(),
                     static_cast<DWORD>(frame.size()), &wrote, nullptr)
           && wrote == frame.size();
#else
    return wire::sendFrame(handle_, frame, pass_fd);
#endif
}

bool WireOutbound::sendResponse(
    const rpi_imager::privileged::proto::WireResponse& response, int pass_fd) {
    const std::string frame = wire::encodeServerResponse(response);
    if (frame.empty()) {
        return false;
    }
    return sendFramePayload(frame, pass_fd);
}

bool WireOutbound::sendEvent(const rpi_imager::privileged::proto::WireEvent& event) {
    const std::string frame = wire::encodeServerEvent(event);
    if (frame.empty()) {
        return false;
    }
    return sendFramePayload(frame, -1);
}

} // namespace rpi_imager::writer
