#ifndef WLANCREDENTIALS_H
#define WLANCREDENTIALS_H

/*
 * Interface for wlan credential detection
 * Use WlanCredentials::instance() to get platform
 * specific implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2023 Raspberry Pi Ltd
 */

#include <QByteArray>

class WlanCredentials
{
public:
    static WlanCredentials *instance();
    virtual QByteArray getSSID() = 0;
    virtual QByteArray getPSK() = 0;

protected:
    static WlanCredentials *_instance;
};

#endif // WLANCREDENTIALS_H
