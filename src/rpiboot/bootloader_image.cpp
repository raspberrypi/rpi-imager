/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "bootloader_image.h"

#include <QFile>
#include <QDebug>

#include <cstring>

namespace rpiboot {

namespace {

// Big-endian 32-bit read/write.  Avoid <bit>/std::byteswap to keep the
// minimum compiler version low.
uint32_t readBe32(const QByteArray &b, int offset)
{
    const auto *p = reinterpret_cast<const uint8_t*>(b.constData() + offset);
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}

void writeBe32(QByteArray &b, int offset, uint32_t v)
{
    auto *p = reinterpret_cast<uint8_t*>(b.data() + offset);
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

constexpr int alignUp8(int x) { return (x + 7) & ~7; }

} // namespace

BootloaderImage::BootloaderImage() = default;
BootloaderImage::~BootloaderImage() = default;

bool BootloaderImage::load(const QString &filename)
{
    QFile f(filename);
    if (!f.open(QIODevice::ReadOnly)) {
        _lastError = "Cannot open EEPROM image: " + filename;
        return false;
    }
    _bytes = f.readAll();
    f.close();

    // The upstream tool accepts 512 KiB or 2 MiB images; accept the same
    // sizes and reject anything else as corruption.
    const int sz = _bytes.size();
    if (sz != 512 * 1024 && sz != 2 * 1024 * 1024) {
        _lastError = QString("Unexpected EEPROM image size %1 bytes "
                             "(expected 512 KiB or 2 MiB)").arg(sz);
        return false;
    }
    return parse();
}

bool BootloaderImage::save(const QString &filename) const
{
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const qint64 wrote = f.write(_bytes);
    f.close();
    return wrote == _bytes.size();
}

bool BootloaderImage::parse()
{
    _sections.clear();
    int offset = 0;
    const int imgSize = _bytes.size();

    while (offset + 8 <= imgSize) {
        const uint32_t magic  = readBe32(_bytes, offset);
        const uint32_t length = readBe32(_bytes, offset + 4);

        if (magic == 0 || magic == 0xffffffff)
            break;  // EOF / unwritten flash

        if ((magic & MAGIC_MASK) != MAGIC) {
            _lastError = QString("EEPROM corrupted at offset %1: magic %2")
                            .arg(offset).arg(magic, 8, 16, QChar('0'));
            return false;
        }

        Section s;
        s.magic  = magic;
        s.offset = offset;
        s.length = int(length);
        if (magic == FILE_MAGIC && offset + 8 + FILENAME_LEN <= imgSize) {
            QByteArray nameRaw = _bytes.mid(offset + 8, FILENAME_LEN);
            // Filenames are NUL-padded; strip trailing NULs.
            int firstNul = nameRaw.indexOf('\0');
            if (firstNul >= 0)
                nameRaw.truncate(firstNul);
            s.filename = QString::fromUtf8(nameRaw);
        }
        _sections.push_back(s);

        // Advance: 8-byte header + length, then 8-byte align.
        const qint64 next = qint64(offset) + 8 + qint64(length);
        if (next > imgSize) {
            _lastError = "EEPROM section overruns end of image";
            return false;
        }
        offset = alignUp8(int(next));
    }
    return true;
}

bool BootloaderImage::findFile(const QString &name,
                               int &outHeaderOffset, int &outLength,
                               bool &outIsLast, int &outNextOffset) const
{
    outHeaderOffset = -1;
    outLength       = -1;
    outIsLast       = false;
    // By default, the modifiable region ends one erase block before the
    // end of the image (the last 4 KiB is bootloader scratch space).
    outNextOffset   = _bytes.size() - ERASE_ALIGN_SIZE;

    int i = 0;
    if (name == QStringLiteral("bootcode.bin")) {
        // Bootcode is always the first section, regardless of magic.
        if (_sections.empty()) {
            return false;
        }
        outHeaderOffset = _sections[0].offset;
        outLength       = _sections[0].length;
    } else {
        for (i = 0; i < int(_sections.size()); ++i) {
            const auto &s = _sections[i];
            if (s.magic == FILE_MAGIC && s.filename == name) {
                outHeaderOffset = s.offset;
                outLength       = s.length;
                outIsLast       = (i == int(_sections.size()) - 1);
                break;
            }
        }
        if (outHeaderOffset < 0)
            return false;
    }

    // Walk past padding sections to find the next "real" section start —
    // that's the upper bound for an updated payload's footprint.
    ++i;
    while (i < int(_sections.size())) {
        if (_sections[i].magic == PAD_MAGIC) {
            ++i;
            continue;
        }
        outNextOffset = _sections[i].offset;
        break;
    }
    return true;
}

QByteArray BootloaderImage::getFile(const QString &name) const
{
    int hdr = 0, len = 0, next = 0;
    bool isLast = false;
    if (!findFile(name, hdr, len, isLast, next))
        return {};

    if (name == QStringLiteral("bootcode.bin")) {
        // bootcode payload starts directly after the 8-byte section header.
        return _bytes.mid(hdr + 8, len);
    }
    // Named-file payload starts after [magic 4][length 4][filename 12][meta 4].
    // The recorded `length` covers filename + 4 metadata bytes + payload, so
    // we subtract the 16 bytes that aren't payload.
    return _bytes.mid(hdr + 4 + FILE_HDR_LEN, len - FILENAME_LEN - 4);
}

bool BootloaderImage::updateFile(const QString &name, const QByteArray &payload)
{
    if (payload.size() > MAX_FILE_SIZE) {
        _lastError = QString("Payload for %1 too large (%2 bytes, max %3)")
                        .arg(name).arg(payload.size()).arg(MAX_FILE_SIZE);
        return false;
    }

    int hdr = 0, oldLen = 0, next = 0;
    bool isLast = false;
    if (!findFile(name, hdr, oldLen, isLast, next)) {
        _lastError = "Section not found: " + name;
        return false;
    }

    const int updateLen = payload.size() + FILE_HDR_LEN;
    if (hdr + updateLen > int(_bytes.size()) - ERASE_ALIGN_SIZE) {
        _lastError = QString("No space for %1: would overrun scratch reserve")
                        .arg(name);
        return false;
    }
    if (hdr + updateLen > next) {
        _lastError = QString("Update of %1 (%2 bytes) doesn't fit in section "
                             "(available %3)").arg(name).arg(updateLen)
                                                .arg(next - hdr);
        return false;
    }

    // Recorded length covers [filename 12 + meta 4 + payload].
    const int newLen = payload.size() + FILENAME_LEN + 4;
    writeBe32(_bytes, hdr + 4, uint32_t(newLen));
    std::memcpy(_bytes.data() + hdr + 4 + FILE_HDR_LEN,
                payload.constData(), payload.size());

    // 0xFF-pad to next 8-byte boundary
    int padStart = hdr + 4 + FILE_HDR_LEN + payload.size();
    while (padStart % 8 != 0) {
        _bytes.data()[padStart++] = char(0xff);
    }

    // Insert a PAD_MAGIC section to consume the remaining space within
    // this erase block, except when this is the last section (where
    // trailing padding bytes are fine and a header would shift offsets
    // for round-trip equality with rpi-eeprom-config).
    //
    // The gap is always a multiple of 8 (both padStart and next are
    // 8-aligned).  Use ">= 8" — matching rpi-eeprom-config — so that an
    // exactly-8-byte gap becomes a zero-length PAD header rather than 8
    // raw 0xff bytes, which parse() would read as 0xffffffff and treat as
    // a premature EOF, silently dropping every following section.
    int padBytes = next - padStart;
    if (padBytes >= 8 && !isLast) {
        padBytes -= 8;
        writeBe32(_bytes, padStart, PAD_MAGIC);
        writeBe32(_bytes, padStart + 4, uint32_t(padBytes));
        padStart += 8;
    }
    for (int i = 0; i < padBytes; ++i) {
        _bytes.data()[padStart + i] = char(0xff);
    }

    // Re-parse so subsequent updates see consistent offsets / filenames.
    return parse();
}

bool BootloaderImage::updateBootcode(const QByteArray &payload)
{
    int hdr = 0, oldLen = 0, next = 0;
    bool isLast = false;
    if (!findFile(QStringLiteral("bootcode.bin"), hdr, oldLen, isLast, next))
        return false;

    // Non-AB images reserve a fixed 128 KiB bootcode region (next must be
    // ≥ 128 KiB).  AB-capable images carry a smaller bootcode inside the
    // 64 KiB read-only section — the bulk bootloader lives in the separate
    // "bootsys" partition file — so the reservation does not apply.
    // Mirrors rpi-eeprom-config's `not self._is_ab_image` guard.
    constexpr int BOOTCODE_RESERVED = 128 * 1024;
    if (!isABImage() && next < BOOTCODE_RESERVED) {
        _lastError = "Bootcode reserved region < 128 KiB";
        return false;
    }
    if (8 + payload.size() > next) {
        _lastError = QString("Signed bootcode (%1 bytes) larger than reserve")
                        .arg(payload.size());
        return false;
    }

    writeBe32(_bytes, hdr + 4, uint32_t(payload.size()));
    std::memcpy(_bytes.data() + hdr + 8, payload.constData(), payload.size());

    int padStart = hdr + 8 + payload.size();
    while (padStart % 8 != 0)
        _bytes.data()[padStart++] = char(0xff);

    // ">= 8" (not "> 8"): an exactly-8-byte gap must become a zero-length
    // PAD header, not 8 raw 0xff bytes that parse() would treat as EOF.
    int padBytes = next - padStart;
    if (padBytes >= 8) {
        padBytes -= 8;
        writeBe32(_bytes, padStart, PAD_MAGIC);
        writeBe32(_bytes, padStart + 4, uint32_t(padBytes));
        padStart += 8;
    }
    for (int i = 0; i < padBytes; ++i) {
        _bytes.data()[padStart + i] = char(0xff);
    }
    return parse();
}

bool BootloaderImage::isABImage() const
{
    for (const auto &s : _sections) {
        if (s.magic == FILE_MAGIC && s.filename == QStringLiteral("bootsys"))
            return true;
    }
    return false;
}

bool BootloaderImage::updateBootsys(const QByteArray &payload)
{
    // bootsys is a FILE_MAGIC file living at the start of the first
    // partition (offset 64 KiB).  Locate it within the first partition,
    // mirroring rpi-eeprom-config's partition-scoped find_file.
    constexpr int partitionEnd = READ_ONLY_SIZE + PARTITION_SIZE;

    int idx = -1;
    for (int i = 0; i < int(_sections.size()); ++i) {
        const auto &s = _sections[i];
        if (s.offset >= READ_ONLY_SIZE && s.offset < partitionEnd &&
            s.magic == FILE_MAGIC && s.filename == QStringLiteral("bootsys")) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        _lastError = "bootsys section not found in first partition";
        return false;
    }

    const int hdr    = _sections[idx].offset;
    const bool isLast = (idx == int(_sections.size()) - 1);

    // next: the first non-padding section after bootsys, capped at the
    // first-partition end and the trailing scratch reserve.
    int next = qMin(int(_bytes.size()) - ERASE_ALIGN_SIZE, partitionEnd);
    for (int i = idx + 1; i < int(_sections.size()); ++i) {
        if (_sections[i].magic == PAD_MAGIC)
            continue;
        next = _sections[i].offset;
        break;
    }

    // bootsys is exempt from MAX_FILE_SIZE; it is bounded only by the
    // partition.  The recorded section length covers [filename + meta +
    // payload], so the on-disk footprint is payload + FILE_HDR_LEN.
    const int updateLen = payload.size() + FILE_HDR_LEN;
    if (hdr + updateLen > int(_bytes.size()) - ERASE_ALIGN_SIZE) {
        _lastError = "No space for bootsys: would overrun scratch reserve";
        return false;
    }
    if (hdr + updateLen > next) {
        _lastError = QString("Signed bootsys (%1 bytes) doesn't fit in "
                             "first partition (available %2)")
                        .arg(updateLen).arg(next - hdr);
        return false;
    }

    const int newLen = payload.size() + FILENAME_LEN + 4;
    writeBe32(_bytes, hdr + 4, uint32_t(newLen));
    std::memcpy(_bytes.data() + hdr + 4 + FILE_HDR_LEN,
                payload.constData(), payload.size());

    // 0xFF-pad to next 8-byte boundary.
    int padStart = hdr + 4 + FILE_HDR_LEN + payload.size();
    while (padStart % 8 != 0)
        _bytes.data()[padStart++] = char(0xff);

    // Insert a PAD_MAGIC section to consume the remaining space up to the
    // next section, unless bootsys is the last section.  ">= 8" (not "> 8")
    // so an exactly-8-byte gap becomes a zero-length PAD header rather than
    // 8 raw 0xff bytes that parse() would treat as a premature EOF.
    int padBytes = next - padStart;
    if (padBytes >= 8 && !isLast) {
        padBytes -= 8;
        writeBe32(_bytes, padStart, PAD_MAGIC);
        writeBe32(_bytes, padStart + 4, uint32_t(padBytes));
        padStart += 8;
    }
    for (int i = 0; i < padBytes; ++i)
        _bytes.data()[padStart + i] = char(0xff);

    return parse();
}

} // namespace rpiboot
