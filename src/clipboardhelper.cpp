/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "clipboardhelper.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QDebug>

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent)
{
    // Connect to clipboard changes to emit our signal
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        connect(clipboard, &QClipboard::dataChanged, this, &ClipboardHelper::clipboardChanged);
    }
}

void ClipboardHelper::setText(const QString &text)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text, QClipboard::Clipboard);
        qDebug() << "ClipboardHelper: Set clipboard text:" << text;
    } else {
        qWarning() << "ClipboardHelper: Failed to access clipboard";
    }
}

QString ClipboardHelper::getText() const
{
    // Use the static method as shown in Qt documentation
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        QString text = clipboard->text(QClipboard::Clipboard);
        qDebug() << "ClipboardHelper: Got clipboard text:" << text;
        return text;
    } else {
        qWarning() << "ClipboardHelper: Failed to access clipboard";
        return QString();
    }
}

bool ClipboardHelper::hasText() const
{
    // Use the static method as shown in Qt documentation
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        return !clipboard->text(QClipboard::Clipboard).isEmpty();
    }
    return false;
}

