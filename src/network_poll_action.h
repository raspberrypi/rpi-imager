/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * What a poll of the network should make the application do.
 *
 * isOnline() runs on a one-second timer for as long as Imager is open. Each
 * tick it asks the platform whether there is connectivity and then decides
 * between four things: fetch the OS list, note that the network came back,
 * note that it went away, or tell the UI there is nothing to show. The
 * arithmetic is small and the consequences are not:
 */

#ifndef NETWORK_POLL_ACTION_H
#define NETWORK_POLL_ACTION_H

namespace rpi_net {

enum class PollAction {
    // Nothing changed that anyone needs to hear about.
    Nothing,
    // Connectivity arrived and there is still no OS list: fetch it, and say
    // so, because the screen is currently showing the offline state.
    ComeOnlineAndFetch,
    // Connectivity arrived and a list is already in hand. Worth recording,
    // not worth another download.
    ComeOnline,
    // Connectivity went away.
    GoOffline,
    // Still no network, and still nothing to show. The UI needs telling so
    // it can offer Retry rather than an empty list.
    ReportUnavailable,
};

PollAction planPollAction(bool hasConnectivity, bool wasOnline, bool haveOsList);

} // namespace rpi_net

#endif // NETWORK_POLL_ACTION_H
