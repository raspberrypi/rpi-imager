#ifndef POSIX_WRITE_ERROR_H
#define POSIX_WRITE_ERROR_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 */

#include "file_operations.h"

#include <cerrno>

namespace rpi_imager {

// What a failed write's errno says about the card, so the user can be told
// the one thing that is wrong rather than three things to check.
//
// Windows has classified its write failures for a long time; every other
// platform answered kUnknown, and the message behind that asks the user to
// check whether the device is writable, has space, and is not
// write-protected -- which is three questions when exactly one of them has
// already been answered by the kernel.
//
// Only failures the user can do something about are classified. A transient
// EAGAIN or an ENOMEM says nothing about the card, and dressing it up as a
// diagnosis would be worse than the generic message.
inline WriteErrorClass ClassifyPosixWriteErrno(int err)
{
    switch (err) {
    case ENOSPC:
        return WriteErrorClass::kDiskFull;
#ifdef EFBIG
    // Writing past a size limit rather than off the end of a device. Not the
    // same cause, but "use a larger one" is still the answer.
    case EFBIG:
        return WriteErrorClass::kDiskFull;
#endif
    case EROFS:
        return WriteErrorClass::kWriteProtected;
    case EACCES:
    case EPERM:
        return WriteErrorClass::kAccessDenied;
    // EIO covers both a bad sector and a reader pulled out mid-write, and
    // there is no errno that separates them. Reported as the device having
    // gone away, because that message covers both readings -- the media
    // message names counterfeit cards, and telling somebody their card is
    // fake when their hub dropped out is worse than saying less.
    case EIO:
    case ENODEV:
    case ENXIO:
    case EPIPE:
#ifdef ESHUTDOWN
    case ESHUTDOWN:
#endif
        return WriteErrorClass::kIoDeviceError;
    case EINVAL:
    case EBADF:
    case ESPIPE:
    case EOVERFLOW:
        return WriteErrorClass::kInvalidParameter;
    default:
        return WriteErrorClass::kUnknown;
    }
}

} // namespace rpi_imager

#endif // POSIX_WRITE_ERROR_H
