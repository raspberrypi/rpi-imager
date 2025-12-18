/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "downloadstatstelemetry.h"
#include "curlnetworkconfig.h"
#include "config.h"
#include <QSettings>
#include <QDebug>
#include <QUrl>
#include <QSysInfo>
#include <QLocale>
#include <QFile>
#include <QRegularExpression>

DownloadStatsTelemetry::DownloadStatsTelemetry(const QByteArray &url, const QByteArray &parentcategory, const QByteArray &osname, bool embedded, const QString &imagerLang, QObject *parent)
    : QThread(parent), _url(TELEMETRY_URL)
{
    QLocale locale;
    
    // Extract clean numeric version (X.Y.Z) from IMAGER_VERSION_STR for telemetry
    // Handles formats like: v2.0.0, v2.0.0-rc4-60-geac7c2f0, 2.0.0, etc.
    QString versionStr(IMAGER_VERSION_STR);
    static QRegularExpression versionRx("^v?([0-9]+\\.[0-9]+\\.[0-9]+)");
    QRegularExpressionMatch versionMatch = versionRx.match(versionStr);
    QByteArray cleanVersion = versionMatch.hasMatch() 
        ? versionMatch.captured(1).toLatin1() 
        : QByteArray(IMAGER_VERSION_STR);
    
    _postfields = "url="+QUrl::toPercentEncoding(url)
            +"&os="+QUrl::toPercentEncoding(parentcategory)
            +"&image="+QUrl::toPercentEncoding(osname)
            +"&imagerVersion="+QUrl::toPercentEncoding(cleanVersion)
            +"&imagerOsType="+(embedded ? "embedded" : QUrl::toPercentEncoding(QSysInfo::productType()))
            +"&imagerOsVersion="+QUrl::toPercentEncoding(QSysInfo::productVersion())
            +"&imagerOsArch="+QUrl::toPercentEncoding(QSysInfo::currentCpuArchitecture())
            +"&imagerLocale="+QUrl::toPercentEncoding(embedded ? imagerLang : locale.name());
#ifdef Q_OS_LINUX
    QFile f("/proc/cpuinfo");
    f.open(f.ReadOnly);
    QByteArray cpuinfo = f.readAll();
    f.close();

    if (cpuinfo.contains("Raspberry Pi")) {
        static QRegularExpression rx("Revision[ \t]*: ([0-9a-f]+)");
        QRegularExpressionMatch m = rx.match(cpuinfo);
        if (m.hasMatch())
        {
            _postfields += "&imagerPiRevision="+QUrl::toPercentEncoding(m.captured(1));
        }
    }
#endif
}

void DownloadStatsTelemetry::run()
{
    QSettings settings;
    if (!settings.value("telemetry", TELEMETRY_ENABLED_DEFAULT).toBool())
        return;

    _c = curl_easy_init();
    if (!_c) {
        qDebug() << "Telemetry: failed to init curl";
        return;
    }
    
    // Apply shared network configuration with FireAndForget profile
    // This gives us: IPv4-only support, proper timeouts, CA bundle, etc.
    CurlNetworkConfig::instance().applyCurlSettings(
        _c, 
        CurlNetworkConfig::FetchProfile::FireAndForget
    );
    
    // Telemetry-specific settings
    curl_easy_setopt(_c, CURLOPT_WRITEFUNCTION, &DownloadStatsTelemetry::_curl_write_callback);
    curl_easy_setopt(_c, CURLOPT_HEADERFUNCTION, &DownloadStatsTelemetry::_curl_header_callback);
    curl_easy_setopt(_c, CURLOPT_URL, _url.constData());
    curl_easy_setopt(_c, CURLOPT_POSTFIELDSIZE, _postfields.length());
    curl_easy_setopt(_c, CURLOPT_POSTFIELDS, _postfields.constData());

    CURLcode ret = curl_easy_perform(_c);
    curl_easy_cleanup(_c);

    if (ret == CURLE_OK) {
        qDebug() << "Telemetry sent successfully";
    } else {
        qDebug() << "Telemetry failed:" << curl_easy_strerror(ret);
    }
}

/* /dev/null write handler */
size_t DownloadStatsTelemetry::_curl_write_callback(char *, size_t size, size_t nmemb, void *)
{
    return size * nmemb;
}

size_t DownloadStatsTelemetry::_curl_header_callback( void *ptr, size_t size, size_t nmemb, void *)
{
    int len = size*nmemb;
    return len;
}
