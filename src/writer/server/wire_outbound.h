// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Thread-safe helper->client frame writer (WireServerMessage envelope).

#pragma once

#include "proto/imager.pb.h"

#include <mutex>
#include <string>

namespace rpi_imager::writer {

class WireOutbound {
public:
#if defined(_WIN32)
    explicit WireOutbound(void* pipe_handle);
#else
    explicit WireOutbound(int fd);
#endif

    bool sendResponse(const rpi_imager::privileged::proto::WireResponse& response,
                      int pass_fd = -1);
    bool sendEvent(const rpi_imager::privileged::proto::WireEvent& event);

private:
    bool sendFramePayload(const std::string& frame, int pass_fd);

#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int handle_ = -1;
#endif
    std::mutex write_mutex_;
};

} // namespace rpi_imager::writer
