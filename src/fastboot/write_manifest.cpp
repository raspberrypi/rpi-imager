/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "write_manifest.h"

#include <cstring>
#include <limits>

namespace fastboot {

WriteManifest::WriteManifest(uint32_t blockSize, uint32_t maxRangeBlocks)
    : _blockSize(blockSize), _maxRangeBlocks(maxRangeBlocks ? maxRangeBlocks : 1)
{
}

void WriteManifest::addBlock(uint64_t index, std::span<const uint8_t> block, bool written)
{
    if (!written) {
        closeRange();
        return;
    }
    if (index > std::numeric_limits<uint32_t>::max())
        _representable = false;

    if (_open && (index != _next || _next - _begin >= _maxRangeBlocks))
        closeRange();
    if (!_open) {
        _open = true;
        _begin = index;
        _hash.reset();
    }
    _hash.addData(QByteArrayView(reinterpret_cast<const char*>(block.data()),
                                 static_cast<qsizetype>(block.size())));
    _next = index + 1;
    ++_writtenBlocks;
}

void WriteManifest::finish()
{
    closeRange();
}

void WriteManifest::closeRange()
{
    if (!_open)
        return;
    BlockRange r{_begin, _next};
    const QByteArray digest = _hash.result();
    std::memcpy(r.sha256.data(), digest.constData(), r.sha256.size());
    r.hasSha256 = true;
    _ranges.push_back(r);
    _open = false;
}

std::vector<uint8_t> WriteManifest::serialize() const
{
    return serializeBlockRanges(_ranges, _blockSize);
}

} // namespace fastboot
