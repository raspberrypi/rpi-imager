/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * See imagesizeparser.h. Moved out of ImageWriter's _parse*File() members,
 * which wrote straight into _extrLen and so could only be reached by
 * standing up the whole QML backend.
 */

#include "imagesizeparser.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

#include <archive.h>
#include <archive_entry.h>
#include <lzma.h>
#define ZSTD_STATIC_LINKING_ONLY  // for ZSTD_findDecompressedSize
#include <zstd.h>

#include "archive_kind.h"

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
        lzma_index *idx = nullptr;

        // liblzma's own guard against an index claiming more records than
        // the file could hold, and it was switched off. backward_size is
        // bounded above, but the index inside that buffer still says how
        // many records to allocate: a crafted 60-byte .xz asked for six
        // petabytes. A real index is a few bytes per block, so this is
        // orders of magnitude more than any genuine image needs.
        uint64_t memlimit = kXzIndexMemLimit;
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

        /* Handle files over 4 GB, where ISIZE has wrapped.
         *
         * The signal is weak: a payload looking smaller than the file
         * holding it. That is wrapping, and it is also every gzip of
         * incompressible data, which deflate cannot shrink and the header
         * and trailer add eighteen bytes to. Alone it called thirty-two
         * bytes of text four gigabytes.
         *
         * So it is bounded by what deflate can have produced: zlib's
         * maximum ratio is 1032:1. Large images keep the benefit.
         */
        const quint64 compressedSize = quint64(qMax(qint64(0), f.size()));
        const quint64 maxPayload = compressedSize > 0
                                       ? compressedSize * Q_UINT64_C(1032)
                                       : 0;
        while (extrLen < compressedSize
               && extrLen + Q_UINT64_C(0x100000000) <= maxPayload)
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

// Feeding libarchive from a QFile rather than handing it the path.
//
// archive_read_open_filename takes a narrow path, and on Windows reads it in
// the active code page -- so a name outside that page names a file libarchive
// cannot find, whatever encoding we hand it. QFile::encodeName gives UTF-8,
// which is right everywhere else and wrong there.
//
// A failed open is not reported as a failure by the caller: it reads as an
// archive holding nothing, which loses the "image too big for this card"
// refusal and makes a multi-file archive look like a single image. So an
// image under a Cyrillic or CJK username -- an ordinary thing to have -- was
// written as though it were one file.
//
// QFile opens by wide path on Windows and by bytes elsewhere, so reading
// through it is correct on all three and needs no platform of its own.
namespace {

struct QFileSource {
    QFile file;
    QByteArray buffer;
};

la_ssize_t readFromQFile(struct archive *, void *data, const void **buff)
{
    auto *src = static_cast<QFileSource *>(data);
    const qint64 n = src->file.read(src->buffer.data(), src->buffer.size());
    if (n < 0)
        return -1;
    *buff = src->buffer.constData();
    return static_cast<la_ssize_t>(n);
}

int closeQFile(struct archive *, void *data)
{
    static_cast<QFileSource *>(data)->file.close();
    return ARCHIVE_OK;
}

// Named rather than left null: the file is already open by the time
// libarchive asks, and a null open callback is not accepted by every version.
int openQFile(struct archive *, void *)
{
    return ARCHIVE_OK;
}

// Without this libarchive reads the archive as a stream, and a zip read that
// way reports nought for any entry whose size lives in the trailing data
// descriptor rather than the local header. The file count then comes back as
// zero for an archive that plainly holds something, which is the same wrong
// answer a failed open gives. archive_read_open_filename installs a seek
// callback of its own; reading through a QFile has to install this one.
la_int64_t seekQFile(struct archive *, void *data, la_int64_t offset, int whence)
{
    auto *src = static_cast<QFileSource *>(data);
    qint64 target = 0;
    switch (whence) {
    case SEEK_SET: target = offset; break;
    case SEEK_CUR: target = src->file.pos() + offset; break;
    case SEEK_END: target = src->file.size() + offset; break;
    default: return ARCHIVE_FATAL;
    }
    if (target < 0 || !src->file.seek(target))
        return ARCHIVE_FATAL;
    return src->file.pos();
}

// Opens `path` and attaches it to `a`. False when the file will not open,
// which the callers treat exactly as libarchive refusing the archive.
bool openArchiveFrom(struct archive *a, const QString &path, QFileSource &src)
{
    src.file.setFileName(path);
    src.buffer.resize(10240);
    if (!src.file.open(QIODevice::ReadOnly)) {
        return false;
    }
    archive_read_set_seek_callback(a, seekQFile);
    if (archive_read_open(a, &src, openQFile, readFromQFile, closeQFile) != ARCHIVE_OK) {
        qDebug() << "imagesize: could not read" << path << ":"
                 << archive_error_string(a);
        return false;
    }
    return true;
}

} // namespace

ArchiveInfo parseArchive(const QString &path)
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    ArchiveInfo info;
    QFileSource src;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (openArchiveFrom(a, path, src))
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

SourceFormat probeFormat(const QString &path)
{
    struct archive *a = archive_read_new();
    struct archive_entry *entry;
    SourceFormat out;
    QFileSource src;

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    // format_all excludes raw: without it a plain .img matches nothing.
    archive_read_support_format_raw(a);

    if (openArchiveFrom(a, path, src)
        && archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        out.format = archive_format(a);
        out.filterCode = archive_filter_code(a, 0);
        out.readable = true;
    }

    archive_read_free(a);
    return out;
}

SourceSize measureLocalFile(const QString &path)
{
    SourceSize out;
    const SourceFormat probed = probeFormat(path);

    // Unreadable, or the bytes are the image: either way the write copies the
    // file verbatim, so the file's own size is the answer.
    if (!probed.readable
        || archivekind::bytesAreTheDiskImage(probed.format, probed.filterCode))
    {
        const qint64 onDisk = QFileInfo(path).size();
        out.uncompressedSize = onDisk > 0 ? quint64(onDisk) : 0;
        out.sizeIsReliable = out.uncompressedSize > 0;
        qDebug() << "Sized as a raw image:" << out.uncompressedSize;
        return out;
    }

    if (archivekind::formatIsASingleImage(probed.format))
    {
        // A compressed image. The write emits the whole decompressed stream,
        // so its length is the answer and entry sizes are not -- a .iso.xz
        // must not be measured, or written, as the ISO's file list.
        switch (probed.filterCode)
        {
        case ARCHIVE_FILTER_XZ:
            out.uncompressedSize = parseXz(path);
            out.sizeIsReliable = out.uncompressedSize > 0;
            break;
        case ARCHIVE_FILTER_GZIP:
            // ISIZE is the original size modulo 2^32: good enough to refuse a
            // card that is plainly too small, never a progress divisor.
            out.uncompressedSize = parseGz(path);
            break;
        case ARCHIVE_FILTER_ZSTD:
            out.uncompressedSize = parseZstd(path);
            out.sizeIsReliable = out.uncompressedSize > 0;
            break;
        default:
            qDebug() << "Compressed with filter" << probed.filterCode
                     << "- size unknown";
            break;
        }
        return out;
    }

    const ArchiveInfo info = parseArchive(path);
    out.uncompressedSize = info.uncompressedSize;
    out.fileCount = info.fileCount;
    out.sizeIsReliable = info.uncompressedSize > 0;
    return out;
}

} // namespace imagesize
