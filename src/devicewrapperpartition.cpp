/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperpartition.h"
#include "devicewrapper.h"

#include <limits>
#include <stdexcept>

DeviceWrapperPartition::DeviceWrapperPartition(DeviceWrapper *dw, quint64 partStart, quint64 partLen, QObject *parent)
    : QObject{parent}, _dw(dw), _partStart(partStart), _partLen(partLen), _offset(partStart)
{
    // Weighed before it is added. read() deliberately rewrites its bounds
    // check as (size > _partEnd - _offset) to avoid wrapping -- and that
    // rewrite is defeated if _partEnd wrapped here, because the subtraction
    // then yields a value near UINT64_MAX and every size passes it. The GPT
    // path can reach that: it bounds StartingLBA and the sector count
    // separately against UINT64_MAX/512 and never their sum. Found by
    // fuzz_parttable, in two UBSan lines a target exited 0 on.
    if (partLen > std::numeric_limits<quint64>::max() - partStart)
        throw std::runtime_error("Partition extends past the addressable range");
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
