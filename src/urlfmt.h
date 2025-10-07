#pragma once
#include <QObject>
#include <QUrl>
#ifndef CLI_ONLY_BUILD
#include <qqmlregistration.h>
#endif

class UrlFmt : public QObject {
    Q_OBJECT
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_SINGLETON
#endif
public:
    Q_INVOKABLE QString display(const QUrl &u) const;
};
