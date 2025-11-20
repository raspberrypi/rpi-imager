/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "urihandler_dbus.h"
#include "../imagewriter.h"
#include <QDebug>
#include <QMetaObject>

UriHandlerAdaptor::UriHandlerAdaptor(ImageWriter *imageWriter, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_imageWriter(imageWriter)
{
}

void UriHandlerAdaptor::HandleUrl(const QString &urlString)
{
    QUrl url(urlString);
    qDebug() << "Received URL via D-Bus:" << urlString;
    QMetaObject::invokeMethod(
        m_imageWriter,
        [url, this] {
            m_imageWriter->handleIncomingUrl(url);
        },
        Qt::QueuedConnection
    );
}

