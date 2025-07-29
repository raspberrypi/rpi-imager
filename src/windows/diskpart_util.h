/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifndef DISKPART_UTIL_H
#define DISKPART_UTIL_H

#include <QString>
#include <QByteArray>
#include <QObject>
#include <chrono>

namespace DiskpartUtil {

enum class VolumeHandling {
    SkipUnmounting,
    UnmountFirst
};

struct DiskpartResult {
    bool success;
    QString errorMessage;
};

/**
 * Cleans a Windows physical drive using diskpart utility
 * 
 * @param device - Windows physical drive path (e.g., "\\\\.\\PHYSICALDRIVE0")
 * @param timeout - Timeout for diskpart operation (default: 60 seconds)
 * @param maxRetries - Maximum number of retry attempts (default: 3)
 * @param volumeHandling - How to handle mounted volumes before diskpart (default: UnmountFirst)
 * @return DiskpartResult with success status and error message if failed
 */
DiskpartResult cleanDisk(const QByteArray &device, std::chrono::milliseconds timeout = std::chrono::seconds(60), int maxRetries = 3, VolumeHandling volumeHandling = VolumeHandling::UnmountFirst);

} // namespace DiskpartUtil

#endif // DISKPART_UTIL_H 