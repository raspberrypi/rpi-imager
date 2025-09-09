#ifndef ICONIMAGEPROVIDER_H
#define ICONIMAGEPROVIDER_H

#include <QQuickImageProvider>
#include <QNetworkAccessManager>
#include <QImage>
#include <QPointer>

class IconImageResponse final : public QQuickImageResponse
{
    Q_OBJECT
public:
    explicit IconImageResponse(const QUrl &url);
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override { return _errorString; }

private slots:
    void onFinished();

private:
    QPointer<QNetworkAccessManager> _nam;
    QPointer<QNetworkReply> _reply;
    QImage _image;
    QString _errorString;
};

class IconImageProvider final : public QQuickAsyncImageProvider
{
public:
    IconImageProvider();
    ~IconImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

#endif // ICONIMAGEPROVIDER_H


