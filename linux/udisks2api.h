#ifndef UDISKS2API_H
#define UDISKS2API_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include <QObject>
#include <QFile>

class UDisks2Api : public QObject
{
    Q_OBJECT
public:
    explicit UDisks2Api(QObject *parent = nullptr);
    int authOpen(const QString &device, const QString &mode = "rw");
    bool formatDrive(const QString &device, bool mountAfterwards = true);
    QString mountDevice(const QString &device);
    void unmountDrive(const QString &device);

protected:
    QString _resolveDevice(const QString &device);
    void _unmountDrive(const QString &driveDbusPath);

signals:

public slots:
};

#endif // UDISKS2API_H
