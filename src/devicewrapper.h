#ifndef DEVICEWRAPPER_H
#define DEVICEWRAPPER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include <QObject>
#include <QMap>
#include <memory>
#include "file_operations.h"

class DeviceWrapperBlockCacheEntry;
class DeviceWrapperFatPartition;


class DeviceWrapper : public QObject
{
    Q_OBJECT
public:
    explicit DeviceWrapper(rpi_imager::FileOperations *file_ops, QObject *parent = nullptr);
    virtual ~DeviceWrapper();
    void sync();
    void pwrite(const char *buf, quint64 size, quint64 offset);
    void pread(char *buf, quint64 size, quint64 offset);
    DeviceWrapperFatPartition *fatPartition(int nr);

protected:
    bool _dirty;
    QMap<quint64,DeviceWrapperBlockCacheEntry *> _blockcache;
    rpi_imager::FileOperations *_file_ops;

    void _readIntoBlockCacheIfNeeded(quint64 offset, quint64 size);
    void _seekToBlock(quint64 blockNr);

signals:

};

#endif // DEVICEWRAPPER_H
