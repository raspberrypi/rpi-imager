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
    auto c = new QNetworkDiskCache;
    c->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QDir::separator()+"oslistcache");
    /* Only cache images and not the .json */
    //_c->remove(QUrl(OSLIST_URL));

    /* Clear all for now as we do not know any potential subitems_url in advance */
    c->clear();
    _nam = new QNetworkAccessManager;
    _nam->setCache(c);
}

QNetworkAccessManager *NetworkAccessManagerFactory::create(QObject *parent)
{
    if (!_nam->parent() && parent && _nam->thread() == parent->thread())
        _nam->setParent(parent);
    return _nam;
}
