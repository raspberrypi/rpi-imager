#include "iconimageprovider.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QQuickTextureFactory>
#include <QUrl>

IconImageResponse::IconImageResponse(const QUrl &url)
    : _nam(new QNetworkAccessManager(this))
{
    QNetworkRequest req(url);
    // Avoid HTTP/2 issues for small icon fetches; use HTTP/1.1
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(3);

    _reply = _nam->get(req);
    QObject::connect(_reply, &QNetworkReply::finished, this, &IconImageResponse::onFinished);
}

void IconImageResponse::onFinished()
{
    if (!_reply) {
        _errorString = QStringLiteral("Network reply missing");
        emit finished();
        return;
    }
    if (_reply->error() != QNetworkReply::NoError) {
        _errorString = _reply->errorString();
        _reply->deleteLater();
        emit finished();
        return;
    }

    const QByteArray data = _reply->readAll();
    _reply->deleteLater();

    QImage img;
    img.loadFromData(data);
    if (!img.isNull()) {
        _image = img;
    } else {
        _errorString = QStringLiteral("Failed to decode image");
    }
    emit finished();
}

QQuickTextureFactory *IconImageResponse::textureFactory() const
{
    if (_image.isNull()) return nullptr;
    return QQuickTextureFactory::textureFactoryForImage(_image);
}

IconImageProvider::IconImageProvider()
    : QQuickAsyncImageProvider()
{}

IconImageProvider::~IconImageProvider() = default;

QQuickImageResponse *IconImageProvider::requestImageResponse(const QString &id, const QSize &)
{
    QUrl url(id);
    return new IconImageResponse(url);
}


