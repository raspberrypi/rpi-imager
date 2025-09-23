/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "linuxfile.h"
#include <unistd.h>
#include <errno.h>
#include <QDebug>

LinuxFile::LinuxFile(QObject *parent)
    : QFile(parent)
{
}

bool LinuxFile::forceSync()
{
    if (!isOpen()) {
        qDebug() << "Warning: Cannot sync closed file";
        return false;
    }
    
    // Flush Qt's internal buffers first
    if (!flush()) {
        qDebug() << "Warning: flush() failed during forceSync:" << errorString();
        return false;
    }
    
    // Force filesystem sync using fsync
    if (::fsync(handle()) != 0) {
        qDebug() << "Warning: fsync() failed during forceSync, errno:" << errno;
        return false;
    }
    
    return true;
}
