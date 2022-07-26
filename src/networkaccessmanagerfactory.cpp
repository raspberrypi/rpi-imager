/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "networkaccessmanagerfactory.h"
#include "config.h"
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QDebug>

/* Configure caching for files downloaded from Internet by QML (e.g. os_list.json and icons) */
NetworkAccessManagerFactory::NetworkAccessManagerFactory()
    : _nr(0)
{
}

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    QNetworkAccessManager *nam = new QNetworkAccessManager(parent);
    auto c = new QNetworkDiskCache(nam);
    c->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QDir::separator()+"oslistcache"+QString::number(_nr++));
    c->clear();
    nam->setCache(c);
    return nam;
}
