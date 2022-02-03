#ifndef WINFILE_H
#define WINFILE_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QObject>
#include <QIODevice>
#include <windows.h>

class WinFile : public QObject
{
    Q_OBJECT
public:
    explicit WinFile(QObject *parent = nullptr);
    virtual ~WinFile();
    void setFileName(const QString &name);
    bool open(QIODevice::OpenMode mode);
    void close();
    bool isOpen();
    qint64 write(const char *data, qint64 maxSize);
    qint64 read(char *data, qint64 maxSize);
    HANDLE handle();
    QString errorString() const;
    int errorCode() const;
    bool flush();
    bool seek(qint64 pos);
    qint64 pos();
    bool lockVolume();
    bool unlockVolume();

protected:
    bool _locked;
    QString _name, _lasterror;
    HANDLE _h;
    int _lasterrorcode;
};

#endif // WINFILE_H
