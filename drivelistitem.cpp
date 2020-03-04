/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include "drivelistitem.h"

DriveListItem::DriveListItem(QString device, QString description, quint64 size, bool isUsb, bool isScsi, QStringList mountpoints, QObject *parent)
    : QObject(parent), _device(device), _description(description), _mountpoints(mountpoints), _size(size), _isUsb(isUsb), _isScsi(isScsi)
{

}

int DriveListItem::sizeInGb()
{
    return _size / 1000000000;
}
