#ifndef CLIPBOARDHELPER_H
#define CLIPBOARDHELPER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include <QObject>
#include <QString>
#include <QQmlEngine>

class ClipboardHelper : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool hasText READ hasText NOTIFY clipboardChanged)

public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    // Clipboard operations
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE QString getText() const;
    bool hasText() const;

signals:
    void clipboardChanged();
};

#endif // CLIPBOARDHELPER_H

