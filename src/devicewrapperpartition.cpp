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
    // Overflow-safe bounds check: rewrite (offset + size > end) as (size > end - offset)
    // to avoid wrapping past UINT64_MAX when offset is near the limit.
    if (size < 0 || static_cast<quint64>(size) > _partEnd - _offset)
    {
        throw std::runtime_error("Error: trying to read beyond partition");
    }

    _dw->pread(data, size, _offset);
    _offset += size;
}

void DeviceWrapperPartition::seek(qint64 pos)
{
    if (pos < 0 || static_cast<quint64>(pos) > _partLen)
    {
        throw std::runtime_error("Error: trying to seek beyond partition");
    }
    _offset = static_cast<quint64>(pos) + _partStart;
}

qint64 DeviceWrapperPartition::pos() const
{
    return _offset-_partStart;
}

void DeviceWrapperPartition::write(const char *data, qint64 size)
{
    // Overflow-safe bounds check (see read() comment).
    if (size < 0 || static_cast<quint64>(size) > _partEnd - _offset)
    {
        throw std::runtime_error("Error: trying to write beyond partition");
    }

    _dw->pwrite(data, size, _offset);
    _offset += size;
}
