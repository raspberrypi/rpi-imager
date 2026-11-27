/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * The block ranges a fastboot write puts on the device, each with the
 * SHA-256 of the bytes sent, for rpi-fastbootd's "oem bmap-verify" to
 * read back and compare.
 */

#ifndef FASTBOOT_WRITE_MANIFEST_H
#define FASTBOOT_WRITE_MANIFEST_H

#include "bmap.h"

#include <QCryptographicHash>

#include <cstdint>
#include <span>
#include <vector>

namespace fastboot {

class WriteManifest {
public:
    // 64 MiB of 4 KiB blocks. The device sends INFO before each range,
    // so this bounds how long it stays silent while hashing.
    static constexpr uint32_t kDefaultMaxRangeBlocks = 16384;

    explicit WriteManifest(uint32_t blockSize = 4096,
                           uint32_t maxRangeBlocks = kDefaultMaxRangeBlocks);

    // Every block of the image, in order. `written` is false for blocks
    // the device is told to skip (DONT_CARE).
    void addBlock(uint64_t index, std::span<const uint8_t> block, bool written);

    // Close the last range. Call once, after the final block.
    void finish();

    const std::vector<BlockRange>& ranges() const { return _ranges; }
    uint64_t writtenBlocks() const { return _writtenBlocks; }
    uint32_t blockSize() const { return _blockSize; }

    // False when a block number does not fit the device's 32-bit format.
    bool representable() const { return _representable; }

    std::vector<uint8_t> serialize() const;

private:
    void closeRange();

    uint32_t _blockSize;
    uint32_t _maxRangeBlocks;
    std::vector<BlockRange> _ranges;
    QCryptographicHash _hash{QCryptographicHash::Sha256};
    bool _open = false;
    uint64_t _begin = 0;
    uint64_t _next = 0;
    uint64_t _writtenBlocks = 0;
    bool _representable = true;
};

} // namespace fastboot

#endif // FASTBOOT_WRITE_MANIFEST_H
