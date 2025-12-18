/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#ifndef ICONIMAGEPROVIDER_H
#define ICONIMAGEPROVIDER_H

#include <QQuickImageProvider>
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

public slots:
    /**
     * Called from IconMultiFetcher when fetch completes.
     * Only receives the cache key - looks up data directly from cache to avoid copies.
     */
    void onFetchComplete(const QString &cacheKey, const QString &error);

private:
    QString _urlKey;  // Cache key for looking up data
    QImage _image;
    QString _errorString;
    std::atomic<bool> _cancelled{false};
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
