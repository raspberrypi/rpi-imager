/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "../nativefiledialog.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QWindow>
#include <QUrl>
#include <QStandardPaths>
#include <QDebug>
#include <QGuiApplication>
#include <unistd.h>
#include <QProcess>

namespace {
QString convertQtFilterToLinux(const QString &qtFilter)
{
    if (qtFilter.isEmpty()) {
        return QString();
    }
    
    // Qt filter format: "Description (*.ext1 *.ext2);;Another (*.ext3)"
    // Linux/portal format: simplified for now - just extract extensions
    return qtFilter.section('(', 1, 1).section(')', 0, 0);
}
} // anonymous namespace

QString NativeFileDialog::getFileNameNative(QWidget *parent, const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog)
{
    Q_UNUSED(parent)
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "NativeFileDialog: No D-Bus session bus available, falling back to Qt";
        return getFileNameQt(parent, title, initialDir, filter, saveDialog);
    }
    
    QDBusInterface interface("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.FileChooser",
                             bus);
    
    if (!interface.isValid()) {
        qDebug() << "NativeFileDialog: XDG Desktop Portal not available, falling back to Qt";
        return getFileNameQt(parent, title, initialDir, filter, saveDialog);
    }
    
    // Prepare arguments for the portal call
    QVariantMap options;
    options["modal"] = true;
    
    if (!title.isEmpty()) {
        options["title"] = title;
    }
    
    // Set initial directory
    if (!initialDir.isEmpty()) {
        QUrl dirUrl = QUrl::fromLocalFile(initialDir);
        options["current_folder"] = QByteArray(dirUrl.toEncoded());
    }
    
    // Convert and set file filters
    if (!filter.isEmpty()) {
        QString linuxFilter = convertQtFilterToLinux(filter);
        // Portal expects filters in a specific format - this is simplified
        options["filters"] = QVariant::fromValue(QStringList() << linuxFilter);
    }
    
    QString method = saveDialog ? "SaveFile" : "OpenFile";
    QString parentWindow = "";  // Could get X11 window ID if needed
    
    QDBusReply<QDBusObjectPath> reply = interface.call(method, parentWindow, title, options);
    
    if (!reply.isValid()) {
        qDebug() << "NativeFileDialog: Portal call failed:" << reply.error().message();
        return getFileNameQt(parent, title, initialDir, filter, saveDialog);
    }
    
    // The portal returns a path to monitor for the response
    // This is a simplified implementation - in reality you'd need to monitor
    // the D-Bus response asynchronously
    QString objPath = reply.value().path();
    
    // For now, fall back to Qt dialog as implementing the full async portal
    // response handling would require significant additional code
    qDebug() << "NativeFileDialog: Portal response handling not fully implemented, falling back to Qt";
    return getFileNameQt(parent, title, initialDir, filter, saveDialog);
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    // Check if we have a desktop environment and portal support
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "NativeFileDialog: No D-Bus session bus available";
        return false;
    }

    // Check if xdg-desktop-portal is available
    QDBusInterface interface("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.FileChooser",
                             bus);
    
    if (!interface.isValid()) {
        qDebug() << "NativeFileDialog: XDG Desktop Portal not available";
        return false;
    }

    return true;
}

