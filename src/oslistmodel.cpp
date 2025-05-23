/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "oslistmodel.h"
#include "imagewriter.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <QJsonValue>
#include <QLocale>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <qjsonarray.h>

namespace {

    QJsonArray getListForLocale(QJsonObject root) {
        // "os_list_<locale>" has priority

        QString localeName = QLocale::system().name();
        QString candidateKey = QStringLiteral("os_list_%1").arg(localeName);

        QJsonArray list;
        if (root.contains(candidateKey)) {
            list = root[candidateKey].toArray();
        } else if (localeName.contains(QLatin1Char('_'))) {
            localeName = localeName.section('_', 0, 0);
            candidateKey = QStringLiteral("os_list_%1").arg(localeName);
            if (root.contains(candidateKey)) {
                list = root[candidateKey].toArray();
            }
        }

        // fallback to "os_list"
        if (list.isEmpty() && root.contains(QLatin1String("os_list"))) {
            list = root["os_list"].toArray();
        }

        return list;
    }

    QJsonArray parseOSJson(QJsonObject root) {
        QJsonArray list = getListForLocale(root);
        if (list.isEmpty()) {
            qWarning() << Q_FUNC_INFO << "Expected to find os_list key" << root.keys();
            return {};
        }

        // Apply random shuffling to arrays containing 'random' flag
        std::function<void(QJsonArray&)> shuffleIfRandom = [&](QJsonArray &lst) {
            for (int i = 0; i < lst.size(); i++) {
                QJsonObject entry = lst[i].toObject();
                
                if (entry.contains(QLatin1String("subitems"))) {
                    QJsonArray subitems = entry["subitems"].toArray();
                    shuffleIfRandom(subitems);
                    
                    // Shuffle if random flag is set
                    if (entry.contains(QLatin1String("random")) && entry["random"].toBool()) {
                        // Fisher-Yates shuffle - properly handle QJsonArray
                        for (int j = subitems.size() - 1; j > 0; j--) {
                            int k = QRandomGenerator::global()->bounded(j + 1);
                            if (j != k) {
                                // Properly swap QJsonArray elements by storing values, not references
                                QJsonValue tempValue = subitems[j];
                                QJsonValue kValue = subitems[k];
                                subitems[j] = kValue;
                                subitems[k] = tempValue;
                            }
                        }
                    }
                    entry["subitems"] = subitems;
                    lst[i] = entry;
                }
            }
        };

        shuffleIfRandom(list);

        // Flatten, since GUI doesn't support a tree model
        for (int i = 0; i < list.size(); i++) {
            QJsonObject entry = list[i].toObject();
            if (entry.contains("subitems")) {
                QJsonDocument subitemsDoc(entry["subitems"].toArray());
                entry["subitems_json"] = QString::fromUtf8(subitemsDoc.toJson());
                entry.remove("subitems");
                list[i] = entry;
            }
        }

        return list;
    }
}

OSListModel::OSListModel(ImageWriter &imageWriter)
    : QAbstractListModel(&imageWriter), _imageWriter(imageWriter) {}

bool OSListModel::reload()
{
    QJsonDocument doc = _imageWriter.getFilteredOSlistDocument();
    QJsonObject root = doc.object();

    QJsonArray list = parseOSJson(root);
    if (list.isEmpty()) {
        return false;
    }

    beginResetModel();
    _osList.clear();
    _osList.reserve(list.count());

    for (const auto value : list) {
        const QJsonObject obj = value.toObject();
        OS os;

        os.name = obj["name"].toString();
        os.description = obj["description"].toString();

        QJsonArray devicesArray = obj["devices"].toArray();
        os.devices.reserve(devicesArray.size());
        for (const auto &device : devicesArray) {
            os.devices.append(device.toString());
        }

        os.extractSize = obj["extract_size"].toDouble();
        os.imageDownloadSize = obj["image_download_size"].toDouble();

        os.random = obj["random"].toBool();

        os.extractSha256 = obj["extract_sha256"].toString();
        os.icon = obj["icon"].toString();
        os.initFormat = obj["init_format"].toString();
        os.releaseDate = obj["release_date"].toString();
        os.url = obj["url"].toString();
        os.subitemsJson = obj["subitems_json"].toString();
        os.tooltip = obj["tooltip"].toString();
        os.website = obj["website"].toString();

        _osList.append(os);
    }

    endResetModel();

    return true;
}

int OSListModel::rowCount(const QModelIndex &) const
{
    return _osList.size();
}

QHash<int, QByteArray> OSListModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { DescriptionRole, "description" },
        { DevicesRole, "devices" },
        { ExtractSha256Role, "extract_sha256" },
        { ExtractSizeRole, "extract_size" },
        { IconRole, "icon" },
        { ImageDownloadSizeRole, "image_download_size" },
        { InitFormatRole, "init_format" },
        { ReleaseDataRole, "release_date" },
        { UrlRole, "url" },{ RandomRole, "random" },
        { SubItemsJsonRole, "subitems_json" },
        { TooltipRole, "tooltip" },
        { WebsiteRole, "website" }
    };
}

QVariant OSListModel::data(const QModelIndex &index, int role) const {
    const int row = index.row();
    if (row < 0 || row >= _osList.size())
        return {};

    const OS &os = _osList[row];

    switch (OSListRole(role)) {
        case NameRole:
            return os.name;
        case DescriptionRole:
            return os.description;
        case DevicesRole:
            return os.devices;
        case ExtractSha256Role:
            return os.extractSha256;
        case ExtractSizeRole:
            return os.extractSize;
        case IconRole:
            return os.icon;
        case ImageDownloadSizeRole:
            return os.imageDownloadSize;
        case InitFormatRole:
            return os.initFormat;
        case ReleaseDataRole:
            return os.releaseDate;
        case UrlRole:
            return os.url;
        case RandomRole:
            return os.random;
        case SubItemsJsonRole:
            return os.subitemsJson;
        case TooltipRole:
            return os.tooltip;
        case WebsiteRole:
            return os.website;
    }

    return {};
}

void OSListModel::markFirstAsRecommended() {
    if (!_osList.isEmpty()) {
        OS &candidate = _osList[0];

        const QString recommendedString = QStringLiteral(" (Recommended)");
        const QString recommendedStringLocalized = QStringLiteral(" (%1)").arg(tr("Recommended"));

        if (!candidate.description.isEmpty() &&
            candidate.subitemsJson.isEmpty() &&
            !candidate.description.contains(recommendedString) &&
            !candidate.description.contains(recommendedStringLocalized))
        {
            candidate.description += recommendedStringLocalized;
        }
    }
}
