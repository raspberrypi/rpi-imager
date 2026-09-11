/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "downloadstatstelemetry.h"
#include "imager_version.h"
#include "curlnetworkconfig.h"
#include "config.h"
#include <QSettings>
#include <QDebug>
#include <QUrl>
#include <QSysInfo>
#include <QLocale>
#include <QFile>
#include <QRegularExpression>

DownloadStatsTelemetry::DownloadStatsTelemetry(const QByteArray &url, const QByteArray &parentcategory, const QByteArray &osname, bool embedded, const QString &imagerLang, QObject *parent, const QByteArray &endpoint)
    : QThread(parent), _url(endpoint)
{
    QLocale locale;
    
    // Extract clean numeric version (X.Y.Z, or X.Y.Z.W for a hotfix release) from
    // IMAGER_VERSION_STR for telemetry.
    // Handles formats like: v2.0.0, v2.0.0-rc4-60-geac7c2f0, 2.0.0, v2.0.11.1, etc.
    // The fourth component must be kept: without it a hotfix is indistinguishable
    // from the release it fixes, so uptake of the fix cannot be measured.
    QString versionStr(IMAGER_VERSION_STR);
    static QRegularExpression versionRx("^v?([0-9]+\\.[0-9]+\\.[0-9]+(?:\\.[0-9]+)?)");
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
    if (f.open(f.ReadOnly)) {
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
    }
#endif
}

DownloadStatsTelemetry::~DownloadStatsTelemetry()
{
    // QThread's destructor calls qFatal() if the thread is still running, so
    // a telemetry POST still in flight when the owning ImageWriter goes away
    // takes the whole process down with it. That is reachable on any quit
    // that follows a download, and on the error paths where the report is
    // sent and the writer is torn down immediately afterwards.
    //
    // The request is a single curl_easy_perform() on the FireAndForget
    // profile, so it is already short-timeout bounded; waiting is normally
    // instant. If it somehow is not, killing one fire-and-forget telemetry
    // thread is a better outcome than aborting.
    if (!isRunning())
        return;

    if (wait(10000))
        return;

    terminate();
    wait(2000);
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
