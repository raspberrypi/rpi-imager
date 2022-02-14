/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "../dependencies/drivelist/src/drivelist.hpp"
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

/*
 * Our third-party drivelist module does not provide a C++ implementation
 * for listing drives on Linux (only Javascript)
 * So roll our own for Linux using same function/data structure as the drivelist one
 */

namespace Drivelist
{
    static void _walkStorageChildren(Drivelist::DeviceDescriptor &d, QStringList &labels, QJsonArray &ca)
    {
        for (auto j : ca)
        {
            QJsonObject child = j.toObject();
            QString label = child["label"].toString();
            QString mp = child["mountpoint"].toString();
            if (!label.isEmpty())
            {
                labels.append(label);
            }
            if (!mp.isEmpty())
            {
                d.mountpoints.push_back(mp.toStdString());
                d.mountpointLabels.push_back(label.toStdString());
            }

            QJsonArray subca = child["children"].toArray();
            if (subca.count())
            {
                _walkStorageChildren(d, labels, subca);
            }
        }
    }

    std::vector<Drivelist::DeviceDescriptor> ListStorageDevices()
    {
        std::vector<DeviceDescriptor> deviceList;

        QProcess p;
        QStringList args = { "--bytes", "--json", "--paths", "--output-all" };
        p.start("lsblk", args);
        p.waitForFinished(2000);
        QByteArray output = p.readAll();

        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() || output.isEmpty())
        {
            qDebug() << "Error executing lsblk";
            return deviceList;
        }

        QJsonDocument d = QJsonDocument::fromJson(output);
        QJsonArray a = d.object()["blockdevices"].toArray();
        for (auto i : a)
        {
            DeviceDescriptor d;
            QJsonObject bdev = i.toObject();
            QString name = bdev["kname"].toString();
            QString subsystems = bdev["subsystems"].toString();
            if (name.startsWith("/dev/loop") || name.startsWith("/dev/sr") || name.startsWith("/dev/ram") || name.startsWith("/dev/zram") || name.isEmpty())
                continue;

            d.busType    = bdev["busType"].toString().toStdString();
            d.device     = name.toStdString();
            d.raw        = true;
            d.isVirtual  = subsystems == "block";
            if (bdev["ro"].isBool())
            {
                /* With some lsblk versions it is a bool in others a "0" or "1" string */
                d.isReadOnly = bdev["ro"].toBool();
                d.isRemovable= bdev["rm"].toBool() || bdev["hotplug"].toBool() || d.isVirtual;
            }
            else
            {
                d.isReadOnly = bdev["ro"].toString() == "1";
                d.isRemovable= bdev["rm"].toString() == "1" || bdev["hotplug"].toString() == "1" || d.isVirtual;
            }
            if (bdev["size"].isString())
            {
                d.size       = bdev["size"].toString().toULongLong();
            }
            else
            {
                d.size       = bdev["size"].toDouble();
            }
            d.isSystem   = !d.isRemovable && !d.isVirtual;
            d.isUSB      = subsystems.contains("usb");
            d.isSCSI     = subsystems.contains("scsi") && !d.isUSB;
            d.blockSize  = bdev["phy-sec"].toInt();
            d.logicalBlockSize = bdev["log-sec"].toInt();

            QStringList dp = {
                bdev["label"].toString().trimmed(),
                bdev["vendor"].toString().trimmed(),
                bdev["model"].toString().trimmed()
            };
            if (name == "/dev/mmcblk0")
            {
                dp.removeAll("");
                if (dp.empty())
                    dp.append(QObject::tr("Internal SD card reader"));
            }

            QString mp = bdev["mountpoint"].toString();
            if (!mp.isEmpty())
            {
                d.mountpoints.push_back(mp.toStdString());
                d.mountpointLabels.push_back(bdev["label"].toString().toStdString());
            }
            QStringList labels;
            QJsonArray ca = bdev["children"].toArray();
            _walkStorageChildren(d, labels, ca);

            if (labels.count()) {
                dp.append("("+labels.join(", ")+")");
            }
            dp.removeAll("");
            d.description = dp.join(" ").toStdString();

            /* Mark internal NVMe drives as non-system if not mounted
               anywhere else than under /media */
            if (d.isSystem && subsystems.contains("nvme"))
            {
                bool isMounted = false;
                for (std::string mp : d.mountpoints)
                {
                    if (!QByteArray::fromStdString(mp).startsWith("/media/")) {
                        isMounted = true;
                        break;
                    }
                }
                if (!isMounted)
                {
                    d.isSystem = false;
                }
            }

            deviceList.push_back(d);
        }

        return deviceList;
    }
}
