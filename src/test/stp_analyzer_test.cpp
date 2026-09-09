/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Unit tests for StpAnalyzer.
 *
 * The embedded (kiosk) build listens for spanning-tree BPDUs on the wired
 * interface so it can warn that getting an IP address will take a while --
 * a switch with STP enabled holds a port in listening/learning for the
 * forward delay, typically fifteen seconds twice over, and the imager
 * otherwise looks like it has simply failed to reach the network.
 *
 * Nothing here needs a real network. The analyser reads its packet from a
 * file descriptor, so a socketpair carrying a hand-built BPDU exercises the
 * decision exactly as a switch would.
 */

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "linux/stpanalyzer.h"

#include <QCoreApplication>
#include <QSocketNotifier>

#include "signal_log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// The 802.2 LLC service access point that says "this is a BPDU".
constexpr uint8_t kLsapBpdu = 0x42;

// A Configuration BPDU as it appears on the wire: a 14-byte Ethernet
// header, three bytes of 802.2 LLC, then the STP fields. 52 bytes in all.
// Built by hand rather than from the struct in the .cpp, so the layout the
// analyser expects is pinned rather than assumed.
std::vector<uint8_t> makeBpdu(uint16_t forwardDelayRaw,
                              uint8_t dsap = kLsapBpdu,
                              uint8_t ssap = kLsapBpdu,
                              uint16_t protocol = 0)
{
    std::vector<uint8_t> f(52, 0);

    // Ethernet: the BPDU multicast group, an arbitrary source, and a length
    // rather than an ethertype (which is what makes it an 802.2 frame).
    const std::array<uint8_t, 6> dst{0x01, 0x80, 0xC2, 0x00, 0x00, 0x00};
    std::memcpy(f.data(), dst.data(), dst.size());
    const std::array<uint8_t, 6> src{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::memcpy(f.data() + 6, src.data(), src.size());
    f[12] = 0x00;
    f[13] = 0x26;   // length: 38 bytes of LLC + BPDU

    f[14] = dsap;
    f[15] = ssap;
    f[16] = 0x03;   // unnumbered information

    std::memcpy(f.data() + 17, &protocol, sizeof(protocol));
    f[19] = 0x00;   // protocol version
    f[20] = 0x00;   // Configuration BPDU
    f[21] = 0x00;   // flags

    // The timers live at the end. forward delay is the last field.
    std::memcpy(f.data() + 50, &forwardDelayRaw, sizeof(forwardDelayRaw));
    return f;
}

// A switch reports its timers in 1/256 of a second, big-endian, so a whole
// number of seconds has the seconds in the first byte and zero in the
// second.
uint16_t forwardDelaySeconds(uint8_t seconds)
{
    uint16_t raw = 0;
    auto *p = reinterpret_cast<uint8_t *>(&raw);
    p[0] = seconds;
    p[1] = 0;
    return raw;
}

// An analyser reading from a socket pair the test writes into, standing in
// for the packet socket it would open on a real interface.
class WiredAnalyser : public StpAnalyzer
{
public:
    explicit WiredAnalyser(int minForwardDelay = 5, bool withNotifier = true)
        : StpAnalyzer(minForwardDelay)
    {
        int fds[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) != 0)
            return;
        _switchEnd = fds[0];
        _s = fds[1];
        if (withNotifier) {
            _qsn = new QSocketNotifier(_s, QSocketNotifier::Read, this);
            _qsn->setEnabled(false);
        }
    }

    ~WiredAnalyser() override
    {
        if (_switchEnd != -1)
            ::close(_switchEnd);
    }

    bool ready() const { return _switchEnd != -1 && _s != -1; }

    // Put a frame on the wire and let the analyser read it, as the socket
    // notifier would.
    void deliver(const std::vector<uint8_t> &frame)
    {
        REQUIRE(::write(_switchEnd, frame.data(), frame.size())
                == static_cast<ssize_t>(frame.size()));
        onPacket(QSocketDescriptor(_s), QSocketNotifier::Read);
    }

    bool stillListening() const { return _s != -1; }
    int descriptor() const { return _s; }

private:
    int _switchEnd = -1;
};

// Can this process open a packet socket at all? Without CAP_NET_RAW it
// cannot, and startListening() then stops at the socket() call rather than
// reaching the state under test.
bool canOpenPacketSocket()
{
    int s = ::socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_2));
    if (s < 0)
        return false;
    ::close(s);
    return true;
}

// Exposes the descriptor so a test can see whether the socket was let go.
class ObservableAnalyser : public StpAnalyzer
{
public:
    int descriptor() const { return _s; }
};

} // namespace

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("A switch holding the port down for long enough is reported",
          "[stp]")
{
    // Fifteen seconds is the default forward delay, and it is paid twice --
    // half a minute before the board has an address. Saying so is the
    // difference between a slow start and an apparent failure.
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    analyser.deliver(makeBpdu(forwardDelaySeconds(15)));

    CHECK(detected.count() == 1);
}

TEST_CASE("A switch that forwards almost at once is not worth warning about",
          "[stp]")
{
    // Below the threshold there is nothing to tell the user; a warning here
    // would appear on every managed switch with fast forwarding on.
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    analyser.deliver(makeBpdu(forwardDelaySeconds(4)));

    CHECK(detected.count() == 0);
    // Answered either way: the question does not need asking again.
    CHECK_FALSE(analyser.stillListening());
}

TEST_CASE("The first BPDU settles it and the socket is let go", "[stp]")
{
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    analyser.deliver(makeBpdu(forwardDelaySeconds(15)));
    REQUIRE(detected.count() == 1);
    CHECK_FALSE(analyser.stillListening());

    // A second frame cannot arrive, because there is nothing left listening
    // for it -- and if one did, it must not be reported twice.
    CHECK(detected.count() == 1);
}

TEST_CASE("A frame that is not a BPDU is left alone", "[stp]")
{
    // 0xAA is SNAP: an IPv6 neighbour advertisement, an LLDP frame, anything
    // else that happens to be 802.2. Reading STP timers out of one would
    // produce a warning from whatever bytes were in those positions.
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    analyser.deliver(makeBpdu(forwardDelaySeconds(15), 0xAA, 0xAA));

    CHECK(detected.count() == 0);
    // Still listening: this was not the frame it is waiting for.
    CHECK(analyser.stillListening());
}

TEST_CASE("A BPDU-looking frame of another protocol is left alone", "[stp]")
{
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    analyser.deliver(makeBpdu(forwardDelaySeconds(15), kLsapBpdu, kLsapBpdu,
                              /*protocol=*/0x1234));

    CHECK(detected.count() == 0);
    CHECK(analyser.stillListening());
}

TEST_CASE("A frame too short to be a BPDU is left alone", "[stp]")
{
    // A runt, or the tail of something else. The timers would be read from
    // whatever the buffer happened to hold.
    WiredAnalyser analyser;
    REQUIRE(analyser.ready());
    rpi_test::SignalLog detected(&analyser, &StpAnalyzer::detected);

    auto runt = makeBpdu(forwardDelaySeconds(15));
    runt.resize(20);
    analyser.deliver(runt);

    CHECK(detected.count() == 0);
    CHECK(analyser.stillListening());
}

TEST_CASE("Stopping a listener that was never wired up does not crash",
          "[stp]")
{
    // This is the state startListening() leaves behind when the interface
    // named is not on the machine: a socket, and no notifier watching it.
    // The destructor runs stopListening() over exactly that, so a board
    // whose wired port is not called eth0 took the crash on the way out.
    WiredAnalyser analyser(5, /*withNotifier=*/false);
    REQUIRE(analyser.ready());
    const int fd = analyser.descriptor();

    CHECK_NOTHROW(analyser.stopListening());

    CHECK_FALSE(analyser.stillListening());
    // And the socket went with it rather than being held for the life of
    // the application.
    CHECK(::fcntl(fd, F_GETFD) == -1);
}

TEST_CASE("An interface that is not there leaves no socket behind", "[stp]")
{
    // The whole of the above, through the real entry point. Reaching the
    // failure needs the packet socket to open first, which needs
    // CAP_NET_RAW; without it startListening() stops one step earlier and
    // there is nothing to see.
    if (!canOpenPacketSocket())
        SKIP("no CAP_NET_RAW, so a packet socket cannot be opened");

    ObservableAnalyser analyser;
    analyser.startListening("rpi-imager-no-such-if0");

    CHECK(analyser.descriptor() == -1);
    // Destruction runs stopListening() over whatever is left.
    CHECK_NOTHROW(analyser.stopListening());
}
