// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Thread-safe helper->client frame writer (WireServerMessage envelope).

#pragma once

#include "proto/imager.pb.h"
#include "wire/duplex_stream.h"

#include <string>

namespace rpi_imager::writer {

class WireOutbound {
public:
    // The stream must outlive this writer. Responses, drive events and async
    // completions arrive from different threads; the stream keeps whole
    // frames from interleaving and lets them go out while a read is pending.
    explicit WireOutbound(rpi_imager::privileged::wire::DuplexStream& stream);

    bool sendResponse(const rpi_imager::privileged::proto::WireResponse& response,
                      int pass_fd = -1);
    bool sendEvent(const rpi_imager::privileged::proto::WireEvent& event);

private:
    rpi_imager::privileged::wire::DuplexStream& stream_;
};

} // namespace rpi_imager::writer
