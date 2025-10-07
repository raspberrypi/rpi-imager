/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifndef HWLISTMODEL_H
#define HWLISTMODEL_H

#include <QAbstractItemModel>
#ifndef CLI_ONLY_BUILD
#include <QQmlEngine>
#endif
#include <QJsonArray>

class ImageWriter;

class HWListModel : public QAbstractListModel
{
    Q_OBJECT
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
#endif
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentNameChanged)
    Q_PROPERTY(QString currentArchitecture READ currentArchitecture NOTIFY currentArchitectureChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
public:

    enum HWListRole {
        NameRole = Qt::UserRole + 1,
        TagsRole,
        CapabilitiesRole,
        IconRole,
        DescriptionRole,
        MatchingTypeRole,
        ArchitectureRole
    };

    struct HardwareDevice {
        QString name;
        QJsonArray tags;
        QJsonArray capabilities;
        QString icon;
        QString description;
        QString matchingType;
        QString architecture; // Preferred architecture (armel, armhf, armv8)

        bool isInclusive() const {
            return matchingType == QLatin1String("inclusive");
        }
    };

    explicit HWListModel(ImageWriter &);

    Q_INVOKABLE bool reload();

    // Returns the name associated with the current index
    QString currentName() const;

    // Returns the preferred architecture of the current device
    QString currentArchitecture() const;

    int currentIndex() const;
    void setCurrentIndex(int index);

Q_SIGNALS:
    void currentNameChanged();
    void currentArchitectureChanged();
    void currentIndexChanged();

protected:
    int rowCount(const QModelIndex &) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QVector<HardwareDevice> _hwDevices;
    ImageWriter &_imageWriter;
    int _currentIndex = -1;
    QString _lastSelectedDeviceName;  // Track actual device to detect changes
};

#endif
