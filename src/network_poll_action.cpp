/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "network_poll_action.h"

namespace rpi_net {

PollAction planPollAction(bool hasConnectivity, bool wasOnline, bool haveOsList)
{
    if (hasConnectivity && !wasOnline && !haveOsList)
        return PollAction::ComeOnlineAndFetch;
    if (hasConnectivity && !wasOnline)
        return PollAction::ComeOnline;
    if (!hasConnectivity && wasOnline)
        return PollAction::GoOffline;
    if (!hasConnectivity && !haveOsList)
        return PollAction::ReportUnavailable;
    return PollAction::Nothing;
}

} // namespace rpi_net
