/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

#include "scriptrun.h"
#include "drivelistitem.h"
#include "downloadextractthread.h"
#include "dependencies/drivelist/src/drivelist.hpp"
#include "dependencies/sha256crypt/sha256crypt.h"
#include "driveformatthread.h"
#include "localfileextractthread.h"
#include "downloadstatstelemetry.h"
#include <archive.h>
#include <archive_entry.h>
#include <random>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QProcess>
#include <QRegExp>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimeZone>
#include <QWindow>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QDebug>
#include <QVersionNumber>
#ifndef QT_NO_WIDGETS
#include <QFileDialog>
#include <QApplication>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#include <wlanapi.h>
#ifndef WLAN_PROFILE_GET_PLAINTEXT_KEY
#define WLAN_PROFILE_GET_PLAINTEXT_KEY 4
#endif

#include <QWinTaskbarButton>
#include <QWinTaskbarProgress>
#endif

ScriptRun::ScriptRun(QObject *parent)
    : QObject(parent)
{

#ifdef Q_OS_WIN
    _taskbarButton = nullptr;
#endif

}

ScriptRun::~ScriptRun()
{

}

void ScriptRun::setSrc(QString filePath)
{
    _src = filePath;
}
