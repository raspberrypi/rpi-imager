/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "networkmanagerapi.h"
#include <QDebug>
#include <QNetworkInterface>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/wireless.h>

#ifndef QT_NO_DBUS
#include <QDBusMetaType>
#include <QDBusInterface>
#include <QDBusReply>
#endif

typedef QMap<QString, QVariantMap> VariantMapMap;
Q_DECLARE_METATYPE(VariantMapMap)

NetworkManagerApi::NetworkManagerApi()
{
#ifndef QT_NO_DBUS
    qDBusRegisterMetaType<VariantMapMap>();
#endif
}

QByteArray NetworkManagerApi::getSSID()
{
    QByteArray ssid;
    const auto interfaces = QNetworkInterface::allInterfaces();

    for (const auto &interface : interfaces)
    {
        if (interface.type() == interface.Wifi)
        {
            ssid = _getSSIDofInterface(interface.name().toLatin1());
            if (!ssid.isEmpty())
                break;
        }
    }

    return ssid;
}

QByteArray NetworkManagerApi::_getSSIDofInterface(const QByteArray &iface)
{
    char ssid[IW_ESSID_MAX_SIZE+1] = {0};
    struct iwreq iw = {0};
    int s = ::socket(AF_INET, SOCK_DGRAM, 0);

    if (s == -1)
        return QByteArray();

    strncpy(iw.ifr_ifrn.ifrn_name, iface.data(), sizeof(iw.ifr_ifrn.ifrn_name));
    iw.u.essid.pointer = ssid;
    iw.u.essid.length = IW_ESSID_MAX_SIZE;
    ::ioctl(s, SIOCGIWESSID, &iw);
    ::close(s);

    return QByteArray(ssid);
}

QByteArray NetworkManagerApi::getPSK()
{
#ifndef QT_NO_DBUS
    QDBusInterface nm("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                      "org.freedesktop.NetworkManager", QDBusConnection::systemBus());

    if (!nm.isValid())
    {
        qDebug() << "NetworkManager not available";
        return QByteArray();
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
            return secrets.value("psk").toByteArray();
    }
#endif

    return QByteArray();
}

WlanCredentials *WlanCredentials::_instance = NULL;
WlanCredentials *WlanCredentials::instance()
{
    if (!_instance)
        _instance = new NetworkManagerApi();

    return _instance;
}
