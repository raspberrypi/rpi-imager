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
        return false;
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
    }

private:
    QString m_result;
    QEventLoop *m_loop;
};

static QString portalParentHandleForWindow(QWindow *window)
{
    return QString();
}

QString NativeFileDialog::getFileNameNative(const QString &title,
                                            const QString &initialDir, const QString &filter,
                                            bool saveDialog, void *parentWindow)
{
    QString result;

    return result;
}

bool NativeFileDialog::areNativeDialogsAvailablePlatform()
{
    return false;
}

#include "nativefiledialog_freebsd.moc"
