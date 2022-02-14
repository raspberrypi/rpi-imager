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
{
    _c = new QNetworkDiskCache(this);
    _c->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QDir::separator()+"oslistcache");
    /* Only cache images and not the .json */
    //_c->remove(QUrl(OSLIST_URL));

    /* Clear all for now as we do not know any potential subitems_url in advance */
    _c->clear();
}

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    QNetworkAccessManager *nam = new QNetworkAccessManager(parent);
    nam->setCache(_c);
    return nam;
}
