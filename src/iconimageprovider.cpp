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
    : _id(IconResponseRegistry::instance().add(this))
    , _urlKey(url.toString())  // Pre-compute cache key
{
    // Queue fetch with the multi-fetcher (efficient for many concurrent icons)
    IconMultiFetcher::instance().queueFetch(_id, url);
}

IconImageResponse::~IconImageResponse()
{
    IconResponseRegistry::instance().remove(_id);
}

void IconImageResponse::cancel()
{
    _cancelled.store(true, std::memory_order_relaxed);
    IconMultiFetcher::instance().cancelFetch(_id);
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
// IconResponseRegistry
// ----------------------------------------------------------------------------

IconResponseRegistry &IconResponseRegistry::instance()
{
    static IconResponseRegistry registry;
    return registry;
}

IconResponseRegistry::IconResponseRegistry()
{
    /* Queued, and it has to be: the fetcher emits from its own thread while
       this object lives on the thread that owns the responses. Delivery
       therefore happens where the lookup and the destructor happen, which is
       what makes the lookup safe without a lock. */
    connect(&IconMultiFetcher::instance(), &IconMultiFetcher::fetchFinished,
            this, &IconResponseRegistry::deliver, Qt::QueuedConnection);
}

quint64 IconResponseRegistry::add(IconImageResponse *response)
{
    const quint64 id = _next++;
    _live.insert(id, response);
    return id;
}

void IconResponseRegistry::remove(quint64 id)
{
    _live.remove(id);
}

void IconResponseRegistry::deliver(quint64 id, const QString &urlKey,
                                   const QString &error)
{
    /* An id the registry does not hold is a response that has already gone --
       a delegate scrolled away while its icon was in flight, which is
       ordinary. Nothing to do and nothing to warn about. */
    const auto it = _live.constFind(id);
    if (it == _live.cend())
        return;
    it.value()->onFetchComplete(urlKey, error);
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
