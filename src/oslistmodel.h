/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#ifndef OSLISTMODEL_H
#define OSLISTMODEL_H

#include <QAbstractItemModel>
#ifndef CLI_ONLY_BUILD
#include <QQmlEngine>
#endif

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
#ifndef CLI_ONLY_BUILD
    QML_ELEMENT
    QML_UNCREATABLE("Created by C++")
#endif
public:

    enum OSListRole {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        DevicesRole,
        CapabilitiesRole,
        ExtractSha256Role,
        BmapUrlRole,
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
        ArchitectureRole,
        PiConnectRole,
    };

    struct OS {
        QString name;
        QString description;
        QStringList devices; // not used by QML but present in JSON
        QStringList capabilities;
        QString icon;
        QString initFormat;
        QString releaseDate;
        QString url;
        QString subitemsJson;
        QString subitemsUrl;
        QString tooltip;
        QString website;
        QString extractSha256;
        QString bmapUrl;       // Optional bmap file URL for fastboot DONT_CARE optimisation
        QString architecture; // Architecture this OS expects (armel, armhf, armv8)

        quint64 imageDownloadSize = 0;
        quint64 extractSize = 0;

        bool random = false;
        bool enableRPiConnect = false;
    };

    explicit OSListModel(ImageWriter &);

    Q_INVOKABLE bool reload();
    // Emit dataChanged for all rows without resetting the model
    Q_INVOKABLE void softRefresh();

    // Adds "(Recommended)" to the description of the first OS
    Q_INVOKABLE void markFirstAsRecommended();

    // The same, on rows that are not in the model yet.
    static void markRecommendedIn(QVector<OS> &rows);

    // Replace the rows with `next`, reporting the difference rather than a
    // reset.
    //
    // A reset destroys every delegate in the view, which costs more than the
    // rebuilding: it drops the scroll position and the highlight, and it
    // loses whatever click was in progress. Qt delivers a click only when
    // the press and the release reach the same item, so a list rebuilt
    // between the two swallows it -- the user presses, the list refills,
    // nothing is selected, and they click a second time. That is reachable:
    // until the OS list arrives the model holds only "Erase" and "Use
    // custom", and the arrival of the real list is what rebuilds it.
    //
    // Rows are matched by url and name, so the two that were already there
    // keep their delegates and simply move down as the real entries are
    // inserted above them.
    void applyRows(QVector<OS> &&next);

signals:
    void eventOsListParse(quint32 durationMs, bool success);

public slots:

protected:
    int rowCount(const QModelIndex &) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    QVector<OS> _osList;
    ImageWriter &_imageWriter;
};

#endif
