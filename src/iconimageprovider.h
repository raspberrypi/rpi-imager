/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#ifndef ICONIMAGEPROVIDER_H
#define ICONIMAGEPROVIDER_H

#include <QQuickImageProvider>
#include <QHash>
#include <QImage>
#include <QUrl>
#include <atomic>

/**
 * Async image response that fetches icons via IconMultiFetcher.
 * 
 * Uses curl_multi under the hood for efficient concurrent downloads with
 * HTTP/2 multiplexing and connection pooling. Supports cancellation.
 * 
 * Data is looked up directly from the shared cache to avoid copies through
 * the Qt signal system.
 */
class IconImageResponse final : public QQuickImageResponse
{
    Q_OBJECT
public:
    explicit IconImageResponse(const QUrl &url);

    /*
     * Takes this response's id out of the registry, on the thread that owns
     * both. The fetcher never held a pointer to it, so there is nothing for
     * this to race with and nothing to lock.
     */
    ~IconImageResponse() override;
    
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override { return _errorString; }
    
    /**
     * Cancel the fetch. Called by Qt when the image is no longer needed.
     */
    void cancel() override;
    
    /**
     * Check if cancellation has been requested.
     */
    bool isCancelled() const { return _cancelled.load(std::memory_order_relaxed); }

    /**
     * What the fetcher knows this request by. It is given an id rather than
     * a pointer, so that a response going away while a fetch is in flight
     * cannot be dereferenced on the fetcher's thread.
     */
    quint64 requestId() const { return _id; }

public slots:
    /**
     * Called from IconMultiFetcher when fetch completes.
     * Only receives the cache key - looks up data directly from cache to avoid copies.
     */
    void onFetchComplete(const QString &cacheKey, const QString &error);

private:
    quint64 _id;      // What the fetcher knows this request by
    QString _urlKey;  // Cache key for looking up data
    QImage _image;
    QString _errorString;
    std::atomic<bool> _cancelled{false};
};

/**
 * Where a finished fetch is turned back into a response object.
 *
 * The fetcher runs on its own thread and reports by id, never by pointer:
 * it cannot be given a QObject whose lifetime another thread controls. This
 * holds the ids that are still live, is only ever touched on the thread that
 * creates and destroys responses, and drops a result whose id has gone.
 */
class IconResponseRegistry final : public QObject
{
    Q_OBJECT
public:
    static IconResponseRegistry &instance();

    quint64 add(IconImageResponse *response);
    void remove(quint64 id);

public slots:
    void deliver(quint64 id, const QString &urlKey, const QString &error);

private:
    IconResponseRegistry();
    QHash<quint64, IconImageResponse *> _live;
    quint64 _next = 1;
};

/**
 * QML image provider for remote icons.
 * 
 * Usage in QML: Image { source: "image://icons/https://example.com/icon.png" }
 * 
 * Uses curl_multi (via IconMultiFetcher) for efficient concurrent fetching
 * with HTTP/2 multiplexing, connection pooling, and respecting shared
 * CurlNetworkConfig settings (IPv4-only mode, proxy, etc.).
 */
class IconImageProvider final : public QQuickAsyncImageProvider
{
public:
    IconImageProvider();
    ~IconImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

#endif // ICONIMAGEPROVIDER_H
