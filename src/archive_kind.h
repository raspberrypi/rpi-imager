#ifndef ARCHIVE_KIND_H
#define ARCHIVE_KIND_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include <archive.h>

namespace archivekind {

// Given what libarchive says it has just identified, are the bytes the disk
// image itself, or a container holding one?
//
// Both answers matter and neither is safe to guess. Unpack a plain .img and
// libarchive's raw reader hands the same bytes back, only slower. Copy a
// container verbatim and the card gets the zip rather than what is in it.
//
// libarchive is asked with format_raw enabled, which matches any file at all,
// so "did it recognise something" is always yes and cannot be the question.
// The question is what it recognised.

// Half of that question, asked of the format alone, so it still holds with
// compression wrapped around it: a .iso.xz is an image to write, not a bundle
// of the ISO's contents to unpack. Sizing needs this half on its own.
inline bool formatIsASingleImage(int format)
{
    switch (format & ARCHIVE_FORMAT_BASE_MASK) {
    case ARCHIVE_FORMAT_RAW:
    case ARCHIVE_FORMAT_ISO9660:
        return true;
    default:
        return false;
    }
}

// The whole question: an image, and nothing wrapped around it.
inline bool bytesAreTheDiskImage(int format, int filterCode)
{
    return filterCode == ARCHIVE_FILTER_NONE && formatIsASingleImage(format);
}

} // namespace archivekind

#endif // ARCHIVE_KIND_H
