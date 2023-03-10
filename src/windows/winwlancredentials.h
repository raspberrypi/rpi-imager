#ifndef WINWLANCREDENTIALS_H
#define WINWLANCREDENTIALS_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "wlancredentials.h"

class WinWlanCredentials : public WlanCredentials
{
public:
    WinWlanCredentials();
    virtual QByteArray getSSID();
    virtual QByteArray getPSK();

protected:
    QByteArray _ssid, _psk;
};

#endif // WINWLANCREDENTIALS_H
