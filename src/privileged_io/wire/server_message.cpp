// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd

#include "server_message.h"

#include "frame.h"

namespace rpi_imager::privileged::wire {

namespace {

std::string encodeServerMessage(const proto::WireServerMessage& msg) {
    std::string ser;
    if (!msg.SerializeToString(&ser) || ser.size() > kMaxFrameBytes) {
        return {};
    }
    return encodeFrame(ser);
}

} // namespace

std::string encodeServerResponse(const proto::WireResponse& response) {
    proto::WireServerMessage msg;
    *msg.mutable_response() = response;
    return encodeServerMessage(msg);
}

std::string encodeServerEvent(const proto::WireEvent& event) {
    proto::WireServerMessage msg;
    *msg.mutable_event() = event;
    return encodeServerMessage(msg);
}

bool decodeServerMessage(const std::string& payload,
                         proto::WireResponse& out_response,
                         proto::WireEvent& out_event,
                         bool& is_event) {
    proto::WireServerMessage msg;
    if (!msg.ParseFromString(payload)) {
        return false;
    }
    switch (msg.message_case()) {
        case proto::WireServerMessage::kResponse:
            is_event = false;
            out_response = msg.response();
            return true;
        case proto::WireServerMessage::kEvent:
            is_event = true;
            out_event = msg.event();
            return true;
        default:
            return false;
    }
}

} // namespace rpi_imager::privileged::wire
