/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "networkmanagerapi.h"
#include <QDBusMetaType>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

typedef QMap<QString, QVariantMap> VariantMapMap;
Q_DECLARE_METATYPE(VariantMapMap)

NetworkManagerApi::NetworkManagerApi(QObject *parent)
    : QObject{parent}
{

}

QString NetworkManagerApi::getPSK()
{
    qDBusRegisterMetaType<VariantMapMap>();
    QDBusInterface nm("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                      "org.freedesktop.NetworkManager", QDBusConnection::systemBus());

    if (!nm.isValid())
    {
        qDebug() << "NetworkManager not available";
        return QString();
    }

    const auto activeConnections = nm.property("ActiveConnections").value<QList<QDBusObjectPath>>();
    for (const QDBusObjectPath &activeConnection : activeConnections)
    {
        QString activeConnectionPath = activeConnection.path();
        if (activeConnectionPath.isEmpty())
            continue;
        QDBusInterface ac("org.freedesktop.NetworkManager", activeConnectionPath,
                          "org.freedesktop.NetworkManager.Connection.Active", QDBusConnection::systemBus());
        if (!ac.isValid())
            continue;
        QString settingsPath = ac.property("Connection").value<QDBusObjectPath>().path();
        if (settingsPath.isEmpty())
            continue;
        QDBusInterface settings("org.freedesktop.NetworkManager", settingsPath,
                          "org.freedesktop.NetworkManager.Settings.Connection", QDBusConnection::systemBus());
        if (!settings.isValid())
            continue;
        QDBusReply<VariantMapMap> reply = settings.call("GetSecrets", "802-11-wireless-security");
        if (!reply.isValid())
            continue;
        QVariantMap secrets = reply.value().value("802-11-wireless-security");
        if (secrets.contains("psk"))
            return secrets.value("psk").toString();
    }

    return QString();
}
