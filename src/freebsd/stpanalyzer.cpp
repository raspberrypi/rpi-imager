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
#include <net/ethernet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/bpf.h>
#include <net/if.h>
#include <QSocketNotifier>
#include <QtEndian>

struct stp_packet {
    /* Ethernet header */
    struct ether_header eth;
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
}

void StpAnalyzer::startListening(const QByteArray &ifname)
{
}

void StpAnalyzer::stopListening()
{
}

void StpAnalyzer::onPacket(QSocketDescriptor socket, QSocketNotifier::Type type)
{
}
