/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "driveformatthread.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/mountutils/src/mountutils.hpp"
#include <regex>
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <QCoreApplication>

#ifdef Q_OS_LINUX
#include "linux/udisks2api.h"
#include <unistd.h>
#endif

DriveFormatThread::DriveFormatThread(const QByteArray &device, QObject *parent)
    : QThread(parent), _device(device)
{

}

DriveFormatThread::~DriveFormatThread()
{
    wait();
}

void DriveFormatThread::run()
{
#ifdef Q_OS_WIN
    std::regex windriveregex("\\\\\\\\.\\\\PHYSICALDRIVE([0-9]+)", std::regex_constants::icase);
    std::cmatch m;

    if (std::regex_match(_device.constData(), m, windriveregex))
    {
        QByteArray nr = QByteArray::fromStdString(m[1]);

        qDebug() << "Formatting Windows drive #" << nr << "(" << _device << ")";

        QProcess proc;
        QByteArray diskpartCmds =
                "select disk "+nr+"\r\n"
                "clean\r\n"
                "create partition primary\r\n"
                "select partition 1\r\n"
                "set id=0e\r\n"
                "assign\r\n";
        proc.start("diskpart");
        proc.waitForStarted();
        proc.write(diskpartCmds);
        proc.closeWriteChannel();
        proc.waitForFinished();

        QByteArray output = proc.readAllStandardError();
        qDebug() << output;
        qDebug() << "Done running diskpart. Exit status code =" << proc.exitCode();

        if (proc.exitCode())
        {
            emit error(tr("Error partitioning: %1").arg(QString(output)));
        }
        else
        {
            auto l = Drivelist::ListStorageDevices();
            QByteArray devlower = _device.toLower();
            for (auto i : l)
            {
                if (QByteArray::fromStdString(i.device).toLower() == devlower && i.mountpoints.size() == 1)
                {
                    QByteArray driveLetter = QByteArray::fromStdString(i.mountpoints.front());
                    if (driveLetter.endsWith("\\"))
                        driveLetter.chop(1);
                    qDebug() << "Drive letter of device:" << driveLetter;

                    QProcess f32format;
                    QStringList args;
                    args << "-y" << driveLetter;
                    f32format.start(QCoreApplication::applicationDirPath()+"/fat32format.exe", args);
                    if (!f32format.waitForStarted())
                    {
                        emit error(tr("Error starting fat32format"));
                        return;
                    }

                    //f32format.write("y\r\n");
                    f32format.closeWriteChannel();
                    f32format.waitForFinished(120000);

                    if (f32format.exitStatus() || f32format.exitCode())
                    {
                        emit error(tr("Error running fat32format: %1").arg(QString(f32format.readAll())));
                    }
                    else
                    {
                        emit success();
                    }
                    return;
                }
            }

            emit error(tr("Error determining new drive letter"));
        }
    }
    else
    {
        emit error(tr("Invalid device: %1").arg(QString(_device)));
    }
#elif defined(Q_OS_DARWIN)
    QProcess proc;
    QStringList args;
    args << "eraseDisk" << "FAT32" << "SDCARD" << "MBRFormat" << _device;
    proc.start("diskutil", args);
    proc.waitForFinished();

    QByteArray output = proc.readAllStandardError();
    qDebug() << args;
    qDebug() << "diskutil output:" << output;

    if (proc.exitCode())
    {
        emit error(tr("Error partitioning: %1").arg(QString(output)));
    }
    else
    {
        emit success();
    }

#elif defined(Q_OS_LINUX)

    if (::access(_device.constData(), W_OK) != 0)
    {
        /* Not running as root, try to outsource formatting to udisks2 */

#ifndef QT_NO_DBUS
        UDisks2Api udisks2;
        if (udisks2.formatDrive(_device))
        {
            emit success();
        }
        else
        {
#endif
            emit error(tr("Error formatting (through udisks2)"));
#ifndef QT_NO_DBUS
        }
#endif

        return;
    }


    QProcess proc;
    QByteArray partitionTable;
    QStringList args;
    QByteArray fatpartition = _device;
    partitionTable = "8192,,0E\n"
            "0,0\n"
            "0,0\n"
            "0,0\n";
    args << "-uS" << _device;
    if (isdigit(fatpartition.at(fatpartition.length()-1)))
        fatpartition += "p1";
    else
        fatpartition += "1";

    unmount_disk(_device);
    proc.setProcessChannelMode(proc.MergedChannels);
    proc.start("sfdisk", args);
    if (!proc.waitForStarted())
    {
        emit error(tr("Error starting sfdisk"));
        return;
    }
    proc.write(partitionTable);
    proc.closeWriteChannel();
    proc.waitForFinished();
    QByteArray output = proc.readAll();
    qDebug() << "sfdisk:" << output;

    if (proc.exitCode())
    {
        emit error(tr("Error partitioning: %1").arg(QString(output)));
        return;
    }

    proc.execute("partprobe", QStringList() );
    for (int tries = 0; tries < 30; tries++)
    {
        if (QFile::exists(fatpartition))
            break;

        QThread::msleep(100);
    }
    if (!QFile::exists(fatpartition))
    {
        emit error(tr("Partitioning did not create expected FAT partition %1").arg(QString(fatpartition)));
        return;
    }

    args.clear();
    args << fatpartition;
    proc.start("mkfs.fat", args);
    if (!proc.waitForStarted())
    {
        emit error(tr("Error starting mkfs.fat"));
        return;
    }

    proc.waitForFinished();
    output = proc.readAll();
    qDebug() << "mkfs.fat:" << output;

    if (proc.exitCode())
    {
        emit error(tr("Error running mkfs.fat: %1").arg(QString(output)));
        return;
    }

    emit success();

#else
    emit error(tr("Formatting not implemented for this platform"));
#endif
}
