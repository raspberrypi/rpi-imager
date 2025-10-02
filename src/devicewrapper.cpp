/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

#include "devicewrapper.h"
#include "devicewrapperblockcacheentry.h"
#include "devicewrapperstructs.h"
#include "devicewrapperfatpartition.h"
#include <QDebug>

DeviceWrapper::DeviceWrapper(rpi_imager::FileOperations *file_ops, QObject *parent)
    : QObject(parent), _dirty(false), _file_ops(file_ops)
{

}

DeviceWrapper::~DeviceWrapper()
{
    sync();
}

void DeviceWrapper::_seekToBlock(quint64 blockNr)
{
    auto result = _file_ops->Seek(blockNr * 4096);
    if (result != rpi_imager::FileError::kSuccess) {
        throw std::runtime_error("Error seeking device");
    }
}

void DeviceWrapper::sync()
{
    if (!_dirty)
        return;

    const auto blockNrs = _blockcache.keys();
    for (auto blockNr : blockNrs)
    {
        if (blockNr == 0)
            continue; /* Save writing first block with MBR for last */

        auto block = _blockcache.value(blockNr);

        if (!block->dirty)
            continue;

        _seekToBlock(blockNr);
        auto result = _file_ops->WriteSequential(reinterpret_cast<const std::uint8_t*>(block->block), 4096);
        if (result != rpi_imager::FileError::kSuccess) {
            throw std::runtime_error("Error writing to device");
        }
        block->dirty = false;
    }

    if (_blockcache.contains(0))
    {
        /* Write first block with MBR */
        auto block = _blockcache.value(0);

        if (block->dirty)
        {
            _seekToBlock(0);
            auto result = _file_ops->WriteSequential(reinterpret_cast<const std::uint8_t*>(block->block), 4096);
            if (result != rpi_imager::FileError::kSuccess) {
                throw std::runtime_error("Error writing MBR to device");
            }
            block->dirty = false;
        }
    }

    _dirty = false;
}

void DeviceWrapper::_readIntoBlockCacheIfNeeded(quint64 offset, quint64 size)
{
    if (!size)
        return;

    quint64 firstBlock = offset/4096;
    quint64 lastBlock = (offset+size)/4096;

    for (auto i = firstBlock; i <= lastBlock; i++)
    {
        if (!_blockcache.contains(i))
        {
            _seekToBlock(i);

            auto cacheEntry = new DeviceWrapperBlockCacheEntry(this);
            std::size_t bytes_read = 0;
            auto result = _file_ops->ReadSequential(reinterpret_cast<std::uint8_t*>(cacheEntry->block), 4096, bytes_read);
            if (result != rpi_imager::FileError::kSuccess || bytes_read != 4096) {
                throw std::runtime_error("Error reading from device");
            }
            _blockcache.insert(i, cacheEntry);
        }
    }
}

void DeviceWrapper::pread(char *buf, quint64 size, quint64 offset)
{
    if (!size)
        return;

    _readIntoBlockCacheIfNeeded(offset, size);
    quint64 firstBlock = offset / 4096;
    quint64 offsetInBlock = offset % 4096;

    for (auto i = firstBlock; size; i++)
    {
        auto block = _blockcache.value(i);
        size_t bytesToCopyFromBlock = qMin(4096-offsetInBlock, size);
        memcpy(buf, block->block + offsetInBlock, bytesToCopyFromBlock);

        buf  += bytesToCopyFromBlock;
        size -= bytesToCopyFromBlock;
        offsetInBlock = 0;
    }
}

void DeviceWrapper::pwrite(const char *buf, quint64 size, quint64 offset)
{
    if (!size)
        return;

    quint64 firstBlock = offset / 4096;
    quint64 offsetInBlock = offset % 4096;

    if (offsetInBlock || size % 4096)
    {
        /* Need to read existing data from disk
           as we will only be replacing a part of a block. */
        _readIntoBlockCacheIfNeeded(offset, size);
    }

    for (auto i = firstBlock; size; i++)
    {
        auto block = _blockcache.value(i, NULL);
        if (!block)
        {
            block = new DeviceWrapperBlockCacheEntry(this);
            _blockcache.insert(i, block);
        }

        block->dirty = true;
        size_t bytesToCopyFromBlock = qMin(4096-offsetInBlock, size);
        memcpy(block->block + offsetInBlock, buf, bytesToCopyFromBlock);

        buf  += bytesToCopyFromBlock;
        size -= bytesToCopyFromBlock;
        offsetInBlock = 0;
    }

    _dirty = true;
}

DeviceWrapperFatPartition *DeviceWrapper::fatPartition(int nr)
{
    if (nr > 4 || nr < 1)
        throw std::runtime_error("Only basic partitions 1-4 supported");

    /* GPT table handling */
    struct gpt_header gpt;
    struct gpt_partition gptpart;
    pread((char *) &gpt, sizeof(gpt), 512);

    if (!strncmp("EFI PART", gpt.Signature, 8) && gpt.MyLBA == 1)
    {
        qDebug() << "Using GPT partition table";
        if (nr > gpt.NumberOfPartitionEntries)
            throw std::runtime_error("Partition does not exist");

        pread((char *) &gptpart, sizeof(gptpart), gpt.PartitionEntryLBA*512 + gpt.SizeOfPartitionEntry*(nr-1));

        return new DeviceWrapperFatPartition(this, gptpart.StartingLBA*512, (gptpart.EndingLBA-gptpart.StartingLBA+1)*512, this);
    }

    /* MBR table handling */
    struct mbr_table mbr;
    pread((char *) &mbr, sizeof(mbr), 0);

    if (mbr.signature[0] != 0x55 || mbr.signature[1] != 0xAA)
        throw std::runtime_error("MBR does not have valid signature");

    if (!mbr.part[nr-1].starting_sector || !mbr.part[nr-1].nr_of_sectors)
        throw std::runtime_error("Partition does not exist");

    return new DeviceWrapperFatPartition(this, mbr.part[nr-1].starting_sector*512, mbr.part[nr-1].nr_of_sectors*512, this);
}

