#ifndef NETWORKACCESSMANAGERFACTORY_H
#define NETWORKACCESSMANAGERFACTORY_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QQmlNetworkAccessManagerFactory>

class QNetworkAccessManager;

class NetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    NetworkAccessManagerFactory();
    QNetworkAccessManager *create(QObject *parent) override;

protected:
    QNetworkAccessManager *_nam;
};

#endif // NETWORKACCESSMANAGERFACTORY_H
