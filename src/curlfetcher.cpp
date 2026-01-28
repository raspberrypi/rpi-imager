/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "curlfetcher.h"
#include "curlnetworkconfig.h"

#include <QRunnable>
#include <QPointer>
#include <QDebug>
#include <curl/curl.h>
#include <cstring>

/**
 * Worker task that fetches data using libcurl in a thread pool.
 * Uses QPointer to safely handle case where CurlFetcher is destroyed during fetch.
 */
class CurlFetchTask : public QRunnable
{
public:
    CurlFetchTask(CurlFetcher *fetcher, const QUrl &url)
        : _fetcher(fetcher), _url(url)
    {
        setAutoDelete(true);
    }
    
    void run() override
    {
        // Check if fetcher was destroyed or cancelled
        if (!_fetcher || _fetcher->isCancelled()) {
            deliverResult(QByteArray(), QStringLiteral("Cancelled"), QString());
            return;
        }
        
        QByteArray data;
        QString error;
        QString stats;
        char errorBuffer[CURL_ERROR_SIZE] = {0};
        
        CURL *curl = curl_easy_init();
        if (!curl) {
            error = QStringLiteral("Failed to initialize curl");
            deliverResult(data, error, stats);
            return;
        }
        
        // Apply shared network configuration with SmallFile profile (JSON/sublists)
        CurlNetworkConfig::instance().applyCurlSettings(
            curl, 
            CurlNetworkConfig::FetchProfile::SmallFile,
            errorBuffer
        );
        
        // Set URL
        QByteArray urlBytes = _url.toEncoded();
        curl_easy_setopt(curl, CURLOPT_URL, urlBytes.constData());
        
        // Set up write callback to capture response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        
        // Set up progress callback for cancellation support
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        // Track the effective URL after redirects (empty string if not available)
        QString effectiveUrlStr;
        
        if (res == CURLE_ABORTED_BY_CALLBACK) {
            error = QStringLiteral("Cancelled");
        } else if (res != CURLE_OK) {
            // Build detailed error message
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            
            QString curlError = errorBuffer[0] ? QString::fromUtf8(errorBuffer) 
                                               : QString::fromUtf8(curl_easy_strerror(res));
            
            if (httpCode > 0) {
                error = QString("HTTP %1: %2").arg(httpCode).arg(curlError);
            } else {
                error = curlError;
            }
            qDebug() << "CurlFetcher: fetch failed for" << _url.toString() << "-" << error;
        } else {
            // Success - get the effective URL after any redirects
            char *effectiveUrl = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
            if (effectiveUrl) {
                // Compare raw C strings first to avoid QString construction when no redirect occurred
                QByteArray originalUrlBytes = _url.toEncoded();
                if (strcmp(effectiveUrl, originalUrlBytes.constData()) != 0) {
                    effectiveUrlStr = QString::fromUtf8(effectiveUrl);
                    qDebug() << "CurlFetcher: URL was redirected from" << _url.toString() << "to" << effectiveUrlStr;
                }
            }
            
            // Collect CURL connection timing metrics for performance analysis
            double dnsTime = 0, connectTime = 0, tlsTime = 0, startTransferTime = 0, totalTime = 0;
            curl_off_t downloadSpeed = 0;
            curl_off_t downloadSize = 0;
            long httpVersion = 0;
            
            curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &dnsTime);
            curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connectTime);
            curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &tlsTime);
            curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &startTransferTime);
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
            curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD_T, &downloadSpeed);
            curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloadSize);
            curl_easy_getinfo(curl, CURLINFO_HTTP_VERSION, &httpVersion);
            
            const char* versionStr = "unknown";
            switch (httpVersion) {
                case CURL_HTTP_VERSION_1_0: versionStr = "HTTP/1.0"; break;
                case CURL_HTTP_VERSION_1_1: versionStr = "HTTP/1.1"; break;
                case CURL_HTTP_VERSION_2_0: versionStr = "HTTP/2"; break;
                case CURL_HTTP_VERSION_3: versionStr = "HTTP/3"; break;
                default: break;
            }
            
            // Format stats the same way as DownloadThread for consistency
            // Use asprintf for single allocation instead of 8 arg() calls
            stats = QString::asprintf(
                "dns_ms: %d; connect_ms: %d; tls_ms: %d; ttfb_ms: %d; total_ms: %d; speed_kbps: %lld; size_bytes: %lld; http: %s",
                static_cast<int>(dnsTime * 1000),
                static_cast<int>(connectTime * 1000),
                static_cast<int>(tlsTime * 1000),
                static_cast<int>(startTransferTime * 1000),
                static_cast<int>(totalTime * 1000),
                static_cast<long long>(downloadSpeed / 1024),
                static_cast<long long>(downloadSize),
                versionStr);
            
            qDebug() << "CurlFetcher: fetched" << data.size() << "bytes from" << _url.host() << "using" << versionStr;
        }
        
        curl_easy_cleanup(curl);
        deliverResult(data, error, stats, effectiveUrlStr);
    }
    
private:
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *buffer = static_cast<QByteArray*>(userdata);
        size_t totalSize = size * nmemb;
        buffer->append(ptr, static_cast<qsizetype>(totalSize));
        return totalSize;
    }
    
    static int progressCallback(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        auto *task = static_cast<CurlFetchTask*>(clientp);
        // Return non-zero to abort the transfer if fetcher is gone or cancelled
        if (!task->_fetcher) {
            return 1;  // Fetcher destroyed, abort
        }
        return task->_fetcher->isCancelled() ? 1 : 0;
    }
    
    void deliverResult(const QByteArray &data, const QString &error, const QString &stats, const QString &effectiveUrl = QString())
    {
        // Only deliver if fetcher still exists
        if (_fetcher) {
            QMetaObject::invokeMethod(_fetcher.data(), "onFetchComplete",
                                      Qt::QueuedConnection,
                                      Q_ARG(QByteArray, data),
                                      Q_ARG(QString, error),
                                      Q_ARG(QString, stats),
                                      Q_ARG(QString, effectiveUrl));
        }
    }
    
    QPointer<CurlFetcher> _fetcher;  // QPointer to detect fetcher destruction
    QUrl _url;
};

// ----------------------------------------------------------------------------
// CurlFetcher
// ----------------------------------------------------------------------------

CurlFetcher::CurlFetcher(QObject *parent)
    : QObject(parent)
{
}

CurlFetcher::~CurlFetcher() = default;

void CurlFetcher::fetch(const QUrl &url)
{
    // Security: Only allow HTTP/HTTPS/file schemes.
    // file:// is needed for local repositories.
    // SSRF via redirect (http->file) is blocked by CURLOPT_REDIR_PROTOCOLS.
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") 
        && scheme != QLatin1String("file")) {
        qWarning() << "CurlFetcher: Rejecting unsupported URL scheme:" << scheme;
        QMetaObject::invokeMethod(this, "onFetchComplete",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, QByteArray()),
                                  Q_ARG(QString, QStringLiteral("Unsupported URL scheme")),
                                  Q_ARG(QString, QString()),
                                  Q_ARG(QString, QString()));
        return;
    }
    
    _url = url;
    _cancelled.store(false, std::memory_order_relaxed);
    
    // Start fetch in dedicated network thread pool (not global pool)
    auto *task = new CurlFetchTask(this, url);
    CurlNetworkConfig::instance().networkThreadPool()->start(task);
}

void CurlFetcher::cancel()
{
    _cancelled.store(true, std::memory_order_relaxed);
}

void CurlFetcher::onFetchComplete(const QByteArray &data, const QString &errorMsg, const QString &stats, const QString &effectiveUrl)
{
    // Emit connection stats if available (for performance tracking)
    if (!stats.isEmpty()) {
        emit connectionStats(stats, _url);
    }
    
    if (!errorMsg.isEmpty()) {
        emit error(errorMsg, _url);
    } else {
        // Use effective URL if available, otherwise fall back to original URL
        QUrl finalUrl = effectiveUrl.isEmpty() ? _url : QUrl(effectiveUrl);
        emit finished(data, _url, finalUrl);
    }
    
    // Auto-delete after delivering result
    deleteLater();
}
