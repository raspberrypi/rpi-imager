/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifndef OSLISTMODEL_H
#define OSLISTMODEL_H

#include <QAbstractItemModel>
#include <QQmlEngine>

class ImageWriter;

/*
  Implements a model for OSPopup.qml

  The model is flat since it moved from QML to C++, the current GUI is expecting a list model
  not a tree model. The next step for improvement would be turning this into a tree model instead of storing
  JSON in 'subitemsJson', + a QSortFilterProxyModel so we can change root index easily.
*/
class OSListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
public:

    enum OSListRole {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        DevicesRole,
        ExtractSha256Role,
        ExtractSizeRole,
        IconRole,
        ImageDownloadSizeRole,
        InitFormatRole,
        ReleaseDataRole,
        UrlRole,
        RandomRole,
        SubItemsJsonRole,
        TooltipRole,
        WebsiteRole,
    };

    struct OS {
        QString name;
        QString description;
        QStringList devices; // not used by QML but present in JSON
        QString icon;
        QString initFormat;
        QString releaseDate;
        QString url;
        QString subitemsJson;
        QString subitemsUrl;
        QString tooltip;
        QString website;
        QString extractSha256;

        quint64 imageDownloadSize = 0;
        quint64 extractSize = 0;

        bool random = false;
    };

    explicit OSListModel(ImageWriter &);

    Q_INVOKABLE bool reload();

    // Adds "(Recommended)" to the description of the first OS
    Q_INVOKABLE void markFirstAsRecommended();

protected:
    int rowCount(const QModelIndex &) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QVector<OS> _osList;
    ImageWriter &_imageWriter;
};

#endif
