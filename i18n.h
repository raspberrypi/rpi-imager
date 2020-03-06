/*
 * License: Apache-2.0
 * Copyright (C) 2020 GloomyGhost
 */

#pragma once

#ifndef I18N_H
#define I18N_H

#include <QObject>
#include <QTranslator>
#include <QMap>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
class I18n: public QObject{
    Q_OBJECT
public:
    QDir directoryOf(const QString &subdir)
    {
        QDir dir(QCoreApplication::applicationDirPath());

    #if defined(Q_OS_WIN)
        if (dir.dirName().toLower() == "debug"
                || dir.dirName().toLower() == "release"
                || dir.dirName().toLower() == "bin")
            dir.cdUp();
    #elif defined(Q_OS_MAC)
        if (dir.dirName() == "MacOS") {
            dir.cdUp();
            dir.cdUp();
            dir.cdUp();
        }
    #endif
        dir.cd(subdir);
        return dir;
    }

    I18n(QObject *parent = 0) : QObject(parent) {
        QTranslator *en = new QTranslator(this);
        QTranslator *cn = new QTranslator(this);
        bool ret = en->load(directoryOf(".").absoluteFilePath("en_us.qm"));
        ret = cn->load(directoryOf(".").absoluteFilePath("zh_cn.qm"));
        mTrans["en"] = en;
        mTrans["zh"] = cn;
    }

    Q_INVOKABLE void reTrans(const QString &lan) {
        if (mTrans.contains(lan)) {
            if (!mLastLan.isEmpty()) {
                QCoreApplication::removeTranslator(mTrans[mLastLan]);
            }
            mLastLan = lan;
            QCoreApplication::installTranslator(mTrans[mLastLan]);
            emit retransRequest();
            qWarning() << "retrans" << lan;
        }
    }
signals:
    void retransRequest();
private:
    QMap<QString, QTranslator *> mTrans;
    QString mLastLan;
};

#endif // I18N_H
