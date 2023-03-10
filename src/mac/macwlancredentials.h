#ifndef MACWLANCREDENTIALS_H
#define MACWLANCREDENTIALS_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include "wlancredentials.h"

class MacWlanCredentials : public WlanCredentials
{
public:
    virtual QByteArray getSSID();
    virtual QByteArray getPSK();

protected:
    QByteArray _ssid;
};

#endif // MACWLANCREDENTIALS_H
