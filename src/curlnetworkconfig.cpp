/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#include "curlnetworkconfig.h"
#include "imager_version.h"
#include "config.h"
#include "platformquirks.h"
#include <QMutexLocker>
#include <QDebug>
#if __has_include(<QNetworkProxy>)
#define HAS_QT_NETWORK_PROXY
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#endif

// Static member initialization
std::atomic<bool> CurlNetworkConfig::_curlInitialized{false};

void CurlNetworkConfig::ensureInitialized()
{
    // Double-checked locking pattern for thread-safe initialization
    if (!_curlInitialized.load(std::memory_order_acquire)) {
        static QMutex initMutex;
        QMutexLocker locker(&initMutex);
        if (!_curlInitialized.load(std::memory_order_relaxed)) {
            CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (result != CURLE_OK) {
                qWarning() << "curl_global_init failed:" << curl_easy_strerror(result);
            } else {
                qDebug() << "libcurl initialized globally";
            }
            _curlInitialized.store(true, std::memory_order_release);
        }
    }
}

CurlNetworkConfig& CurlNetworkConfig::instance()
{
    static CurlNetworkConfig instance;
    return instance;
}

CurlNetworkConfig::CurlNetworkConfig()
{
    // Set default user agent
    _userAgent = "Mozilla/5.0 rpi-imager/" IMAGER_VERSION_STR;
    
    // Create dedicated network thread pool
    _networkPool = new QThreadPool();
    _networkPool->setMaxThreadCount(MaxConcurrentFetches);
    qDebug() << "Network thread pool created with" << MaxConcurrentFetches << "threads";
}

CurlNetworkConfig::~CurlNetworkConfig()
{
    if (_networkPool) {
        _networkPool->waitForDone();
        delete _networkPool;
    }
    
    // Note: We don't call curl_global_cleanup() here because other code
    // (DownloadThread) may still be using curl. The cleanup happens at
    // process exit which is fine for our use case.
}

QThreadPool* CurlNetworkConfig::networkThreadPool()
{
    return _networkPool;
}

QByteArray CurlNetworkConfig::proxy() const
{
    QMutexLocker locker(&_mutex);
    return _proxy;
}

void CurlNetworkConfig::setProxy(const QByteArray &proxy)
{
    QMutexLocker locker(&_mutex);
    if (_proxy != proxy) {
        _proxy = proxy;
        qDebug() << "CurlNetworkConfig: proxy set to" << (proxy.isEmpty() ? "(none)" : proxy);
    }
}

QByteArray CurlNetworkConfig::userAgent() const
{
    QMutexLocker locker(&_mutex);
    return _userAgent;
}

void CurlNetworkConfig::setUserAgent(const QByteArray &ua)
{
    QMutexLocker locker(&_mutex);
    _userAgent = ua;
}

void CurlNetworkConfig::detectSystemProxy(const QUrl &url)
{
#ifdef HAS_QT_NETWORK_PROXY
    QMutexLocker locker(&_mutex);
    if (_proxyDetected || !_proxy.isEmpty()) {
        return;  // Already have a proxy configured (either detected or user-set)
    }
    _proxyDetected = true;  // Only attempt detection once

    QNetworkProxyQuery npq{url};
    QList<QNetworkProxy> proxyList = QNetworkProxyFactory::systemProxyForQuery(npq);
    if (!proxyList.isEmpty()) {
        QNetworkProxy proxy = proxyList.first();
        if (proxy.type() != QNetworkProxy::NoProxy) {
            QUrl proxyUrl;
            proxyUrl.setScheme(proxy.type() == QNetworkProxy::Socks5Proxy ? "socks5h" : "http");
            proxyUrl.setHost(proxy.hostName());
            proxyUrl.setPort(proxy.port());

            if (!proxy.user().isEmpty()) {
                proxyUrl.setUserName(proxy.user());
                proxyUrl.setPassword(proxy.password());
            }

            _proxy = proxyUrl.toEncoded();
            qDebug() << "System proxy detected:" << proxyUrl.host() << ":" << proxyUrl.port();
        }
    }
#else
    Q_UNUSED(url)
#endif
}

void CurlNetworkConfig::applyCurlSettings(CURL *curl, FetchProfile profile, char *errorBuffer) const
{
    if (!curl) return;
    
    // =========================================================================
    // Thread safety - REQUIRED for all multi-threaded curl usage
    // =========================================================================
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    
    // =========================================================================
    // Error handling
    // =========================================================================
    // Return error on HTTP 4xx/5xx responses
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    
    // Detailed error buffer if provided
    if (errorBuffer) {
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    }
    
    // =========================================================================
    // Redirect handling
    // =========================================================================
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    
    // Security: Only allow redirects to HTTP/HTTPS to prevent SSRF
    // via redirect to file://, gopher://, etc.
#if LIBCURL_VERSION_NUM >= 0x075500  // 7.85.0
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    
    // =========================================================================
    // Timeouts - profile-specific
    // =========================================================================
    switch (profile) {
        case FetchProfile::SmallFile:
            // Icons, JSON files - moderate timeouts, size limit for safety
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);   // Stall detection after 30s
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L); // Below 100 B/s = stalled
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);          // Total timeout 60s
            // Safety limit: 10MB max for JSON/icons (prevents OOM from malicious servers)
            curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(10 * 1024 * 1024));
            break;
            
        case FetchProfile::LargeFile:
            // OS images - longer timeouts, TCP keepalive
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);   // Stall detection after 60s
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L); // Below 100 B/s = stalled
            // No total timeout - large files can take hours
            
            // TCP keepalive to detect dead connections
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);     // Start keepalive after 30s idle
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);    // Send keepalive every 15s
            
            // Enable HTTP/2 for HTTPS connections (better for large transfers)
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            break;
            
        case FetchProfile::FireAndForget:
            // Telemetry - short timeouts, we don't want to block
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 10L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);          // Total timeout 30s
            break;
    }
    
    // =========================================================================
    // IPv4-only mode (for users with broken IPv6 routing)
    // =========================================================================
    if (_ipv4Only.load(std::memory_order_relaxed)) {
        curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    }
    
    // =========================================================================
    // Proxy and User Agent
    // =========================================================================
    {
        QMutexLocker locker(&_mutex);
        if (!_proxy.isEmpty()) {
            curl_easy_setopt(curl, CURLOPT_PROXY, _proxy.constData());
        }
        
        if (!_userAgent.isEmpty()) {
            curl_easy_setopt(curl, CURLOPT_USERAGENT, _userAgent.constData());
        }
    }
    
    // =========================================================================
    // SSL/TLS - CA bundle for AppImage compatibility on Linux
    // =========================================================================
#ifdef Q_OS_LINUX
    const char* caBundle = PlatformQuirks::findCACertBundle();
    if (caBundle) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, caBundle);
    }
#endif
}
