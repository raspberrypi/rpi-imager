/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */

#ifndef CURLFETCHER_H
#define CURLFETCHER_H

#include <QObject>
#include <QUrl>
#include <QByteArray>
#include <atomic>

/**
 * Asynchronous data fetcher using libcurl.
 * 
 * Fetches data from a URL in a background thread and emits signals when complete.
 * Respects the shared CurlNetworkConfig settings (IPv4-only mode, proxy, etc.)
 * 
 * Usage:
 *   auto *fetcher = new CurlFetcher(this);
 *   connect(fetcher, &CurlFetcher::finished, this, &MyClass::onDataReceived);
 *   connect(fetcher, &CurlFetcher::error, this, &MyClass::onError);
 *   fetcher->fetch(QUrl("https://example.com/data.json"));
 * 
 * Cancellation:
 *   fetcher->cancel();  // Will emit error("Cancelled", url) when the fetch stops
 * 
 * The fetcher auto-deletes after emitting finished or error.
 */
class CurlFetcher : public QObject
{
    Q_OBJECT
public:
    explicit CurlFetcher(QObject *parent = nullptr);
    ~CurlFetcher() override;
    
    /**
     * Start fetching data from the given URL.
     * 
     * The fetch runs asynchronously. When complete, either finished() or error()
     * will be emitted, and the object will auto-delete.
     * 
     * @param url The URL to fetch
     */
    void fetch(const QUrl &url);
    
    /**
     * Cancel an in-progress fetch.
     * 
     * If the fetch is still running, it will be aborted and error() will be
     * emitted with "Cancelled" message.
     */
    void cancel();
    
    /**
     * Check if cancellation has been requested.
     */
    bool isCancelled() const { return _cancelled.load(std::memory_order_relaxed); }
    
    /**
     * Get the URL being fetched (for identification in callbacks).
     */
    QUrl url() const { return _url; }

signals:
    /**
     * Emitted when the fetch completes successfully.
     * @param data The downloaded data
     * @param url The original URL that was requested (for identification)
     * @param effectiveUrl The final URL after any redirects (may differ from url)
     */
    void finished(const QByteArray &data, const QUrl &url, const QUrl &effectiveUrl);
    
    /**
     * Emitted when the fetch fails or is cancelled.
     * @param errorMessage Description of the error
     * @param url The URL that was being fetched
     */
    void error(const QString &errorMessage, const QUrl &url);
    
    /**
     * Emitted with CURL connection timing stats for performance analysis.
     * Format: "dns_ms: X; connect_ms: X; tls_ms: X; ttfb_ms: X; total_ms: X; speed_kbps: X; size_bytes: X; http: X"
     * @param statsMetadata Formatted connection statistics
     * @param url The URL that was fetched
     */
    void connectionStats(const QString &statsMetadata, const QUrl &url);

public slots:
    // Internal: called from worker thread
    void onFetchComplete(const QByteArray &data, const QString &error, const QString &stats, const QString &effectiveUrl);

private:
    QUrl _url;
    std::atomic<bool> _cancelled{false};
};

#endif // CURLFETCHER_H
