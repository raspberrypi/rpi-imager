/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bmap.h"

#include <QXmlStreamReader>
#include <algorithm>
#include <cstring>

namespace fastboot {

// Parse a hex character to its nibble value (0-15), or -1 on error.
static int hexNibble(QChar c)
{
    if (c >= '0' && c <= '9') return c.unicode() - '0';
    if (c >= 'a' && c <= 'f') return c.unicode() - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c.unicode() - 'A' + 10;
    return -1;
}

// Parse a hex string into a byte array.  Returns false if the string is
// not exactly 2*len hex characters.
static bool parseHex(const QString& hex, uint8_t* out, size_t len)
{
    if (static_cast<size_t>(hex.size()) != 2 * len)
        return false;

    for (size_t i = 0; i < len; ++i) {
        int hi = hexNibble(hex[static_cast<int>(2 * i)]);
        int lo = hexNibble(hex[static_cast<int>(2 * i + 1)]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool BlockMap::parse(std::string_view xml, std::string* errorMsg)
{
    _ranges.clear();
    _blockSize = 4096;
    _blockCount = 0;
    _mappedBlockCount = 0;
    _hasChecksums = false;
    _cursor = 0;

    QXmlStreamReader reader(QByteArray::fromRawData(xml.data(),
                            static_cast<int>(xml.size())));

    bool inBlockMap = false;

    while (!reader.atEnd()) {
        auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;

        auto name = reader.name();

        if (name == u"BlockSize") {
            _blockSize = reader.readElementText().toULongLong();
        } else if (name == u"BlocksCount") {
            _blockCount = reader.readElementText().toULongLong();
        } else if (name == u"MappedBlocksCount") {
            _mappedBlockCount = reader.readElementText().toULongLong();
        } else if (name == u"BlockMap") {
            inBlockMap = true;
        } else if (inBlockMap && name == u"Range") {
            BlockRange range{};

            // Parse optional checksum attribute
            auto chksum = reader.attributes().value(u"chksum");
            if (!chksum.isEmpty()) {
                QString hex = chksum.toString();
                if (parseHex(hex, range.sha256.data(), 32)) {
                    range.hasSha256 = true;
                    _hasChecksums = true;
                }
            }

            // Format: "begin-end" (inclusive) or just "begin" (single block)
            auto text = reader.readElementText();
            auto parts = text.split('-');

            range.begin = parts[0].trimmed().toULongLong();
            range.end = (parts.size() > 1)
                ? parts[1].trimmed().toULongLong() + 1  // convert inclusive to exclusive
                : range.begin + 1;

            _ranges.push_back(std::move(range));
        }
    }

    if (reader.hasError() && reader.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
        if (errorMsg)
            *errorMsg = reader.errorString().toStdString();
        _ranges.clear();
        return false;
    }

    // Sort and validate
    std::sort(_ranges.begin(), _ranges.end(),
              [](const BlockRange& a, const BlockRange& b) {
                  return a.begin < b.begin;
              });

    return true;
}

std::vector<uint8_t> BlockMap::serialize() const
{
    size_t payloadSize = sizeof(BmapWireHeader)
                       + _ranges.size() * sizeof(BmapWireRange);
    std::vector<uint8_t> buf(payloadSize, 0);

    // Header
    BmapWireHeader hdr{};
    hdr.magic = BMAP_WIRE_MAGIC;
    hdr.block_size = static_cast<uint32_t>(_blockSize);
    hdr.range_count = static_cast<uint32_t>(_ranges.size());
    hdr.reserved = 0;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    // Ranges — convert from half-open [begin, end) to inclusive [start, end]
    auto* dst = buf.data() + sizeof(BmapWireHeader);
    for (const auto& r : _ranges) {
        BmapWireRange wr{};
        wr.start_block = static_cast<uint32_t>(r.begin);
        wr.end_block = static_cast<uint32_t>(r.end - 1);  // inclusive
        if (r.hasSha256)
            std::memcpy(wr.sha256, r.sha256.data(), 32);
        std::memcpy(dst, &wr, sizeof(wr));
        dst += sizeof(BmapWireRange);
    }

    return buf;
}

bool BlockMap::isMapped(uint64_t idx) const
{
    // Binary search for the range that could contain idx
    auto it = std::upper_bound(_ranges.begin(), _ranges.end(), idx,
        [](uint64_t val, const BlockRange& r) { return val < r.begin; });

    if (it == _ranges.begin())
        return false;

    --it;
    return idx < it->end;
}

} // namespace fastboot
