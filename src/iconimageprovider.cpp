/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020-2025 Raspberry Pi Ltd
 */

#include "iconimageprovider.h"
#include "iconmultifetcher.h"

#include <QQuickTextureFactory>
#include <QDebug>

// ----------------------------------------------------------------------------
// IconImageResponse
// ----------------------------------------------------------------------------

IconImageResponse::IconImageResponse(const QUrl &url)
    : _urlKey(url.toString())  // Pre-compute cache key
{
    // Queue fetch with the multi-fetcher (efficient for many concurrent icons)
    IconMultiFetcher::instance().queueFetch(this, url);
}

void IconImageResponse::cancel()
{
    _cancelled.store(true, std::memory_order_relaxed);
    IconMultiFetcher::instance().cancelFetch(this);
}

void IconImageResponse::onFetchComplete(const QString &cacheKey, const QString &error)
{
    if (!error.isEmpty()) {
        _errorString = error;
    } else if (cacheKey.isEmpty()) {
        _errorString = QStringLiteral("Empty response");
    } else {
        // Look up data directly from cache - no copy through signal system
        QByteArray data = IconMultiFetcher::instance().getCachedData(cacheKey);
        if (data.isEmpty()) {
            _errorString = QStringLiteral("Cache miss");
        } else {
            if (!_image.loadFromData(data)) {
                _errorString = QStringLiteral("Failed to decode image");
            }
        }
    }
    
    emit finished();
}

QQuickTextureFactory *IconImageResponse::textureFactory() const
{
    if (_image.isNull()) {
        return nullptr;
    }
    return QQuickTextureFactory::textureFactoryForImage(_image);
}

// ----------------------------------------------------------------------------
// IconImageProvider
// ----------------------------------------------------------------------------

IconImageProvider::IconImageProvider()
    : QQuickAsyncImageProvider()
{
}

IconImageProvider::~IconImageProvider() = default;

QQuickImageResponse *IconImageProvider::requestImageResponse(const QString &id, const QSize &)
{
    QUrl url(id);
    return new IconImageResponse(url);
}
