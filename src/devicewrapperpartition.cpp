/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperpartition.h"
#include "devicewrapper.h"

DeviceWrapperPartition::DeviceWrapperPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent)
    : QObject{parent}, _dw(dw), _partStart(partStart), _partLen(partLen), _offset(partStart)
{
    _partEnd = _partStart + _partLen;
}

DeviceWrapperPartition::~DeviceWrapperPartition()
{

}

void DeviceWrapperPartition::read(char *data, qint64 size)
{
    if (_offset+size > _partEnd)
    {
        throw std::runtime_error("Error: trying to read beyond partition");
    }

    _dw->pread(data, size, _offset);
    _offset += size;
}

void DeviceWrapperPartition::seek(qint64 pos)
{
    if (pos > _partLen)
    {
        throw std::runtime_error("Error: trying to seek beyond partition");
    }
    _offset = pos+_partStart;
}

qint64 DeviceWrapperPartition::pos() const
{
    return _offset-_partStart;
}

void DeviceWrapperPartition::write(const char *data, qint64 size)
{
    if (_offset+size > _partEnd)
    {
        throw std::runtime_error("Error: trying to write beyond partition");
    }

    _dw->pwrite(data, size, _offset);
    _offset += size;
}
