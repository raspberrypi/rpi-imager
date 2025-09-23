/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef LINUXFILE_H
#define LINUXFILE_H

#include <QFile>

class LinuxFile : public QFile
{
    Q_OBJECT
public:
    LinuxFile(QObject *parent = nullptr);
    
    /**
     * @brief Force filesystem sync using fsync()
     * @return true if sync was successful, false otherwise
     */
    bool forceSync();
};

#endif // LINUXFILE_H
