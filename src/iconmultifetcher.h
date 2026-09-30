/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#ifndef ICONMULTIFETCHER_H
#define ICONMULTIFETCHER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QUrl>
#include <QByteArray>
#include <QHash>
#include <QQueue>
#include <QList>
#include <QSet>
#include <QPointer>
#include <atomic>
#include <curl/curl.h>

class IconImageResponse;

/**
 * Manages concurrent icon downloads using curl_multi with in-memory caching.
 * 
 * This singleton runs a dedicated thread with an event loop that efficiently
 * handles many small downloads using a single curl_multi handle. Benefits:
 * 
 * - In-memory cache: Same icon URL returns instantly from cache
 * - HTTP/2 multiplexing: Multiple icons from the same host share one connection
 * - Connection pooling: Reuses TCP connections across requests
 * - Controlled concurrency: Limits parallel connections to avoid overwhelming servers
 * - Single thread: No thread pool contention for icon fetches
 * - Coalescing: Multiple requests for the same URL share one network fetch
 * 
 * Usage:
 *   IconMultiFetcher::instance().queueFetch(response, url);
 *   IconMultiFetcher::instance().cancelFetch(response);
 */
class IconMultiFetcher : public QObject
{
    Q_OBJECT
public:
    static IconMultiFetcher& instance();
    
    /**
     * Queue an icon URL for fetching.
     * If the URL is cached, delivers result immediately.
     * If another request for the same URL is in flight, coalesces with it.
     * Results are delivered via the response's onFetchComplete slot.
     * Thread-safe.
     */
    void queueFetch(quint64 requestId, const QUrl &url);
    
    /**
     * Cancel a pending fetch. If the fetch is in progress, it will be aborted.
     * Thread-safe.
     */
    void cancelFetch(quint64 requestId);
    
    /**
     * Clear the in-memory icon cache.
     * Useful when switching OS list repositories.
     */
    void clearCache();
    
    /**
     * Get cached data by URL key. Returns empty QByteArray if not cached.
     * Thread-safe. Used by IconImageResponse to read data without copying
     * through the signal system.
     */
    QByteArray getCachedData(const QString &urlKey) const;
    
    /**
     * Shutdown the fetcher thread. Called during application exit.
     */
    void shutdown();
    
    ~IconMultiFetcher() override;


    // Cache configuration
    static constexpr qsizetype MaxCacheBytes = 32 * 1024 * 1024; // 32 MB cache limit
    static constexpr int MaxCacheEntries = 500; // Also limit entry count

signals:
    /*
     * A fetch has finished, named by the id its caller gave.
     *
     * An id and two strings, never a pointer. The fetcher used to hold
     * QPointers to IconImageResponse and null-check them before posting --
     * which is not a guard: QObject clears a QPointer without taking this
     * class's mutex, so the response could go between the test and the call,
     * and the test itself races the destruction. With an id there is nothing
     * to dereference here and nothing to lock; whoever owns the responses
     * looks the id up on its own thread.
     */
    void fetchFinished(quint64 requestId, const QString &urlKey,
                       const QString &error);

private:
    explicit IconMultiFetcher(QObject *parent = nullptr);
    IconMultiFetcher(const IconMultiFetcher&) = delete;
    IconMultiFetcher& operator=(const IconMultiFetcher&) = delete;
    
    /**
     * Runs in the dedicated fetcher thread.
     */
    void runEventLoop();
    
    /**
     * Process pending requests (called within event loop).
     */
    void processPendingRequests();
    
    /**
     * Process completed transfers and deliver results.
     */
    void processCompletedTransfers();
    
    /**
     * Create and configure a CURL easy handle for an icon fetch.
     * @param url The URL to fetch
     * @param urlKey Pre-computed url.toString() to avoid allocation
     */
    CURL* createEasyHandle(const QUrl &url, const QString &urlKey);
    
    /**
     * Clean up a transfer (remove from multi, cleanup easy handle).
     */
    void cleanupTransfer(CURL *easy);
    
    /**
     * Add data to cache, evicting old entries if needed.
     * Must be called with _mutex held.
     */
    void addToCache(const QString &urlKey, const QByteArray &data);
    
    /**
     * Evict oldest entries until cache is within limits.
     * Must be called with _mutex held.
     */
    void evictIfNeeded();
    
    // Write callback for curl
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    
    // Header callback to pre-allocate buffer based on Content-Length
    static size_t headerCallback(char *buffer, size_t size, size_t nitems, void *userdata);
    
    // Dedicated thread for curl_multi event loop
    QThread *_thread = nullptr;
    
    // curl_multi handle (only accessed from _thread)
    CURLM *_multi = nullptr;
    
    // Pending requests queue (protected by _mutex)
    struct PendingRequest {
        quint64 id = 0;
        QUrl url;
        QString urlKey;  // Pre-computed to avoid repeated QUrl::toString() allocations
    };
    QQueue<PendingRequest> _pendingRequests;
    
    // Cancellation set (protected by _mutex)
    QSet<quint64> _cancelledResponses;
    
    // Active transfers: easy handle -> transfer data (only accessed from _thread)
    struct TransferData {
        QUrl url;
        QString urlKey;  // Pre-computed to avoid repeated allocations
        QByteArray buffer;
        char errorBuffer[CURL_ERROR_SIZE];
        QList<quint64> waitingIds;   // ids, so nothing here can dangle
    };
    QHash<CURL*, TransferData*> _activeTransfers;
    
    // In-flight URLs: URL -> easy handle (for coalescing, only accessed from _thread)
    QHash<QString, CURL*> _inFlightUrls;
    
    // In-memory cache (protected by _mutex)
    struct CacheEntry {
        QByteArray data;
        QString errorMsg; // Non-empty if fetch failed (cache negative results briefly)
    };
    QHash<QString, CacheEntry> _cache;
    QList<QString> _cacheOrder; // LRU order: oldest at front
    qsizetype _cacheBytes = 0;
    
    // Synchronization
    mutable QMutex _mutex;  // mutable for const getCachedData()
    QWaitCondition _hasWork;
    std::atomic<bool> _shutdown{false};
};

#endif // ICONMULTIFETCHER_H
