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
#include <QEventLoop>
#include <QTimer>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <unistd.h>
#include <QProcess>

// Helper class to handle D-Bus portal response signals
class PortalResponseHandler : public QObject
{
    Q_OBJECT
public:
    PortalResponseHandler(QString &result, bool &dialogFinished)
        : m_result(result), m_dialogFinished(dialogFinished) {}

public slots:
    void handleResponse(uint response, const QVariantMap &results) {
        qDebug() << "NativeFileDialog: Portal response:" << response << results;
        
        if (response == 0) { // Success
            // Extract selected files from results
            QVariant urisVar = results.value("uris");
            if (urisVar.isValid()) {
                QStringList uris = urisVar.toStringList();
                if (!uris.isEmpty()) {
                    // Convert file:// URI to local path
                    QUrl url(uris.first());
                    m_result = url.toLocalFile();
                    qDebug() << "NativeFileDialog: Selected file:" << m_result;
                }
            }
        } else {
            qDebug() << "NativeFileDialog: Dialog cancelled or failed, response code:" << response;
        }
        
        m_dialogFinished = true;
    }

private:
    QString &m_result;
    bool &m_dialogFinished;
};

// Anonymous namespace removed - filter conversion now done inline

QString NativeFileDialog::getFileNameNative(const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog)
{
    
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "NativeFileDialog: No D-Bus session bus available, falling back to Qt";
        return getFileNameQt(title, initialDir, filter, saveDialog);
    }
    
    QDBusInterface interface("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.FileChooser",
                             bus);
    
    if (!interface.isValid()) {
        qDebug() << "NativeFileDialog: XDG Desktop Portal not available, falling back to Qt";
        return getFileNameQt(title, initialDir, filter, saveDialog);
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
    
    // Convert and set file filters - Portal expects specific format
    if (!filter.isEmpty()) {
        // Parse Qt filter format: "Images (*.png *.jpg);;All files (*)"
        QStringList filterParts = filter.split(";;");
        QVariantList filters;
        
        for (const QString &filterPart : filterParts) {
            if (filterPart.contains('(') && filterPart.contains(')')) {
                QString name = filterPart.section('(', 0, 0).trimmed();
                QString patterns = filterPart.section('(', 1, 1).section(')', 0, 0);
                QStringList patternList = patterns.split(' ', Qt::SkipEmptyParts);
                
                // Portal filter format: [name, [[pattern1, pattern2], ...]]
                QVariantList filterEntry;
                filterEntry << name;
                QVariantList patternVariants;
                for (const QString &pattern : patternList) {
                    patternVariants << QVariant(pattern);
                }
                filterEntry << QVariant(patternVariants);
                filters << QVariant(filterEntry);
            }
        }
        
        if (!filters.isEmpty()) {
            options["filters"] = QVariant(filters);
        }
    }
    
    // Generate unique request token
    static uint requestCounter = 0;
    QString token = QString("rpi_imager_%1_%2").arg(getpid()).arg(++requestCounter);
    options["handle_token"] = token;
    
    QString method = saveDialog ? "SaveFile" : "OpenFile";
    QString parentWindow = "";  // Could be set to X11 window ID for proper modal behavior
    
    // Make the async call
    QDBusReply<QDBusObjectPath> reply = interface.call(method, parentWindow, title, options);
    
    if (!reply.isValid()) {
        qDebug() << "NativeFileDialog: Portal call failed:" << reply.error().message();
        return getFileNameQt(title, initialDir, filter, saveDialog);
    }
    
    QString requestPath = reply.value().path();
    qDebug() << "NativeFileDialog: Portal request created at:" << requestPath;
    
    // Connect to the Response signal to get the result
    QString result;
    bool dialogFinished = false;
    
    // Create handler for the portal response
    PortalResponseHandler handler(result, dialogFinished);
    
    // Connect to the D-Bus signal using QDBusConnection
    bool connected = QDBusConnection::sessionBus().connect(
        "org.freedesktop.portal.Desktop",
        requestPath,
        "org.freedesktop.portal.Request", 
        "Response",
        &handler,
        SLOT(handleResponse(uint, QVariantMap))
    );
    
    if (!connected) {
        qDebug() << "NativeFileDialog: Could not connect to portal Response signal";
        return getFileNameQt(title, initialDir, filter, saveDialog);
    }
    
    // Run a local event loop until we get the response
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(30000); // 30 second timeout
    
    QObject::connect(&timeoutTimer, &QTimer::timeout, [&loop, &dialogFinished]() {
        qWarning() << "NativeFileDialog: Portal dialog timed out";
        dialogFinished = true;
        loop.quit();
    });
    
    // Check for completion every 100ms
    QTimer checkTimer;
    checkTimer.setInterval(100);
    QObject::connect(&checkTimer, &QTimer::timeout, [&loop, &dialogFinished]() {
        if (dialogFinished) {
            loop.quit();
        }
    });
    
    timeoutTimer.start();
    checkTimer.start();
    
    // Wait for response or timeout
    loop.exec();
    
    // Clean up the request object
    QDBusInterface requestInterface("org.freedesktop.portal.Desktop",
                                    requestPath,
                                    "org.freedesktop.portal.Request",
                                    bus);
    if (requestInterface.isValid()) {
        requestInterface.call("Close");
    }
    
    if (result.isEmpty()) {
        qDebug() << "NativeFileDialog: No file selected or portal failed, falling back to Qt";
        return getFileNameQt(title, initialDir, filter, saveDialog);
    }
    
    return result;
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

#include "nativefiledialog_linux.moc"

