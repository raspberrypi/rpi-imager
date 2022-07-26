#ifndef NETWORKACCESSMANAGERFACTORY_H
#define NETWORKACCESSMANAGERFACTORY_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QQmlNetworkAccessManagerFactory>

class QNetworkDiskCache;

class NetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    NetworkAccessManagerFactory();
    virtual QNetworkAccessManager *create(QObject *parent);

protected:
    int _nr;
};

#endif // NETWORKACCESSMANAGERFACTORY_H
