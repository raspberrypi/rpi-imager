#ifndef STPANALYZER_H
#define STPANALYZER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2024 Raspberry Pi Ltd
 */

#include <QObject>
#include <QSocketNotifier>

class StpAnalyzer : public QObject
{
    Q_OBJECT
public:
    StpAnalyzer(int onlyReportIfForwardDelayIsAbove = 5, QObject *parent = nullptr);
    virtual ~StpAnalyzer();
    void startListening(const QByteArray &ifname);
    void stopListening();

signals:
    void detected();

protected:
    int _s, _minForwardDelay;
    QSocketNotifier *_qsn;

protected slots:
    void onPacket(QSocketDescriptor socket, QSocketNotifier::Type type);
};

#endif // STPANALYZER_H
