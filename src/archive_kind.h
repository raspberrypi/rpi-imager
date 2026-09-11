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
// The question is what it recognised:
inline bool bytesAreTheDiskImage(int format, int filterCode)
{
    if (filterCode != ARCHIVE_FILTER_NONE)
        return false;

    switch (format & ARCHIVE_FORMAT_BASE_MASK) {
    case ARCHIVE_FORMAT_RAW:
    case ARCHIVE_FORMAT_ISO9660:
        return true;
    default:
        return false;
    }
}

} // namespace archivekind

#endif // ARCHIVE_KIND_H
