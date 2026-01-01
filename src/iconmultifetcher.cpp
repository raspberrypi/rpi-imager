/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "iconmultifetcher.h"
#include "iconimageprovider.h"
#include "curlnetworkconfig.h"

#include <QDebug>

// Maximum concurrent connections for icon fetching
// This limits both total connections and connections per host
static constexpr long MAX_TOTAL_CONNECTIONS = 10;
static constexpr long MAX_HOST_CONNECTIONS = 6;

IconMultiFetcher& IconMultiFetcher::instance()
{
    static IconMultiFetcher instance;
    return instance;
}

IconMultiFetcher::IconMultiFetcher(QObject *parent)
    : QObject(parent)
{
    // Create and start the dedicated fetcher thread
    _thread = QThread::create([this]() { runEventLoop(); });
    _thread->setObjectName(QStringLiteral("IconMultiFetcher"));
    _thread->start();
}

IconMultiFetcher::~IconMultiFetcher()
{
    shutdown();
}

void IconMultiFetcher::shutdown()
{
    if (_shutdown.exchange(true)) {
        return; // Already shutting down
    }
    
    // Wake up the event loop
    {
        QMutexLocker locker(&_mutex);
        _hasWork.wakeAll();
    }
    
    // Wait for thread to finish
    if (_thread && _thread->isRunning()) {
        _thread->wait(5000); // 5 second timeout
        if (_thread->isRunning()) {
            qWarning() << "IconMultiFetcher: Thread did not exit cleanly, terminating";
            _thread->terminate();
            _thread->wait();
        }
    }
    
    delete _thread;
    _thread = nullptr;
}

void IconMultiFetcher::clearCache()
{
    QMutexLocker locker(&_mutex);
    _cache.clear();
    _cacheOrder.clear();
    _cacheBytes = 0;
    qDebug() << "IconMultiFetcher: Cache cleared";
}

QByteArray IconMultiFetcher::getCachedData(const QString &urlKey) const
{
    QMutexLocker locker(&_mutex);
    auto it = _cache.find(urlKey);
    if (it != _cache.end()) {
        return it->data;  // Returns a copy, but this is called once per icon
    }
    return QByteArray();
}

// Limit pending requests to prevent memory exhaustion DoS
static constexpr int MaxPendingRequests = 500;

void IconMultiFetcher::queueFetch(IconImageResponse *response, const QUrl &url)
{
    if (_shutdown.load()) {
        qWarning() << "IconMultiFetcher: Ignoring fetch request during shutdown";
        return;
    }
    
    // Pre-compute urlKey once to avoid repeated QString allocations
    const QString urlKey = url.toString();
    
    QMutexLocker locker(&_mutex);
    
    // DoS protection: reject if queue is full (cache hits still work)
    if (_pendingRequests.size() >= MaxPendingRequests) {
        locker.unlock();
        qWarning() << "IconMultiFetcher: Request queue full, rejecting:" << url.host();
        if (response) {
            QMetaObject::invokeMethod(response, "onFetchComplete",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("Request queue full")));
        }
        return;
    }
    
    // Check cache first
    auto cacheIt = _cache.find(urlKey);
    if (cacheIt != _cache.end()) {
        // Cache hit! Move to end of LRU order (TODO: optimize to O(1) with linked list)
        _cacheOrder.removeOne(urlKey);
        _cacheOrder.append(urlKey);
        
        // Deliver cache key - response will look up data directly (avoids copy)
        const QString errorMsg = cacheIt->errorMsg;
        locker.unlock();
        
        if (response) {  // Defensive check
            QMetaObject::invokeMethod(response, "onFetchComplete",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, urlKey),
                                      Q_ARG(QString, errorMsg));
        }
        return;
    }
    
    // Not in cache - queue for fetching with pre-computed urlKey
    _pendingRequests.enqueue({response, url, urlKey});
    _hasWork.wakeAll();
    
    // Wake up curl_multi_poll immediately to reduce latency (requires libcurl 7.68.0+)
#if LIBCURL_VERSION_NUM >= 0x074400
    if (_multi) {
        curl_multi_wakeup(_multi);
    }
#endif
}

void IconMultiFetcher::cancelFetch(IconImageResponse *response)
{
    QMutexLocker locker(&_mutex);
    _cancelledResponses.insert(response);
    _hasWork.wakeAll();
}

void IconMultiFetcher::runEventLoop()
{
    qDebug() << "IconMultiFetcher: Event loop starting";
    
    // Initialize curl_multi handle
    _multi = curl_multi_init();
    if (!_multi) {
        qCritical() << "IconMultiFetcher: Failed to initialize curl_multi";
        return;
    }
    
    // Configure multi handle for optimal icon fetching
    curl_multi_setopt(_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, MAX_TOTAL_CONNECTIONS);
    curl_multi_setopt(_multi, CURLMOPT_MAX_HOST_CONNECTIONS, MAX_HOST_CONNECTIONS);
    
    // Enable HTTP/2 multiplexing
    curl_multi_setopt(_multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    
    while (!_shutdown.load()) {
        // Process any pending requests
        processPendingRequests();
        
        // Run curl_multi_perform
        int runningHandles = 0;
        CURLMcode mc = curl_multi_perform(_multi, &runningHandles);
        if (mc != CURLM_OK) {
            qWarning() << "IconMultiFetcher: curl_multi_perform error:" << curl_multi_strerror(mc);
        }
        
        // Process completed transfers
        processCompletedTransfers();
        
        // Wait for activity or new requests
        // Use a short timeout so we can check for new pending requests
        int numfds = 0;
        mc = curl_multi_poll(_multi, nullptr, 0, 100, &numfds); // 100ms timeout
        if (mc != CURLM_OK) {
            qWarning() << "IconMultiFetcher: curl_multi_poll error:" << curl_multi_strerror(mc);
        }
        
        // Also check if we have pending work
        {
            QMutexLocker locker(&_mutex);
            if (_pendingRequests.isEmpty() && _activeTransfers.isEmpty() && !_shutdown.load()) {
                // No work to do, wait for new requests
                _hasWork.wait(&_mutex, 500); // Wake up every 500ms to check shutdown
            }
        }
    }
    
    // Cleanup: abort all active transfers
    for (auto it = _activeTransfers.begin(); it != _activeTransfers.end(); ++it) {
        CURL *easy = it.key();
        TransferData *data = it.value();
        curl_multi_remove_handle(_multi, easy);
        curl_easy_cleanup(easy);
        delete data;
    }
    _activeTransfers.clear();
    _inFlightUrls.clear();
    
    curl_multi_cleanup(_multi);
    _multi = nullptr;
    
    qDebug() << "IconMultiFetcher: Event loop exiting";
}

void IconMultiFetcher::processPendingRequests()
{
    QMutexLocker locker(&_mutex);
    
    // First, handle cancellations for pending requests
    QQueue<PendingRequest> stillPending;
    while (!_pendingRequests.isEmpty()) {
        PendingRequest req = _pendingRequests.dequeue();
        
        // Skip if response was deleted
        if (!req.response) {
            continue;
        }
        
        if (_cancelledResponses.contains(req.response.data())) {
            // Don't start cancelled requests
            _cancelledResponses.remove(req.response.data());
            // Notify of cancellation (empty urlKey signals no data available)
            locker.unlock();
            if (req.response) {  // Re-check after releasing lock
                QMetaObject::invokeMethod(req.response.data(), "onFetchComplete",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, QString()),
                                          Q_ARG(QString, QStringLiteral("Cancelled")));
            }
            locker.relock();
        } else {
            stillPending.enqueue(req);
        }
    }
    
    // Now process remaining pending requests
    while (!stillPending.isEmpty()) {
        PendingRequest req = stillPending.dequeue();
        
        // Skip if response was deleted while waiting
        if (!req.response) {
            continue;
        }
        
        // Use pre-computed urlKey (no QString allocation here)
        const QString &urlKey = req.urlKey;
        
        // Check if this URL is already being fetched (coalescing)
        auto inFlightIt = _inFlightUrls.find(urlKey);
        if (inFlightIt != _inFlightUrls.end()) {
            // Add this response to the existing transfer's waiting list
            CURL *existingEasy = inFlightIt.value();
            auto transferIt = _activeTransfers.find(existingEasy);
            if (transferIt != _activeTransfers.end()) {
                transferIt.value()->waitingResponses.append(req.response);
                continue; // No new fetch needed
            }
        }
        
        locker.unlock();
        CURL *easy = createEasyHandle(req.url, urlKey);
        locker.relock();
        
        if (easy) {
            // Add response to waiting list
            _activeTransfers[easy]->waitingResponses.append(req.response);
            _inFlightUrls[urlKey] = easy;
            
            CURLMcode mc = curl_multi_add_handle(_multi, easy);
            if (mc != CURLM_OK) {
                qWarning() << "IconMultiFetcher: Failed to add handle:" << curl_multi_strerror(mc);
                _inFlightUrls.remove(urlKey);
                locker.unlock();
                cleanupTransfer(easy);
                if (req.response) {  // Re-check after releasing lock
                    QMetaObject::invokeMethod(req.response.data(), "onFetchComplete",
                                              Qt::QueuedConnection,
                                              Q_ARG(QString, QString()),
                                              Q_ARG(QString, QStringLiteral("Failed to start transfer")));
                }
                locker.relock();
            }
        }
    }
    
    // Handle cancellations for active transfers
    QSet<IconImageResponse*> toCancel = _cancelledResponses;
    _cancelledResponses.clear();
    locker.unlock();
    
    // Remove cancelled/deleted responses from waiting lists
    for (auto it = _activeTransfers.begin(); it != _activeTransfers.end(); ) {
        TransferData *data = it.value();
        
        // Remove cancelled or deleted responses from waiting list
        for (auto respIt = data->waitingResponses.begin(); respIt != data->waitingResponses.end(); ) {
            bool shouldRemove = !(*respIt) || toCancel.contains(respIt->data());
            if (shouldRemove) {
                if (*respIt) {  // Only notify if not deleted
                    QMetaObject::invokeMethod(respIt->data(), "onFetchComplete",
                                              Qt::QueuedConnection,
                                              Q_ARG(QString, QString()),
                                              Q_ARG(QString, QStringLiteral("Cancelled")));
                }
                respIt = data->waitingResponses.erase(respIt);
            } else {
                ++respIt;
            }
        }
        
        // If no responses are waiting anymore, cancel the transfer
        if (data->waitingResponses.isEmpty()) {
            CURL *easy = it.key();
            _inFlightUrls.remove(data->urlKey);  // Use pre-computed key
            curl_multi_remove_handle(_multi, easy);
            curl_easy_cleanup(easy);
            delete data;
            it = _activeTransfers.erase(it);
        } else {
            ++it;
        }
    }
}

void IconMultiFetcher::processCompletedTransfers()
{
    CURLMsg *msg;
    int msgsLeft;
    
    while ((msg = curl_multi_info_read(_multi, &msgsLeft))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            CURLcode result = msg->data.result;
            
            auto it = _activeTransfers.find(easy);
            if (it == _activeTransfers.end()) {
                qWarning() << "IconMultiFetcher: Completed transfer not found in active transfers";
                continue;
            }
            
            TransferData *data = it.value();
            QString errorMsg;
            
            if (result != CURLE_OK) {
                errorMsg = data->errorBuffer[0] 
                    ? QString::fromUtf8(data->errorBuffer)
                    : QString::fromUtf8(curl_easy_strerror(result));
                qDebug() << "IconMultiFetcher: Fetch failed for" << data->url.host() << "-" << errorMsg;
            }
            
            // Add to cache (including failures, to avoid retrying broken URLs)
            // Note: Negative caching is intentional for icons because:
            // 1. Broken icon URLs are typically permanent (wrong URL in OS list)
            // 2. Retrying 50 broken URLs on every scroll would be wasteful
            // 3. Cache is cleared when switching repositories anyway
            // TODO: Consider adding TTL for negative cache entries if needed
            {
                QMutexLocker locker(&_mutex);
                addToCache(data->urlKey, data->buffer);  // Use pre-computed key
                if (!errorMsg.isEmpty()) {
                    // Update cache entry with error message
                    auto cacheIt = _cache.find(data->urlKey);
                    if (cacheIt != _cache.end()) {
                        cacheIt->errorMsg = errorMsg;
                    }
                }
            }
            
            // Deliver cache key to all waiting responses (they look up data directly)
            for (const QPointer<IconImageResponse> &response : data->waitingResponses) {
                if (response) {
                    QMetaObject::invokeMethod(response.data(), "onFetchComplete",
                                              Qt::QueuedConnection,
                                              Q_ARG(QString, data->urlKey),
                                              Q_ARG(QString, errorMsg));
                }
            }
            
            // Cleanup - use pre-computed urlKey
            _inFlightUrls.remove(data->urlKey);
            curl_multi_remove_handle(_multi, easy);
            curl_easy_cleanup(easy);
            delete data;
            _activeTransfers.erase(it);
        }
    }
}

CURL* IconMultiFetcher::createEasyHandle(const QUrl &url, const QString &urlKey)
{
    // Security: Only allow HTTP/HTTPS/file schemes.
    // file:// is needed for local repositories with local icons.
    // The user explicitly trusts the repository they've configured.
    // SSRF via redirect (http->file) is blocked by CURLOPT_REDIR_PROTOCOLS in applyCurlSettings.
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https") 
        && scheme != QLatin1String("file")) {
        qWarning() << "IconMultiFetcher: Rejecting unsupported URL scheme:" << scheme;
        return nullptr;
    }
    
    CURL *easy = curl_easy_init();
    if (!easy) {
        qWarning() << "IconMultiFetcher: Failed to create easy handle";
        return nullptr;
    }
    
    // Create transfer data with pre-computed urlKey
    auto *data = new TransferData;
    data->url = url;
    data->urlKey = urlKey;  // Store to avoid re-computing later
    memset(data->errorBuffer, 0, CURL_ERROR_SIZE);
    
    // Apply shared network configuration (SmallFile profile for icons)
    CurlNetworkConfig::instance().applyCurlSettings(
        easy,
        CurlNetworkConfig::FetchProfile::SmallFile,
        data->errorBuffer
    );
    
    // Set URL (curl copies the string internally, so urlBytes can go out of scope)
    QByteArray urlBytes = url.toEncoded();
    curl_easy_setopt(easy, CURLOPT_URL, urlBytes.constData());
    
    // Set up write callback
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, data);
    
    // Set up header callback to pre-allocate buffer based on Content-Length
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, data);
    
    // Store in active transfers map
    _activeTransfers[easy] = data;
    
    return easy;
}

void IconMultiFetcher::cleanupTransfer(CURL *easy)
{
    auto it = _activeTransfers.find(easy);
    if (it != _activeTransfers.end()) {
        delete it.value();
        _activeTransfers.erase(it);
    }
    curl_easy_cleanup(easy);
}

void IconMultiFetcher::addToCache(const QString &urlKey, const QByteArray &data)
{
    // Already have mutex from caller
    
    // Check if already cached (shouldn't happen, but be safe)
    if (_cache.contains(urlKey)) {
        return;
    }
    
    // Evict if needed before adding
    _cacheBytes += data.size();
    evictIfNeeded();
    
    // Add to cache
    _cache[urlKey] = {data, QString()};
    _cacheOrder.append(urlKey);
}

void IconMultiFetcher::evictIfNeeded()
{
    // Already have mutex from caller
    
    while ((_cacheBytes > MaxCacheBytes || _cache.size() > MaxCacheEntries) 
           && !_cacheOrder.isEmpty()) {
        // Evict oldest entry
        QString oldest = _cacheOrder.takeFirst();
        auto it = _cache.find(oldest);
        if (it != _cache.end()) {
            _cacheBytes -= it->data.size();
            _cache.erase(it);
        }
    }
}

size_t IconMultiFetcher::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *data = static_cast<TransferData*>(userdata);
    size_t totalSize = size * nmemb;
    data->buffer.append(ptr, static_cast<qsizetype>(totalSize));
    return totalSize;
}

size_t IconMultiFetcher::headerCallback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    size_t totalSize = size * nitems;
    auto *data = static_cast<TransferData*>(userdata);
    
    // Parse Content-Length header to pre-allocate buffer
    // Format: "Content-Length: 12345\r\n"
    QByteArray header(buffer, static_cast<qsizetype>(totalSize));
    if (header.startsWith("Content-Length:") || header.startsWith("content-length:")) {
        // Parse the length value
        int colonPos = header.indexOf(':');
        if (colonPos > 0) {
            bool ok = false;
            qint64 contentLength = header.mid(colonPos + 1).trimmed().toLongLong(&ok);
            
            if (ok && contentLength > 0 && contentLength < 10 * 1024 * 1024) {  // Sanity limit: 10MB
                // Pre-allocate buffer to avoid reallocations during download
                data->buffer.reserve(static_cast<qsizetype>(contentLength));
            }
        }
    }
    
    return totalSize;  // Must return total size to continue
}
