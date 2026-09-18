#ifndef WINWLANCREDENTIALS_H
#define WINWLANCREDENTIALS_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "wlancredentials.h"
#include "secure_bytes.h"

class WinWlanCredentials : public WlanCredentials
{
public:
    WinWlanCredentials();
    ~WinWlanCredentials();
    virtual QByteArray getSSID();
    virtual QByteArray getPSK();
    virtual QByteArray getPSKForSSID(const QByteArray &ssid);

protected:
    QByteArray _ssid;
    // Not a QByteArray. That is implicitly shared, so handing the passphrase
    // to a caller leaves the two holding one buffer -- and the non-const
    // data() a wipe needs detaches, zeroing a fresh copy while the secret
    // stays where it was. This owns its storage and can actually clear it.
    rpi_imager::SecureBytes _psk;
};

#endif // WINWLANCREDENTIALS_H
