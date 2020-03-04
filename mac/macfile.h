#ifndef MACFILE_H
#define MACFILE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include <QFile>

class MacFile : public QFile
{
    Q_OBJECT
public:
    MacFile(QObject *parent = nullptr);
    virtual bool isSequential() const;
    bool authOpen(const QByteArray &filename);
};

#endif // MACFILE_H
