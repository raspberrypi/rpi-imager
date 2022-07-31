#ifndef NETWORKMANAGERAPI_H
#define NETWORKMANAGERAPI_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <QObject>

class NetworkManagerApi : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManagerApi(QObject *parent = nullptr);
    QString getPSK();

signals:

};

#endif // NETWORKMANAGERAPI_H
