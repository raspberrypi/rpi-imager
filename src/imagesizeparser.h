/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * Uncompressed-size estimation for the image formats the imager accepts.
 *
 * These answers feed the capacity check in ImageWriter::startWrite(), which
 * refuses to start when the image will not fit on the chosen card. Both kinds
 * of wrong answer are user-visible and neither is obvious from the message:
 * too large and a perfectly good image is rejected as "Storage capacity is
 * not large enough"; too small and the check passes, then the write fails
 * partway through with the card already overwritten.
 */

#ifndef IMAGESIZEPARSER_H
#define IMAGESIZEPARSER_H

#include <QString>
#include <QtGlobal>

namespace imagesize {

// .xz: read the stream footer and index.
quint64 parseXz(const QString &path);

// .gz: the ISIZE trailer, which is the original size modulo 2^32, corrected
// upwards while it reads as smaller than the compressed file.
quint64 parseGz(const QString &path);

// .zst: the frame content size, where "not recorded" is a distinct answer
// from "error" and both must come back as unknown rather than as a size.
quint64 parseZstd(const QString &path);

struct ArchiveInfo {
    quint64 uncompressedSize = 0;
    int fileCount = 0;
};

// Anything libarchive recognises (.zip, .tar, ...): total size of the
// entries, and how many there were.
ArchiveInfo parseArchive(const QString &path);

// What libarchive makes of the first header, so callers can ask what a file
// is rather than what it is called.
struct SourceFormat {
    int format = 0;
    int filterCode = 0;
    bool readable = false;
};

SourceFormat probeFormat(const QString &path);

struct SourceSize {
    quint64 uncompressedSize = 0;  // 0 when unknown
    int fileCount = 0;
    bool sizeIsReliable = false;   // false also bars it as a progress divisor
};

// How many bytes writing this file will put on the card, chosen by content.
//
// Sizing must reach the same verdict the write does, and the write sniffs
// (see archivekind::bytesAreTheDiskImage). Going by extension instead let a
// mislabelled file -- xz bytes named .img -- be sized as raw then written
// decompressed: progress past 400%, and a capacity check against a fraction
// of what the card needed.
SourceSize measureLocalFile(const QString &path);

} // namespace imagesize

#endif // IMAGESIZEPARSER_H
