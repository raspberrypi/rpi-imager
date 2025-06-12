#ifndef DRIVEFORMATTHREAD_H
#define DRIVEFORMATTHREAD_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QThread>
#include <cstdint>

class DriveFormatThread : public QThread
{
    Q_OBJECT
public:
    DriveFormatThread(const QByteArray &device, QObject *parent = nullptr);
    virtual ~DriveFormatThread();
    virtual void run();

signals:
    void success();
    void error(QString msg);

protected:
    QByteArray _device;
    std::uint64_t getDeviceSize(const QByteArray &device);
};

#endif // DRIVEFORMATTHREAD_H
