#ifndef DRIVELISTITEM_H
#define DRIVELISTITEM_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QObject>
#include <QStringList>

class DriveListItem : public QObject
{
    Q_OBJECT
public:
    explicit DriveListItem(QString device, QString description, quint64 size, bool isUsb = false, bool isScsi = false, bool readOnly = false, QStringList mountpoints = QStringList(), QObject *parent = nullptr);

    Q_PROPERTY(QString device MEMBER _device CONSTANT)
    Q_PROPERTY(QString description MEMBER _description CONSTANT)
    Q_PROPERTY(quint64 size MEMBER _size CONSTANT)
    Q_PROPERTY(QStringList mountpoints MEMBER _mountpoints CONSTANT)
    Q_PROPERTY(bool isUsb MEMBER _isUsb CONSTANT)
    Q_PROPERTY(bool isScsi MEMBER _isScsi CONSTANT)
    Q_PROPERTY(bool isReadOnly MEMBER _isReadOnly CONSTANT)
    Q_INVOKABLE int sizeInGb();

signals:

public slots:

protected:
    QString _device;
    QString _description;
    QStringList _mountpoints;
    quint64 _size;
    bool _isUsb;
    bool _isScsi;
    bool _isReadOnly;
};

#endif // DRIVELISTITEM_H
