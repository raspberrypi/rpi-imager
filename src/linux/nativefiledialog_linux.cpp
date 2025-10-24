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
#include <QEvent>
#include <unistd.h>
#include <QProcess>
#include <qnativeinterface.h>

// Helper class to block all input events on a window
class InputBlockerEventFilter : public QObject
{
    Q_OBJECT
public:
    InputBlockerEventFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        // Block all input events and provide feedback
        switch (event->type()) {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            case QEvent::MouseMove:
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            case QEvent::Wheel:
            case QEvent::TouchBegin:
            case QEvent::TouchUpdate:
            case QEvent::TouchEnd:
            case QEvent::TabletPress:
            case QEvent::TabletMove:
            case QEvent::TabletRelease:
                // Block the event
                // Note: We cannot directly focus the portal dialog window as it's managed
                // by the system's file picker. The modal flag should keep it on top.
                // Users can click directly on the file dialog to ensure it has focus.
                return true;
            default:
                // Allow other events to pass through
                return QObject::eventFilter(obj, event);
        }
    }
};

// Helper class to handle D-Bus portal response signals
class PortalResponseHandler : public QObject
{
    Q_OBJECT
public:
    PortalResponseHandler(QEventLoop *loop) 
        : m_loop(loop) {}

    QString result() const { return m_result; }

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
        
        // Quit the event loop immediately when we get a response
        if (m_loop) {
            m_loop->quit();
        }
    }

private:
    QString m_result;
    QEventLoop *m_loop;
};

static QString portalParentHandleForWindow(QWindow *window)
{
    if (!window)
        return QString();

    // Platform detection is RUNTIME-based, not compile-time
    // Qt automatically selects the platform when the app starts
    // To test different backends, set the QT_QPA_PLATFORM environment variable:
    //   QT_QPA_PLATFORM=xcb ./rpi-imager      (force X11)
    //   QT_QPA_PLATFORM=wayland ./rpi-imager  (force Wayland)
    const QString platform = QGuiApplication::platformName().toLower();
    
    // X11/XCB backend
    if (platform.contains("xcb") || platform == "xcb") {
        // On X11, provide the window ID in hex format for proper parenting
        WId wid = window->winId();
        if (wid != 0) {
            QString handle = QStringLiteral("x11:%1").arg(QString::number(wid, 16));
            qDebug() << "NativeFileDialog: X11 detected, parent handle:" << handle;
            return handle;
        }
        qWarning() << "NativeFileDialog: X11 detected but could not get window ID";
        return QString();
    }
    
    // Wayland backend
    if (platform.contains("wayland")) {
        // Wayland window export requires the xdg-foreign protocol
        // Qt doesn't provide a simple API for this, so we rely on modal behavior
        qDebug() << "NativeFileDialog: Wayland detected, using modal dialog without parent handle";
        return QString();
    }
    
    // Unknown platform
    qWarning() << "NativeFileDialog: Unknown platform detected:" << platform;
    return QString();
}

QString NativeFileDialog::getFileNameNative(const QString &title,
                                           const QString &initialDir, const QString &filter,
                                           bool saveDialog, void *parentWindow)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qDebug() << "NativeFileDialog: No D-Bus session bus available";
        return QString(); // QML callsites will handle fallback
    }
    
    QDBusInterface interface("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.FileChooser",
                             bus);
    
    if (!interface.isValid()) {
        qDebug() << "NativeFileDialog: XDG Desktop Portal not available";
        return QString(); // QML callsites will handle fallback
    }
    
    // Prepare parent window identifier for modal behavior
    QString parentWindowId = "";
    QWindow *window = nullptr;
    InputBlockerEventFilter *inputBlocker = nullptr;
    
    if (parentWindow) {
        window = static_cast<QWindow*>(parentWindow);
        parentWindowId = portalParentHandleForWindow(window);
        
        // Install event filter to block all input events while dialog is open
        inputBlocker = new InputBlockerEventFilter();
        window->installEventFilter(inputBlocker);
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
    
    // Generate unique request token
    static uint requestCounter = 0;
    QString token = QString("rpi_imager_%1_%2").arg(getpid()).arg(++requestCounter);
    options["handle_token"] = token;
    
    // Note: File type filters are not supported on Linux
    // The XDG Desktop Portal requires complex D-Bus type marshalling (a(sa(us)))
    // that would require significant boilerplate code for minimal benefit.
    // The dialog will show all files - users can navigate and select any file.
    Q_UNUSED(filter);
    
    QString method = saveDialog ? "SaveFile" : "OpenFile";
    
    // Use QDBusMessage for better control over argument types
    QDBusMessage message = QDBusMessage::createMethodCall(
        "org.freedesktop.portal.Desktop",
        "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.FileChooser",
        method);
    
    message << parentWindowId << title << options;
    
    // Make the call and get the reply
    QDBusMessage replyMsg = bus.call(message);
    
    if (replyMsg.type() == QDBusMessage::ErrorMessage) {
        qDebug() << "NativeFileDialog: Portal call failed:" << replyMsg.errorMessage();
        // Restore window interactivity on error
        if (window && inputBlocker) {
            window->removeEventFilter(inputBlocker);
            delete inputBlocker;
        }
        return QString(); // QML callsites will handle fallback
    }
    
    QDBusReply<QDBusObjectPath> reply(replyMsg);
    
    if (!reply.isValid()) {
        qDebug() << "NativeFileDialog: Portal call failed:" << reply.error().message();
        // Restore window interactivity on error
        if (window && inputBlocker) {
            window->removeEventFilter(inputBlocker);
            delete inputBlocker;
        }
        return QString(); // QML callsites will handle fallback
    }
    
    QString requestPath = reply.value().path();
    
    // Create event loop for blocking until we get a response
    QEventLoop loop;
    
    // Create handler for the portal response
    PortalResponseHandler handler(&loop);
    
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
        // Restore window interactivity on error
        if (window && inputBlocker) {
            window->removeEventFilter(inputBlocker);
            delete inputBlocker;
        }
        return QString(); // QML callsites will handle fallback
    }
    
    // Set up timeout timer (5 minutes should be more than enough)
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, [&loop]() {
        qWarning() << "NativeFileDialog: Portal dialog timed out after 5 minutes";
        loop.quit();
    });
    timeoutTimer.start(300000); // 5 minute timeout
    
    // Wait for response or timeout - this blocks until the user interacts with the dialog
    // This makes the application non-interactive while the dialog is open
    loop.exec();
    
    // Stop the timeout timer if it's still running
    timeoutTimer.stop();
    
    // Disconnect the signal
    QDBusConnection::sessionBus().disconnect(
        "org.freedesktop.portal.Desktop",
        requestPath,
        "org.freedesktop.portal.Request",
        "Response",
        &handler,
        SLOT(handleResponse(uint, QVariantMap))
    );
    
    // Clean up the request object
    QDBusInterface requestInterface("org.freedesktop.portal.Desktop",
                                    requestPath,
                                    "org.freedesktop.portal.Request",
                                    bus);
    if (requestInterface.isValid()) {
        requestInterface.call("Close");
    }
    
    QString result = handler.result();
    
    // Restore window interactivity now that dialog is closed
    if (window && inputBlocker) {
        window->removeEventFilter(inputBlocker);
        delete inputBlocker;
    }
    
    if (result.isEmpty()) {
        qDebug() << "NativeFileDialog: No file selected or portal failed";
        return QString(); // QML callsites will handle fallback
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
