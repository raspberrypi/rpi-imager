#include "downloadstatstelemetry.h"
#include "config.h"
#include <QSettings>
#include <QDebug>
#include <QUrl>

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi (Trading) Limited
 */

DownloadStatsTelemetry::DownloadStatsTelemetry(const QByteArray &url, const QByteArray &parentcategory, const QByteArray &osname, QObject *parent)
    : QThread(parent), _url(TELEMETRY_URL)
{
    _postfields = "url="+QUrl::toPercentEncoding(url)
            +"&os="+QUrl::toPercentEncoding(parentcategory)
            +"&image="+QUrl::toPercentEncoding(osname);
    _useragent = "Mozilla/5.0 rpi-imager/" IMAGER_VERSION_STR;
}

void DownloadStatsTelemetry::run()
{
    QSettings settings;
    if (!settings.value("telemetry", TELEMETRY_ENABLED_DEFAULT).toBool())
        return;

    _c = curl_easy_init();
    curl_easy_setopt(_c, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(_c, CURLOPT_WRITEFUNCTION, &DownloadStatsTelemetry::_curl_write_callback);
    curl_easy_setopt(_c, CURLOPT_HEADERFUNCTION, &DownloadStatsTelemetry::_curl_header_callback);
    curl_easy_setopt(_c, CURLOPT_URL, _url.constData());
    curl_easy_setopt(_c, CURLOPT_POSTFIELDSIZE, _postfields.length());
    curl_easy_setopt(_c, CURLOPT_POSTFIELDS, _postfields.constData());
    curl_easy_setopt(_c, CURLOPT_USERAGENT, _useragent.constData());
    curl_easy_setopt(_c, CURLOPT_CONNECTTIMEOUT, 10);
    curl_easy_setopt(_c, CURLOPT_LOW_SPEED_TIME, 10);
    curl_easy_setopt(_c, CURLOPT_LOW_SPEED_LIMIT, 10);

    CURLcode ret = curl_easy_perform(_c);
    curl_easy_cleanup(_c);

    qDebug() << "Telemetry done. cURL status code =" << ret;
}

/* /dev/null write handler */
size_t DownloadStatsTelemetry::_curl_write_callback(char *, size_t size, size_t nmemb, void *)
{
    return size * nmemb;
}

size_t DownloadStatsTelemetry::_curl_header_callback( void *ptr, size_t size, size_t nmemb, void *)
{
    int len = size*nmemb;
    QByteArray headerstr((char *) ptr, len);
    //qDebug() << "Received telemetry header:" << headerstr;
    return len;
}
