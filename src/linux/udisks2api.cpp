/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "udisks2api.h"
#include <unistd.h>
#include <fcntl.h>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QThread>

UDisks2Api::UDisks2Api(QObject *parent)
    : QObject(parent)
{
}

int UDisks2Api::authOpen(const QString &device, const QString &mode)
{
    QString devpath = _resolveDevice(device);
    if (devpath.isEmpty())
        return -1;

    QDBusInterface blockdevice("org.freedesktop.UDisks2", devpath,
                               "org.freedesktop.UDisks2.Block", QDBusConnection::systemBus());
    QString drive = blockdevice.property("Drive").value<QDBusObjectPath>().path();
    if (!drive.isEmpty() && drive != "/")
    {
        _unmountDrive(drive);
    }

    // User may need to enter password in authentication dialog so set long timeout
    blockdevice.setTimeout(3600 * 1000);
    QVariantMap options = {{"flags", O_EXCL}};
    QDBusReply<QDBusUnixFileDescriptor> dbusfd = blockdevice.call("OpenDevice", mode, options);

    if (!blockdevice.isValid() || !dbusfd.isValid() || !dbusfd.value().isValid())
        return -1;

    int fd = ::dup(dbusfd.value().fileDescriptor());

    return fd;
}

QString UDisks2Api::_resolveDevice(const QString &device)
{
    QDBusInterface manager("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                           "org.freedesktop.UDisks2.Manager", QDBusConnection::systemBus());
    QVariantMap devspec = {{"path", device}};
    QVariantMap options;

    QDBusReply<QList<QDBusObjectPath>> list = manager.call("ResolveDevice", devspec, options);

    if (!manager.isValid() || !list.isValid() || list.value().isEmpty())
        return QString();

    return list.value().constFirst().path();
}

void UDisks2Api::_unmountDrive(const QString &driveDbusPath)
{
    //qDebug() << "Drive:" << driveDbusPath;

    QDBusInterface manager("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                           "org.freedesktop.UDisks2.Manager", QDBusConnection::systemBus());
    QVariantMap options;
    const QDBusReply<QList<QDBusObjectPath>> list = manager.call("GetBlockDevices", options);

    if (!manager.isValid() || !list.isValid())
        return;

    const auto listValue = list.value();
    for (const auto& devpath : listValue)
    {
        QString devpathStr = devpath.path();

        QDBusInterface blockdevice("org.freedesktop.UDisks2", devpathStr,
                                   "org.freedesktop.UDisks2.Block", QDBusConnection::systemBus());
        QString driveOfDev = blockdevice.property("Drive").value<QDBusObjectPath>().path();
        if (driveOfDev != driveDbusPath)
            continue;

        //qDebug() << "Device:" << devpathStr << "belongs to same drive";
        QDBusInterface filesystem("org.freedesktop.UDisks2", devpathStr,
                                  "org.freedesktop.UDisks2.Filesystem", QDBusConnection::systemBus());

        QDBusReply<void> reply = filesystem.call("Unmount", options);
        if (reply.isValid())
            qDebug() << "Unmounted" << devpathStr << "successfully";
    }
}

bool UDisks2Api::formatDrive(const QString &device, bool mountAfterwards)
{
    QString devpath = _resolveDevice(device);
    if (devpath.isEmpty())
        return false;

    QDBusInterface blockdevice("org.freedesktop.UDisks2", devpath,
                               "org.freedesktop.UDisks2.Block", QDBusConnection::systemBus());

    QString drive = blockdevice.property("Drive").value<QDBusObjectPath>().path();
    if (!drive.isEmpty() && drive != "/")
    {
        _unmountDrive(drive);
    }

    qDebug() << "Repartitioning drive";
    QVariantMap options;
    QDBusReply<void> reply = blockdevice.call("Format", "dos", options);
    if (!reply.isValid())
    {
        qDebug() << "Error repartitioning device:" << reply.error().message();
        return false;
    }

    QVariantMap partOptions, formatOptions;
    QDBusInterface partitiontable("org.freedesktop.UDisks2", devpath,
                               "org.freedesktop.UDisks2.PartitionTable", QDBusConnection::systemBus());

    /* The all-in-one CreatePartitionAndFormat udisks2 method seems to not always
       work properly. Do seperate actions with sleep in between instead */
    qDebug() << "Adding partition";
    QDBusReply<QDBusObjectPath> newpartition = partitiontable.call("CreatePartition", QVariant((qulonglong) 4*1024*1024), QVariant((qulonglong) 0), "0x0e", "", partOptions);
    if (!newpartition.isValid())
    {
        qDebug() << "Error adding partition:" << newpartition.error().message();
        return false;
    }
    qDebug() << "New partition:" << newpartition.value().path();
    QThread::sleep(1);
    if (!drive.isEmpty() && drive != "/")
    {
        /* Unmount one more time, as auto-mount may have tried to mount an old FAT filesystem again if it
         * lives at the same sector in the new partition table as before */
        _unmountDrive(drive);
    }
    qDebug() << "Formatting drive as FAT32";
    QDBusInterface newblockdevice("org.freedesktop.UDisks2", newpartition.value().path(),
                                  "org.freedesktop.UDisks2.Block", QDBusConnection::systemBus());
    newblockdevice.setTimeout(300 * 1000);
    QDBusReply<void> fatformatreply = newblockdevice.call("Format", "vfat", formatOptions);
    if (!fatformatreply.isValid())
    {
        qDebug() << "Error from udisks2 while performing FAT32 format:" << fatformatreply.error().message();
        return false;
    }

    if (mountAfterwards)
    {
        QDBusInterface filesystem("org.freedesktop.UDisks2", newpartition.value().path(),
                                  "org.freedesktop.UDisks2.Filesystem", QDBusConnection::systemBus());
        QVariantMap mountOptions;

        for (int attempt = 0; attempt < 10; attempt++)
        {
            qDebug() << "Mounting partition";
            // User may need to enter password in authentication dialog if non-removable storage, so set long timeout
            filesystem.setTimeout(3600 * 1000);
            QDBusReply<QString> mp = filesystem.call("Mount", mountOptions);

            if (mp.isValid())
            {
                qDebug() << "Mounted new file system at:" << mp;
                return true;
            }
            else
            {
                /* Check if already auto-mounted */
                auto mps = mountPoints(filesystem);
                if (!mps.isEmpty())
                {
                    qDebug() << "Was already auto-mounted at:" << mps;
                    return true;
                }
                else
                {
                    qDebug() << "Error mounting:" << mp.error().message();
                }
            }

            QThread::sleep(1);
        }

        qDebug() << "Failed to mount new file system.";
        return false;
    }

    return true;
}

QString UDisks2Api::mountDevice(const QString &device)
{
    QString devpath = _resolveDevice(device);
    if (devpath.isEmpty())
        return QString();

    QDBusInterface filesystem("org.freedesktop.UDisks2", devpath,
                              "org.freedesktop.UDisks2.Filesystem", QDBusConnection::systemBus());
    QVariantMap mountOptions;

    for (int attempt = 0; attempt < 10; attempt++)
    {
        qDebug() << "Mounting partition";
        // User may need to enter password in authentication dialog if non-removable storage, so set long timeout
        filesystem.setTimeout(3600 * 1000);
        QDBusReply<QString> mp = filesystem.call("Mount", mountOptions);

        if (mp.isValid())
        {
            qDebug() << "Mounted file system at:" << mp;
            return mp;
        }
        else
        {
            /* Check if already auto-mounted */
            auto mps = mountPoints(filesystem);
            if (!mps.isEmpty())
            {
                qDebug() << "Was already auto-mounted at:" << mps;
                return mps.first();
            }
            else
            {
                qDebug() << "Error mounting:" << mp.error().message();
            }
        }

        QThread::sleep(1);
    }

    qDebug() << "Failed to mount file system.";
    return QString();
}

void UDisks2Api::unmountDrive(const QString &device)
{
    QString devpath = _resolveDevice(device);
    if (devpath.isEmpty())
        return;

    _unmountDrive(devpath);
}

QByteArrayList UDisks2Api::mountPoints(const QString &partitionDevice)
{
    QString devpath = _resolveDevice(partitionDevice);
    if (devpath.isEmpty())
        return QByteArrayList();

    QDBusInterface filesystem("org.freedesktop.UDisks2", devpath,
                              "org.freedesktop.UDisks2.Filesystem", QDBusConnection::systemBus());
    return mountPoints(filesystem);
}

QByteArrayList UDisks2Api::mountPoints(const QDBusInterface &filesystem)
{
    QByteArrayList mps;

    QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.UDisks2", filesystem.path(),
                                                      "org.freedesktop.DBus.Properties", "Get");
    QVariantList args = {"org.freedesktop.UDisks2.Filesystem", "MountPoints"};
    msg.setArguments(args);
    QDBusMessage reply = QDBusConnection::systemBus().call(msg);
    const auto replyArgs = reply.arguments();
    for (const auto& arg : replyArgs)
    {
        arg.value<QDBusVariant>().variant().value<QDBusArgument>() >> mps;
    }
    for (auto &str : mps)
    {
        if (!str.isEmpty() && str.back() == '\0')
            str.chop(1);
    }

    return mps;
}
