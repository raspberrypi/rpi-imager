#ifndef DEVICEWRAPPERPARTITION_H
#define DEVICEWRAPPERPARTITION_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <QObject>

class DeviceWrapper;

class DeviceWrapperPartition : public QObject
{
    Q_OBJECT
public:
    explicit DeviceWrapperPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent = nullptr);
    virtual ~DeviceWrapperPartition();
    void read(char *data, qint64 size);
    void seek(qint64 pos);
    qint64 pos() const;
    void write(const char *data, qint64 size);

protected:
    DeviceWrapper *_dw;
    quint64 _partStart, _partLen, _partEnd, _offset;

signals:

};

#endif // DEVICEWRAPPERPARTITION_H
