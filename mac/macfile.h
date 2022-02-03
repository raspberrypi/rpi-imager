#ifndef MACFILE_H
#define MACFILE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QFile>

class MacFile : public QFile
{
    Q_OBJECT
public:
    enum authOpenResult {authOpenCancelled, authOpenSuccess, authOpenError };

    MacFile(QObject *parent = nullptr);
    virtual bool isSequential() const;
    authOpenResult authOpen(const QByteArray &filename);
};

#endif // MACFILE_H
