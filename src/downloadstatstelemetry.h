#ifndef DOWNLOADSTATSTELEMETRY_H
#define DOWNLOADSTATSTELEMETRY_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QObject>
#include <QThread>

#include "config.h"
#include <curl/curl.h>

class DownloadStatsTelemetry : public QThread
{
    Q_OBJECT
public:
    // `url` is the image that was downloaded -- it goes into the POST body,
    // not the address posted to. `endpoint` is the address, and defaults to
    // the production one; it is last so the two application call sites, which
    // pass `parent` positionally, need no change. A test that does not
    // override it posts real telemetry to the live server.
    explicit DownloadStatsTelemetry(const QByteArray &url, const QByteArray &parentcategory, const QByteArray &osname, bool embedded, const QString &imagerLang, QObject *parent = nullptr, const QByteArray &endpoint = QByteArray(TELEMETRY_URL));
    ~DownloadStatsTelemetry() override;

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
