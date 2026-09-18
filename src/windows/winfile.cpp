/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "winfile.h"

#include <string>
#include <QDebug>
#include <QThread>

WinFile::WinFile(QObject *parent)
    : QObject(parent), _locked(false), _h(INVALID_HANDLE_VALUE), _lasterrorcode(0)
{

}

WinFile::~WinFile()
{
    if (isOpen())
        close();
}

void WinFile::setFileName(const QString &name)
{
    _name = name;
}

bool WinFile::open(QIODevice::OpenMode)
{
    // Wide, not Latin-1. toLatin1() replaces everything outside it with '?',
    // so a path under a profile named in Cyrillic or CJK -- an ordinary thing
    // to have -- named a file that does not exist, and the open failed after
    // two seconds of retries for a reason no message explained.
    const std::wstring n = _name.toStdWString();

    for (int attempt = 0; attempt < 20; attempt++)
    {
        _h = CreateFileW(n.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (_h != INVALID_HANDLE_VALUE)
            break;

        // Not worth retrying something that cannot become true: a path that is
        // not there will not be there in two seconds, and the wait was paid on
        // every failed open. Only a device being briefly held is transient.
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
            err == ERROR_INVALID_NAME)
            break;

        qDebug() << "Error opening device. Retrying...";
        QThread::msleep(100);
    }

    // Try with FILE_SHARE_WRITE
    if (_h == INVALID_HANDLE_VALUE)
        _h = CreateFileW(n.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (_h == INVALID_HANDLE_VALUE)
    {
        _lasterrorcode = GetLastError();
        _lasterror = qt_error_string();
        qDebug() << "Error opening:" << _lasterror;
        return false;
    }

    return true;
}

void WinFile::close()
{
    if (!isOpen())
        return;

    if (_locked)
    {
        unlockVolume();
    }

    CloseHandle(_h);
    _h = INVALID_HANDLE_VALUE;
}

bool WinFile::isOpen()
{
    return _h != INVALID_HANDLE_VALUE;
}

qint64 WinFile::write(const char *data, qint64 maxSize)
{
    DWORD bytesWritten;

    if (maxSize % 512)
        qDebug() << "write: NOT SECTOR ALIGNED";

    if (!WriteFile(_h, data, maxSize, &bytesWritten, NULL))
    {
        _lasterrorcode = GetLastError();
        _lasterror = qt_error_string();
        return -1;
    }

    return bytesWritten;
}

qint64 WinFile::read(char *data, qint64 maxSize)
{
    DWORD bytesRead;

    if (!ReadFile(_h, data, maxSize, &bytesRead, NULL))
    {
        _lasterrorcode = GetLastError();
        _lasterror = qt_error_string();
        return -1;
    }

    return bytesRead;
}

bool WinFile::seek(qint64 pos)
{
    LARGE_INTEGER current;
    LARGE_INTEGER offset;
    offset.QuadPart = pos;
    if (!SetFilePointerEx(_h, offset, &current, FILE_BEGIN))
    {
        _lasterrorcode = GetLastError();
        _lasterror = qt_error_string();
        qDebug() << "Error seeking:" << _lasterror;
        return false;
    }

    return true;
}

qint64 WinFile::pos()
{
    LARGE_INTEGER current;
    LARGE_INTEGER offset;
    offset.QuadPart = 0;
    if (!SetFilePointerEx(_h, offset, &current, FILE_CURRENT))
    {
        _lasterrorcode = GetLastError();
        _lasterror = qt_error_string();
        // -1, not 0: nought is a legitimate position, so answering it here
        // would be indistinguishable from a file open at its start.
        return -1;
    }

    return qint64(current.QuadPart);
}

HANDLE WinFile::handle()
{
    return _h;
}

QString WinFile::errorString() const
{
    return _lasterror;
}

int WinFile::errorCode() const
{
    return _lasterrorcode;
}

bool WinFile::flush()
{
    if (!FlushFileBuffers(_h))
    {
        // Windows 7 does not support flush properly, so ignore errors
        //_lasterror = qt_error_string();
        //return false;
    }

    return true;
}

bool WinFile::forceSync()
{
    if (!isOpen()) {
        qDebug() << "Warning: Cannot sync closed file";
        return false;
    }
    
    // On Windows, FlushFileBuffers does both Qt buffer flush and filesystem sync
    if (!FlushFileBuffers(_h)) {
        DWORD error = GetLastError();
        qDebug() << "Warning: FlushFileBuffers() failed during forceSync, error:" << error;
        return false;
    }
    
    return true;
}

bool WinFile::lockVolume()
{
    DWORD bytesRet;
    DeviceIoControl(_h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &bytesRet, NULL);
    for (int attempt = 0; attempt < 20; attempt++)
    {
        qDebug() << "Locking volume" << _name;

        if (DeviceIoControl(_h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL))
        {
            _locked = true;
            qDebug() << "Locked volume";
            return true;
        }

        qDebug() << "FSCTL_LOCK_VOLUME failed. Retrying in 0.1 sec";
        QThread::msleep(100);
    }

    qDebug() << "Giving up locking volume";
    return false;
}

bool WinFile::unlockVolume()
{
    if (!_locked)
        return true;

    DWORD bytesRet;
    if (DeviceIoControl(_h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &bytesRet, NULL))
    {
        _locked = false;
        qDebug() << "Unlocked volume";
        return true;
    }
    else
    {
        qDebug() << "FSCTL_UNLOCK_VOLUME failed";
        return false;
    }
}
