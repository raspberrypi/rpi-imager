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
#include <algorithm>
#include <QRegularExpression>

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

    // Apply architecture-based sorting to subitems in a JSON array
    void applyArchitectureSorting(QJsonArray &list, const QString &preferredArchitecture) {
        if (preferredArchitecture.isEmpty()) {
            return; // No preferred architecture, no sorting needed
        }

        // First, sort the top-level array itself
        QJsonArray sortedList;
        QJsonArray otherItems;
        
        // Collect items that match preferred architecture first
        for (int i = 0; i < list.size(); i++) {
            QJsonObject item = list[i].toObject();
            QString itemArch = item["architecture"].toString();
            
            if (itemArch == preferredArchitecture) {
                sortedList.append(list[i]);
            } else {
                otherItems.append(list[i]);
            }
        }
        
        // Append all non-matching items
        for (int i = 0; i < otherItems.size(); i++) {
            sortedList.append(otherItems[i]);
        }
        
        // Replace the original list with the sorted one
        list = sortedList;

        // Then recursively process subitems within each entry
        for (int i = 0; i < list.size(); i++) {
            QJsonObject entry = list[i].toObject();
            
            if (entry.contains(QLatin1String("subitems"))) {
                QJsonArray subitems = entry["subitems"].toArray();
                
                // Recursively apply to nested subitems
                applyArchitectureSorting(subitems, preferredArchitecture);
                
                // Manual stable sort: move items matching preferred architecture to the front
                // We'll build a new array with preferred items first, then others
                QJsonArray sortedSubitems;
                QJsonArray otherSubitems;
                
                // First pass: collect items that match preferred architecture
                for (int j = 0; j < subitems.size(); j++) {
                    QJsonObject subitem = subitems[j].toObject();
                    QString itemArch = subitem["architecture"].toString();
                    
                    if (itemArch == preferredArchitecture) {
                        sortedSubitems.append(subitems[j]);
                    } else {
                        otherSubitems.append(subitems[j]);
                    }
                }
                
                // Second pass: append all non-matching items
                for (int j = 0; j < otherSubitems.size(); j++) {
                    sortedSubitems.append(otherSubitems[j]);
                }
                
                entry["subitems"] = sortedSubitems;
                list[i] = entry;
            }
        }
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

    // Get the preferred architecture from the currently selected device
    QString preferredArchitecture = _imageWriter.getHWList()->currentArchitecture();
    
    // Apply architecture-based sorting if device has a preference
    applyArchitectureSorting(list, preferredArchitecture);

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
        os.architecture = obj["architecture"].toString();

        _osList.append(os);
    }

    // Mark the first OS as recommended after architecture sorting
    markFirstAsRecommended();

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
        { WebsiteRole, "website" },
        { ArchitectureRole, "architecture" }
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
        case ArchitectureRole:
            return os.architecture;
    }

    return {};
}

void OSListModel::markFirstAsRecommended() {
    const QString recommendedString = QStringLiteral(" (%1)").arg(tr("Recommended"));

    // First pass: Remove any existing "(Recommended)" labels from all items
    for (int i = 0; i < _osList.size(); i++) {
        OS &os = _osList[i];
        // Remove any variant of the recommended string (handles different locales)
        if (os.description.contains(QRegularExpression(R"( \([^)]*\bRecommended\b[^)]*\))"))) {
            os.description.remove(QRegularExpression(R"( \([^)]*\bRecommended\b[^)]*\))"));
        }
        // Also remove the localized version if it exists
        if (os.description.contains(recommendedString)) {
            os.description.remove(recommendedString);
        }
    }

    // Second pass: Add the localized "(Recommended)" to the first item if appropriate
    if (!_osList.isEmpty()) {
        OS &candidate = _osList[0];

        if (!candidate.description.isEmpty() &&
            candidate.subitemsJson.isEmpty())
        {
            candidate.description += recommendedString;
        }
    }
}
