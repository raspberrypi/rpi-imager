#ifndef HWLISTMODEL_H
#define HWLISTMODEL_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include <QAbstractItemModel>

#include <QQmlEngine>
#include <QJsonArray>

class ImageWriter;

class HWListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentNameChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
public:

    enum HWListRole {
        NameRole = Qt::UserRole + 1,
        TagsRole,
        IconRole,
        DescriptionRole,
        MatchingTypeRole
    };

    struct HardwareDevice {
        QString name;
        QJsonArray tags;
        QString icon;
        QString description;
        QString matchingType;

        bool isInclusive() const {
            return matchingType == QLatin1String("inclusive");
        }
    };

    explicit HWListModel(ImageWriter &);

    Q_INVOKABLE void setSelectedIndex(int index);
    Q_INVOKABLE bool reload();

    QString currentName() const;
    int currentIndex() const;
Q_SIGNALS:
    void currentNameChanged();
    void currentIndexChanged();

protected:
    int rowCount(const QModelIndex &) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    void setCurrentIndex(int index);

    QVector<HardwareDevice> _hwDevices;
    ImageWriter &_imageWriter;
    int _currentIndex = -1;
};

#endif
