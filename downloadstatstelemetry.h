#ifndef DOWNLOADSTATSTELEMETRY_H
#define DOWNLOADSTATSTELEMETRY_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QObject>
#include <QThread>
#include <curl/curl.h>

class DownloadStatsTelemetry : public QThread
{
    Q_OBJECT
public:
    explicit DownloadStatsTelemetry(const QByteArray &url, const QByteArray &parentcategory, const QByteArray &osname, bool embedded, const QString &imagerLang, QObject *parent = nullptr);

protected:
    CURL *_c;
    QByteArray _url, _useragent, _postfields;
    virtual void run();
    static size_t _curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t _curl_header_callback( void *ptr, size_t size, size_t nmemb, void *userdata);

signals:

public slots:
};

#endif // DOWNLOADSTATSTELEMETRY_H
