#ifndef SCRIPTRUN_H
#define SCRIPTRUN_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QSettings>
#include <QVariant>
#include "config.h"
#include "powersaveblocker.h"
#include "drivelistmodel.h"

class QQmlApplicationEngine;
class DownloadThread;
class QNetworkReply;
class QWinTaskbarButton;

class ScriptRun : public QObject
{
    Q_OBJECT
public:
    explicit ScriptRun(QObject *parent = nullptr);
    virtual ~ScriptRun();
    void setEngine(QQmlApplicationEngine *engine);


    Q_INVOKABLE void setSrc(QString filePath = "");

protected:
    QString _src;

#ifdef Q_OS_WIN
    QWinTaskbarButton *_taskbarButton;
#endif
};

#endif // SCRIPTRUN_H
