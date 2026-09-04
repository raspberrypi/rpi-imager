/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 *
 * See imagesizeparser.h. Moved out of ImageWriter's _parse*File() members,
 * which wrote straight into _extrLen and so could only be reached by
 * standing up the whole QML backend.
 */

#include "imagesizeparser.h"

#include <QDebug>
#include <QFile>

#include <archive.h>
#include <archive_entry.h>
#include <lzma.h>
#define ZSTD_STATIC_LINKING_ONLY  // for ZSTD_findDecompressedSize
#include <zstd.h>

namespace imagesize {

quint64 parseXz(const QString &path)
{
    QFile f(path);
    lzma_stream_flags opts = { 0 };

    if (f.size() <= LZMA_STREAM_HEADER_SIZE || !f.open(QIODevice::ReadOnly))
        return 0;

    quint64 extrLen = 0;

    f.seek(f.size() - LZMA_STREAM_HEADER_SIZE);
    QByteArray footer = f.read(LZMA_STREAM_HEADER_SIZE);
    lzma_ret ret = lzma_stream_footer_decode(&opts, (const uint8_t *) footer.constData());

    if (ret == LZMA_OK && opts.backward_size < 1000000
        && opts.backward_size < (quint64) (f.size() - LZMA_STREAM_HEADER_SIZE))
    {
        f.seek(f.size() - LZMA_STREAM_HEADER_SIZE - opts.backward_size);
        QByteArray buf = f.read(opts.backward_size + LZMA_STREAM_HEADER_SIZE);
        lzma_index *idx;
        uint64_t memlimit = UINT64_MAX;
        size_t pos = 0;

        ret = lzma_index_buffer_decode(&idx, &memlimit, NULL,
                                       (const uint8_t *) buf.constData(), &pos, buf.size());
        if (ret == LZMA_OK)
        {
            extrLen = lzma_index_uncompressed_size(idx);
            qDebug() << "Parsed .xz file. Uncompressed size:" << extrLen;
        }
        else
        {
            qDebug() << "Unable to parse index of .xz file";
        }
        lzma_index_end(idx, NULL);
    }
    else
    {
        qDebug() << "Unable to parse footer of .xz file";
    }

    f.close();
    return extrLen;
}

quint64 parseGz(const QString &path)
{
    QFile f(path);

    // Gzip trailer format (last 8 bytes):
    // - CRC32 (4 bytes, little-endian)
    // - ISIZE (4 bytes, little-endian) - original file size modulo 2^32
    //
    // ISIZE is only 32 bits so this is a best-effort estimate for capacity
    // checks.  The caller owns the _extractSizeKnown flag that controls
    // whether the UI trusts this value for progress display.
    const qint64 GZIP_TRAILER_SIZE = 8;

    if (f.size() <= GZIP_TRAILER_SIZE || !f.open(QIODevice::ReadOnly))
    {
        qDebug() << "Unable to open .gz file for parsing";
        return 0;
    }

    quint64 extrLen = 0;

    f.seek(f.size() - 4);  // Seek to ISIZE field (last 4 bytes)
    QByteArray isizeData = f.read(4);

    if (isizeData.size() == 4)
    {
        // ISIZE is stored as little-endian 32-bit unsigned integer
        quint32 isize = static_cast<quint8>(isizeData[0]) |
                       (static_cast<quint8>(isizeData[1]) << 8) |
                       (static_cast<quint8>(isizeData[2]) << 16) |
                       (static_cast<quint8>(isizeData[3]) << 24);

        extrLen = isize;

        // Handle files larger than 4GB where ISIZE wraps around
        // If the uncompressed size appears smaller than the compressed size,
        // the original file was likely > 4GB. This is a heuristic for storage
        // space checks but NOT reliable for progress calculation.
        qint64 compressedSize = f.size();
        while (extrLen < static_cast<quint64>(compressedSize))
        {
            extrLen += Q_UINT64_C(0x100000000);  // Add 4GB
        }

        qDebug() << "Parsed .gz file. Estimated uncompressed size:" << extrLen
                 << "(ISIZE field:" << isize << ") - size unreliable for progress";
    }
    else
    {
        qDebug() << "Unable to read ISIZE from .gz file";
    }

    f.close();
    return extrLen;
}

quint64 parseZstd(const QString &path)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
    {
        qDebug() << "Unable to open .zst file for parsing";
        return 0;
    }

    // ZSTD_findDecompressedSize() iterates through all concatenated frames
    // to compute the total decompressed size. It requires the full compressed
    // data in memory, but this is acceptable for custom file size estimates.
    QByteArray data = f.readAll();
    f.close();

    if (data.isEmpty())
    {
        qDebug() << "Empty .zst file";
        return 0;
    }

    unsigned long long fcs = ZSTD_findDecompressedSize(data.constData(), data.size());

    // The failure sentinels are (0ULL - 2) and (0ULL - 1), not 0, so they have to
    // be tested by name: comparing against 0 alone lets ZSTD_CONTENTSIZE_UNKNOWN
    // through as a size of ULLONG_MAX, and startWrite() then rejects a perfectly
    // good local image with "Storage capacity is not large enough".
    if (fcs == ZSTD_CONTENTSIZE_ERROR)
    {
        qDebug() << "Unable to parse .zst file (invalid or truncated frames)";
        return 0;
    }

    if (fcs == ZSTD_CONTENTSIZE_UNKNOWN)
    {
        // Size not recorded in the frame headers (streaming-compressed input).
        // Leave the size unknown and let progress fall back to the download size.
        qDebug() << "Parsed .zst file. Uncompressed size: unknown (FCS not present)";
        return 0;
    }

    if (fcs == 0)
    {
        qDebug() << "Unable to determine decompressed size of .zst file";
        return 0;
    }

    qDebug() << "Parsed .zst file. Uncompressed size:" << fcs;
    return fcs;
}

ArchiveInfo parseArchive(const QString &path)
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    QByteArray fn = path.toLatin1();
    ArchiveInfo info;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_filename(a, fn.data(), 10240) == ARCHIVE_OK)
    {
        while ( (archive_read_next_header(a, &entry)) == ARCHIVE_OK)
        {
            if (archive_entry_size(entry) > 0)
            {
                info.uncompressedSize += archive_entry_size(entry);
                info.fileCount++;
            }
        }
    }

    archive_read_free(a);

    qDebug() << "Parsed archive containing" << info.fileCount
             << "files, uncompressed size:" << info.uncompressedSize;
    return info;
}

} // namespace imagesize
