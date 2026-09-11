/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Raspberry Pi Ltd
 *
 * See oslistparser.h. Moved verbatim out of oslistmodel.cpp's anonymous
 * namespace so it can be tested without a QML engine.
 */

#include "oslistparser.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QLocale>
#include <QRandomGenerator>
#include <QUrl>
#include <QDebug>

#include <functional>

namespace oslist {


// Valid init_format values according to schema
const QStringList VALID_INIT_FORMATS = {
    QStringLiteral(""),
    QStringLiteral("systemd"),
    QStringLiteral("cloudinit"),
    QStringLiteral("cloudinit-rpi"),
    QStringLiteral("rpi-preseed"),
    QStringLiteral("none")
};

// Validate init_format value and return true if valid
bool isValidInitFormat(const QString &initFormat) {
    return VALID_INIT_FORMATS.contains(initFormat);
}

// Recursively filter OS entries with invalid init_format values
QJsonArray filterInvalidInitFormats(const QJsonArray &list) {
    QJsonArray filtered;
    
    for (const auto &value : list) {
        QJsonObject entry = value.toObject();
        QString initFormat = entry["init_format"].toString();
        
        // Validate init_format if present (empty string is valid, means no customization)
        if (!isValidInitFormat(initFormat)) {
            QString name = entry["name"].toString();
            qWarning() << "OSListModel: Pruning OS entry with invalid init_format '" 
                       << initFormat << "':" << name
                       << "(valid values: '', 'systemd', 'cloudinit', 'cloudinit-rpi', 'rpi-preseed', 'none')";
            continue;
        }
        
        // Check if this entry has subitems and process them recursively
        if (entry.contains(QLatin1String("subitems"))) {
            QJsonArray subitems = entry["subitems"].toArray();
            QJsonArray filteredSubitems = filterInvalidInitFormats(subitems);
            
            // Only include parent entry if it has valid subitems
            if (!filteredSubitems.isEmpty()) {
                entry["subitems"] = filteredSubitems;
                filtered.append(entry);
            } else {
                // Parent has no valid subitems, skip it
                QString name = entry["name"].toString();
                qWarning() << "OSListModel: Pruning OS entry with no valid subitems:" << name;
            }
        } else {
            // Leaf entry with valid init_format
            filtered.append(entry);
        }
    }
    
    return filtered;
}

QJsonArray getListForLocale(const QJsonObject &root, const QString &systemLocale) {
    // "os_list_<locale>" has priority

    QString localeName = systemLocale;
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

QJsonArray parseOSJson(const QJsonObject &root) {
    QJsonArray list = getListForLocale(root);
    if (list.isEmpty()) {
        qWarning() << Q_FUNC_INFO << "Expected to find os_list key" << root.keys();
        return {};
    }

    // Filter out entries with invalid init_format values
    list = filterInvalidInitFormats(list);

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

// Sanitize icon source: allow known-good forms and drop malformed URLs to avoid runtime fetch errors
QString sanitizeIconSource(const QString &raw)
{
    if (raw.isEmpty()) return QString();

    // Common local relative path used by repository JSON
    if (raw.startsWith("icons/")) {
        return QStringLiteral("../") + raw;
    }

    // Allow qrc resources
    if (raw.startsWith("qrc:/") || raw.startsWith("qrc://")) {
        return raw;
    }

    // For explicit URLs, validate scheme and host as appropriate
    const QUrl url(raw);
    if (url.isValid() && !url.scheme().isEmpty()) {
        const QString scheme = url.scheme().toLower();

        if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
            if (!url.host().isEmpty()) {
                return raw; // looks well-formed; allow
            } else {
                qWarning() << "OSListModel: dropping icon with missing host:" << raw;
                return QString();
            }
        } else if (scheme == QLatin1String("file")) {
            // Allowed without touching the filesystem: exists()/isFile() can
            // be slow on iCloud-synced directories and network volumes, and
            // this runs for every icon as the model is populated. QML's Image
            // handles a missing file gracefully.
            if (url.host().isEmpty()) {
                return raw;
            }
            qWarning() << "OSListModel: dropping file URL icon naming a host:" << raw;
            return QString();
        } else {
            // Unknown scheme; pass through (QML may support it) but log once
            qWarning() << "OSListModel: icon uses unrecognized scheme, passing through:" << raw;
            return raw;
        }
    }

    // No scheme: treat as relative path; allow as-is (QML will resolve relative to QML file)
    return raw;
}
QJsonArray getListForLocale(const QJsonObject &root)
{
    return getListForLocale(root, QLocale::system().name());
}

} // namespace oslist
