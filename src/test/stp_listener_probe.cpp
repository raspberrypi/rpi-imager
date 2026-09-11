/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for StpAnalyzer::startListening().
 */

#include "linux/stpanalyzer.h"

#include <QCoreApplication>

#include <cstdio>
#include <cstring>

namespace {

// Exposes the descriptor, so the driver can see both that the socket was
// opened and that it was let go again.
class ObservableAnalyser : public StpAnalyzer
{
public:
    int descriptor() const { return _s; }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <ifname>\n", argv[0]);
        return 2;
    }
    const QByteArray ifname(argv[1]);

    ObservableAnalyser analyser;
    std::printf("BEFORE_FD=%d\n", analyser.descriptor());

    analyser.startListening(ifname);
    const int during = analyser.descriptor();
    std::printf("DURING_FD=%d\n", during);

    // Asking twice must not open a second socket and leak the first: the
    // caller re-arms this whenever the interface list changes.
    analyser.startListening(ifname);
    std::printf("SECOND_FD=%d\n", analyser.descriptor());

    analyser.stopListening();
    std::printf("AFTER_FD=%d\n", analyser.descriptor());

    // And stopping again is not a crash, which is the path taken on the way
    // out when the watch was never armed.
    analyser.stopListening();
    std::printf("SURVIVED=1\n");
    std::fflush(stdout);
    return 0;
}
