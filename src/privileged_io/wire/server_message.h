// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Encode/decode helper->client WireServerMessage frames.

#pragma once

#include "proto/imager.pb.h"

#include <string>

namespace rpi_imager::privileged::wire {

namespace proto = rpi_imager::privileged::proto;

std::string encodeServerResponse(const proto::WireResponse& response);
std::string encodeServerEvent(const proto::WireEvent& event);

// Parses a length-prefixed frame payload from the helper. Returns false on
// malformed input. Sets `is_event` when the frame carries a push notification.
bool decodeServerMessage(const std::string& payload,
                         proto::WireResponse& out_response,
                         proto::WireEvent& out_event,
                         bool& is_event);

} // namespace rpi_imager::privileged::wire
