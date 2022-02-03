#ifndef POWERSAVEBLOCKER_H
#define POWERSAVEBLOCKER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QObject>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class PowerSaveBlocker : public QObject
{
    Q_OBJECT
public:
    explicit PowerSaveBlocker(QObject *parent = nullptr);
    virtual ~PowerSaveBlocker();
    void applyBlock(const QString &reason);
    void removeBlock();

signals:

protected:
    bool _stayingAwake;

#ifdef Q_OS_WIN
    HANDLE _powerRequest;
#endif
};

#endif // POWERSAVEBLOCKER_H
