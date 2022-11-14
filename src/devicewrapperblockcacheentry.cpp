/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapperblockcacheentry.h"
#include <QObject>

DeviceWrapperBlockCacheEntry::DeviceWrapperBlockCacheEntry(QObject *parent, size_t blocksize)
    : QObject(parent), dirty(false)
{
    /* Windows requires buffers to be 4k aligned when reading/writing raw disk devices */
    block = (char *) qMallocAligned(blocksize, 4096);
}

DeviceWrapperBlockCacheEntry::~DeviceWrapperBlockCacheEntry()
{
    qFreeAligned(block);
}
