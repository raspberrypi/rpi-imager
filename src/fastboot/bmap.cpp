/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bmap.h"

#include <QXmlStreamReader>
#include <algorithm>
#include <cstring>
#include <limits>

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
            // Checked, like the ranges below. toULongLong() answers zero for
            // anything it cannot read, and a block size of zero turns every
            // block number into byte offset zero.
            bool ok = false;
            _blockSize = reader.readElementText().toULongLong(&ok);
            if (!ok || _blockSize == 0) {
                if (errorMsg)
                    *errorMsg = "bmap: unreadable BlockSize";
                _ranges.clear();
                return false;
            }
        } else if (name == u"BlocksCount") {
            bool ok = false;
            _blockCount = reader.readElementText().toULongLong(&ok);
            if (!ok) {
                if (errorMsg)
                    *errorMsg = "bmap: unreadable BlocksCount";
                _ranges.clear();
                return false;
            }
        } else if (name == u"MappedBlocksCount") {
            // Unchecked, unlike the two above, and deliberately: nothing acts
            // on this figure -- it reaches a debug line -- so a bmap whose
            // other counts are sound stays usable. The conversion is
            // all-or-nothing, so an unreadable one is nought either way.
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
            //
            // Both conversions are checked. toULongLong() answers zero for
            // anything it cannot read, so a damaged range became block 0 --
            // and a damaged *end* became one, leaving begin past end. A
            // range like that is stepped over by both lookups, the encoder
            // is told those blocks are unmapped, and it writes nothing where
            // their data should have gone. That is the hole in the card the
            // truncation check below exists to prevent, arrived at by
            // another road.
            auto text = reader.readElementText();
            auto parts = text.split('-');

            bool beginOk = false;
            bool endOk = true;
            range.begin = parts[0].trimmed().toULongLong(&beginOk);
            if (parts.size() > 1) {
                const quint64 last = parts[1].trimmed().toULongLong(&endOk);
                range.end = last + 1;   // convert inclusive to exclusive
            } else {
                range.end = range.begin + 1;
            }

            if (!beginOk || !endOk || range.begin >= range.end) {
                if (errorMsg)
                    *errorMsg = "bmap: unreadable block range \""
                                + text.toStdString() + "\"";
                _ranges.clear();
                return false;
            }

            _ranges.push_back(std::move(range));
        }
    }

    // A premature end of document is treated as an error here, unlike in
    // incremental parsing where it just means "feed me more". parse() is
    // handed the whole document at once, so hitting the end early means it
    // was truncated -- most likely a bmap download that was cut short.
    //
    // Accepting it silently is the dangerous option: the ranges past the
    // truncation point are missing, every block in them looks unmapped, and
    // the encoder emits DONT_CARE for data that should have been written.
    // The card comes back with holes in it and nothing reports a problem.
    if (reader.hasError()) {
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

    // Both lookups assume the ranges do not overlap: isMapped() binary
    // searches for the last range beginning at or before the block and asks
    // only that one, while isMappedSequential() walks to the first whose end
    // is past it. Given two ranges covering the same block they answer
    // differently, and a bmap saying a block is mapped twice says nothing a
    // bmap is for.
    for (size_t i = 1; i < _ranges.size(); ++i) {
        if (_ranges[i].begin < _ranges[i - 1].end) {
            if (errorMsg)
                *errorMsg = "bmap: overlapping block ranges";
            _ranges.clear();
            return false;
        }
    }

    // The format handed to the device has 32-bit fields, and serialize()
    // wrote them with a plain cast. A block number past four billion came
    // out as a different block, so the device was told to write somebody
    // else's data there and to leave the real blocks alone -- with nothing
    // said. Refused here instead, where there is still somewhere to say it.
    constexpr quint64 kMaxWireBlock = std::numeric_limits<uint32_t>::max();
    if (_blockSize > kMaxWireBlock || _ranges.size() > kMaxWireBlock) {
        if (errorMsg)
            *errorMsg = "bmap: does not fit the device's block map format";
        _ranges.clear();
        return false;
    }
    for (const auto &r : _ranges) {
        // end is exclusive here and inclusive on the wire, so it is the
        // block before it that has to fit.
        if (r.begin > kMaxWireBlock || (r.end - 1) > kMaxWireBlock) {
            if (errorMsg)
                *errorMsg = "bmap: block number past what the device's format holds";
            _ranges.clear();
            return false;
        }
    }

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
