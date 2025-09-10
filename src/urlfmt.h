#pragma once
#include <QObject>
#include <QUrl>
#include <qqmlregistration.h>

class UrlFmt : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    Q_INVOKABLE QString display(const QUrl &u) const;
};
