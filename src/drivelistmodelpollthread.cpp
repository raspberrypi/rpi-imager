#include "drivelistmodelpollthread.h"
#include <QElapsedTimer>
#include <QDebug>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

DriveListModelPollThread::DriveListModelPollThread(QObject *parent)
    : QThread(parent), _terminate(false)
{
    qRegisterMetaType< std::vector<Drivelist::DeviceDescriptor> >( "std::vector<Drivelist::DeviceDescriptor>" );
}

DriveListModelPollThread::~DriveListModelPollThread()
{
    _terminate = true;
    if (!wait(2000)) {
        terminate();
    }
}

void DriveListModelPollThread::stop()
{
    _terminate = true;
}

void DriveListModelPollThread::start()
{
    _terminate = false;
    QThread::start();
}

void DriveListModelPollThread::run()
{
    QElapsedTimer t1;

    while (!_terminate)
    {
        t1.start();
        emit newDriveList( Drivelist::ListStorageDevices() );
        if (t1.elapsed() > 1000)
            qDebug() << "Enumerating drives took a long time:" << t1.elapsed()/1000.0 << "seconds";
        QThread::sleep(1);
    }
}
