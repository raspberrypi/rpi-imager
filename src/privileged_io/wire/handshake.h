// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Raspberry Pi Ltd
//
// Shared protocol handshake helpers for wire backends and helper servers.

#pragma once

#include "proto/imager.pb.h"

#include <cstdint>
#include <string>

namespace rpi_imager::privileged::wire {

namespace proto_ns = rpi_imager::privileged::proto;

inline proto_ns::ErrorInfo makeOk() {
    return proto_ns::ErrorInfo();
}

inline proto_ns::ErrorInfo makeFail(proto_ns::ErrorCode code, const std::string& detail) {
    proto_ns::ErrorInfo e;
    e.set_code(code);
    e.set_detail(detail);
    return e;
}

inline proto_ns::ErrorInfo checkProtocolVersion(const std::string& payload,
                                                std::uint32_t expected) {
    proto_ns::ProtocolVersion pv;
    if (!pv.ParseFromString(payload)) {
        return makeFail(proto_ns::ERROR_UNKNOWN, "bad ProtocolVersion payload");
    }
    if (pv.version() != expected) {
        return makeFail(
            proto_ns::ERROR_HELPER_VERSION_MISMATCH,
            "protocol version mismatch: client=" + std::to_string(pv.version()) +
                " helper=" + std::to_string(expected));
    }
    return makeOk();
}

inline void fillReadyHelperStatus(proto_ns::HelperStatus& status,
                                  std::uint32_t protocol_version) {
    status.set_state(proto_ns::HELPER_STATE_INSTALLED_READY);
    status.set_installed_version(std::to_string(protocol_version));
    status.set_client_version(std::to_string(protocol_version));
}

inline void applyClientProtocolVersion(proto_ns::HelperStatus& status,
                                       std::uint32_t client_version) {
    status.set_client_version(std::to_string(client_version));
    if (status.state() == proto_ns::HELPER_STATE_INSTALLED_READY &&
        status.installed_version() != status.client_version()) {
        status.set_state(proto_ns::HELPER_STATE_VERSION_MISMATCH);
    }
}

} // namespace rpi_imager::privileged::wire
