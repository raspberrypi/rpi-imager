// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "wire_outbound.h"

#include "wire/server_message.h"

namespace rpi_imager::writer {

namespace wire = rpi_imager::privileged::wire;

WireOutbound::WireOutbound(wire::DuplexStream& stream) : stream_(stream) {}

bool WireOutbound::sendResponse(
    const rpi_imager::privileged::proto::WireResponse& response, int pass_fd) {
    const std::string frame = wire::encodeServerResponse(response);
    if (frame.empty()) {
        return false;
    }
    return stream_.write(frame.data(), frame.size(), pass_fd);
}

bool WireOutbound::sendEvent(const rpi_imager::privileged::proto::WireEvent& event) {
    const std::string frame = wire::encodeServerEvent(event);
    if (frame.empty()) {
        return false;
    }
    return stream_.write(frame.data(), frame.size(), -1);
}

} // namespace rpi_imager::writer
