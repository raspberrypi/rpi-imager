/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Probe for NetworkManagerApi::getPSK(), against a NetworkManager of the
 * test's own making.
 *
 * getPSK() reads this machine's wireless passphrase so the wizard can offer
 * to reuse it, and it walks the active connections on the system bus to do
 * it. On a developer machine the real NetworkManager answers, its
 * GetSecrets call is refused for want of privilege, and the walk stops at
 * the first guard -- so the four rejection arms and the success arm had
 * never run, on a function whose job is handing over a credential.
 */

#include "linux/networkmanagerapi.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusMetaType>
#include <QObject>
#include <QVariantMap>
#include <QList>
#include <QMap>

#include <cstdio>
#include <cstring>

using VariantMapMap = QMap<QString, QVariantMap>;

namespace {

// /org/freedesktop/NetworkManager -- only the property the walk reads.
class FakeManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.NetworkManager")
    Q_PROPERTY(QList<QDBusObjectPath> ActiveConnections READ activeConnections)
public:
    explicit FakeManager(QList<QDBusObjectPath> paths) : _paths(std::move(paths)) {}
    QList<QDBusObjectPath> activeConnections() const { return _paths; }
private:
    QList<QDBusObjectPath> _paths;
};

// An active connection, which points at the settings object holding the
// secrets. An empty path here is a connection the walk must skip.
class FakeActive : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.NetworkManager.Connection.Active")
    Q_PROPERTY(QDBusObjectPath Connection READ connection)
public:
    explicit FakeActive(QString settingsPath) : _settings(std::move(settingsPath)) {}
    QDBusObjectPath connection() const { return QDBusObjectPath(_settings); }
private:
    QString _settings;
};

// The settings object. GetSecrets either answers with a passphrase or
// fails, which is what a wired connection or a refused request looks like.
class FakeSettings : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.NetworkManager.Settings.Connection")
public:
    FakeSettings(QString psk, bool refuse) : _psk(std::move(psk)), _refuse(refuse) {}
public slots:
    VariantMapMap GetSecrets(const QString &setting)
    {
        VariantMapMap out;
        if (_refuse || setting != QStringLiteral("802-11-wireless-security"))
            return out;   // an empty answer, as a wired connection gives
        QVariantMap sec;
        if (!_psk.isEmpty())
            sec.insert(QStringLiteral("psk"), _psk);
        out.insert(QStringLiteral("802-11-wireless-security"), sec);
        return out;
    }
private:
    QString _psk;
    bool _refuse;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<VariantMapMap>();

    const QString mode = argc > 1 ? QString::fromUtf8(argv[1]) : QStringLiteral("found");

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        std::fprintf(stderr, "no system bus\n");
        return 2;
    }

    // "nonm" wants the walk to find no NetworkManager at all, so nothing is
    // registered and the probe goes straight to the call.
    if (mode != QStringLiteral("nonm")) {
        if (!bus.registerService(QStringLiteral("org.freedesktop.NetworkManager"))) {
            std::fprintf(stderr, "could not take the NetworkManager name\n");
            return 2;
        }
    }

    QList<QDBusObjectPath> active;
    FakeActive *activeObj = nullptr;
    FakeSettings *settingsObj = nullptr;

    const auto exportActive = [&](const QString &path, const QString &settingsPath) {
        activeObj = new FakeActive(settingsPath);
        bus.registerObject(path, activeObj,
                           QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots);
        active.append(QDBusObjectPath(path));
    };
    const auto exportSettings = [&](const QString &path, const QString &psk, bool refuse) {
        settingsObj = new FakeSettings(psk, refuse);
        bus.registerObject(path, settingsObj,
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    };

    if (mode == QStringLiteral("noactive")) {
        // A connection path naming an object that is not there: the
        // interface will not validate and the walk moves on.
        active.append(QDBusObjectPath(QStringLiteral("/org/freedesktop/NetworkManager/ActiveConnection/absent")));
    } else if (mode == QStringLiteral("nosettings")) {
        // Active, but its Connection property is empty.
        exportActive(QStringLiteral("/ac/1"), QString());
    } else if (mode == QStringLiteral("settingsgone")) {
        // Active, names a settings object, and that object is not exported.
        exportActive(QStringLiteral("/ac/1"), QStringLiteral("/settings/absent"));
    } else if (mode == QStringLiteral("nosecrets")) {
        exportActive(QStringLiteral("/ac/1"), QStringLiteral("/settings/1"));
        exportSettings(QStringLiteral("/settings/1"), QString(), /*refuse=*/false);
    } else if (mode == QStringLiteral("found")) {
        exportActive(QStringLiteral("/ac/1"), QStringLiteral("/settings/1"));
        exportSettings(QStringLiteral("/settings/1"), QStringLiteral("hunter2"), false);
    } else if (mode == QStringLiteral("secondwins")) {
        // The first connection is wired and answers nothing; the second is
        // the wireless one. Stopping at the first would lose the answer.
        exportActive(QStringLiteral("/ac/1"), QStringLiteral("/settings/1"));
        exportSettings(QStringLiteral("/settings/1"), QString(), false);
        auto *second = new FakeActive(QStringLiteral("/settings/2"));
        bus.registerObject(QStringLiteral("/ac/2"), second,
                           QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSlots);
        active.append(QDBusObjectPath(QStringLiteral("/ac/2")));
        auto *s2 = new FakeSettings(QStringLiteral("hunter2"), false);
        bus.registerObject(QStringLiteral("/settings/2"), s2,
                           QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    }

    if (mode != QStringLiteral("nonm")) {
        auto *manager = new FakeManager(active);
        bus.registerObject(QStringLiteral("/org/freedesktop/NetworkManager"), manager,
                           QDBusConnection::ExportAllProperties);
    }

    NetworkManagerApi api;
    const QByteArray psk = api.getPSK();
    std::printf("PSK=%s\n", psk.constData());
    std::fflush(stdout);
    return 0;
}

#include "network_manager_psk_probe.moc"
