/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../wlancredentials.h"

/* Stub implementation for CLI-only builds that don't need network credential detection */
class WlanCredentialsStub : public WlanCredentials
{
public:
    virtual QByteArray getSSID() { return QByteArray(); }
    virtual QByteArray getPSK() { return QByteArray(); }
    virtual QByteArray getPSKForSSID(const QByteArray &ssid) { Q_UNUSED(ssid); return QByteArray(); }
};

WlanCredentials *WlanCredentials::_instance = NULL;
WlanCredentials *WlanCredentials::instance()
{
    if (!_instance)
        _instance = new WlanCredentialsStub();

    return _instance;
}

