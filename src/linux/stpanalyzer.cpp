/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 *
 * Class to detect if the Spanning-Tree-Protocol
 * is enabled on the Ethernet switch (which can
 * cause a long delay in getting an IP-address)
 */

#include "stpanalyzer.h"
#include <cstdint>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <QSocketNotifier>
#include <QtEndian>

struct stp_packet {
    /* Ethernet header */
    struct ethhdr eth;
    /* 802.2 LLC header */
    uint8_t dsap, ssap, control;
    /* STP fields */
    uint16_t protocol;
    uint8_t  version;
    uint8_t  msgtype;
    uint8_t  flags;
    uint16_t rootpriority;
    unsigned char rootmac[6];
    uint32_t rootpathcost;
    uint16_t bridgepriority;
    unsigned char bridgemac[6];
    uint16_t portid;
    uint16_t msgage;
    uint16_t maxage;
    uint16_t hellotime;
    uint16_t forwarddelay;
} __attribute__((__packed__));

#define LSAP_BDPU  0x42
#define MULTICAST_MAC_BDPU   {0x1, 0x80, 0xC2, 0, 0, 0}
#define MULTICAST_MAC_BDPUPV {0x1, 0, 0x0C, 0xCC, 0xCC, 0xCD}

StpAnalyzer::StpAnalyzer(int onlyReportIfForwardDelayIsAbove, QObject *parent)
    : QObject(parent), _s(-1), _minForwardDelay(onlyReportIfForwardDelayIsAbove), _qsn(nullptr)
{
}

StpAnalyzer::~StpAnalyzer()
{
    stopListening();
}

void StpAnalyzer::startListening(const QByteArray &ifname)
{
    int iface;

    if (_s != -1)
        return; /* Already listening */

    _s = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_802_2));
    if (_s < 0)
    {
        return;
    }

    iface = if_nametoindex(ifname.constData());
    if (!iface) {
        return;
    }

    struct packet_mreq mreq = { iface, PACKET_MR_MULTICAST, ETH_ALEN, MULTICAST_MAC_BDPU };
    setsockopt(_s, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    struct packet_mreq mreq2 = { iface, PACKET_MR_MULTICAST, ETH_ALEN, MULTICAST_MAC_BDPUPV };
    setsockopt(_s, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq2, sizeof(mreq2));

    _qsn = new QSocketNotifier(_s, QSocketNotifier::Read, this);
    connect(_qsn, SIGNAL(activated(QSocketDescriptor,QSocketNotifier::Type)), SLOT(onPacket(QSocketDescriptor,QSocketNotifier::Type)));
}

void StpAnalyzer::stopListening()
{
    if (_s != -1)
    {
        _qsn->setEnabled(false);
        _qsn->deleteLater();
        _qsn = nullptr;
        close(_s);
        _s = -1;
    }
}

void StpAnalyzer::onPacket(QSocketDescriptor socket, QSocketNotifier::Type type)
{
    struct stp_packet packet = { 0 };

    int len = recvfrom(_s, &packet, sizeof(packet), 0, NULL, 0);

    if (len == sizeof(packet)
            && packet.dsap == LSAP_BDPU
            && packet.ssap == LSAP_BDPU
            && packet.protocol == 0)
    {
        /* It is a STP packet */
        int forwardDelay = qFromLittleEndian(packet.forwarddelay);
        if (forwardDelay > _minForwardDelay)
        {
            emit detected();
        }

        /* Only analyze first packet */
        stopListening();
    }
}
