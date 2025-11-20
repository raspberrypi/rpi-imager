/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef URIHANDLER_DBUS_H
#define URIHANDLER_DBUS_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QDBusAbstractAdaptor>

class ImageWriter;

class UriHandlerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.raspberrypi.rpi-imager")

public:
    explicit UriHandlerAdaptor(ImageWriter *imageWriter, QObject *parent = nullptr);

public slots:
    void HandleUrl(const QString &urlString);

private:
    ImageWriter *m_imageWriter;
};

#endif // URIHANDLER_DBUS_H

