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
#include <QRandomGenerator>
#include <qjsonarray.h>
#include <algorithm>
#include <QRegularExpression>
#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QElapsedTimer>

#include "model_row_diff.h"
#include "oslistparser.h"


OSListModel::OSListModel(ImageWriter &imageWriter)
    : QAbstractListModel(&imageWriter), _imageWriter(imageWriter) {}

bool OSListModel::reload()
{
    QElapsedTimer parseTimer;
    parseTimer.start();
    
    QJsonDocument doc = _imageWriter.getFilteredOSlistDocument();
    QJsonObject root = doc.object();

    QJsonArray list = oslist::parseOSJson(root);
    if (list.isEmpty()) {
        emit eventOsListParse(static_cast<quint32>(parseTimer.elapsed()), false);
        return false;
    }

    // Get the preferred architecture from the currently selected device
    QString preferredArchitecture = _imageWriter.getHWList()->currentArchitecture();
    
    // Apply architecture-based sorting if device has a preference
    oslist::applyArchitectureSorting(list, preferredArchitecture);

    QVector<OS> next;
    next.reserve(list.count());
    int unusableSizes = 0;

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

        QJsonArray capsArray = obj["capabilities"].toArray();
        os.capabilities.reserve(capsArray.size());
        for (const auto &cap : capsArray) {
            os.capabilities.append(cap.toString());
        }

        os.extractSize = oslist::byteCountFromJson(obj["extract_size"]);
        os.imageDownloadSize = oslist::byteCountFromJson(obj["image_download_size"]);

        // A size that was there and could not be used is worth saying once.
        // Nought is also what an absent field gives, so the two are told
        // apart here rather than in the helper: without this the only sign
        // is a row that declines to quote a figure, which reads like an
        // ordinary entry.
        if ((obj.contains(QLatin1String("extract_size")) && os.extractSize == 0)
            || (obj.contains(QLatin1String("image_download_size"))
                && os.imageDownloadSize == 0)) {
            ++unusableSizes;
        }

        os.random = obj["random"].toBool();

        os.extractSha256 = obj["extract_sha256"].toString();
        os.bmapUrl = obj["bmap_url"].toString();
        // Icon source: rewrite to image provider to avoid network head-of-line blocking
        {
            const QString routed = oslist::iconSourceFor(obj["icon"].toString());
            if (!routed.isEmpty()) {
                os.icon = routed;
            }
        }
        os.initFormat = obj["init_format"].toString();
        os.releaseDate = obj["release_date"].toString();
        os.url = obj["url"].toString();
        os.subitemsJson = obj["subitems_json"].toString();
        os.tooltip = obj["tooltip"].toString();
        os.website = obj["website"].toString();
        os.architecture = obj["architecture"].toString();
        os.enableRPiConnect = obj.value("enable_rpi_connect").toBool(false);

        next.append(os);
    }

    if (unusableSizes > 0) {
        qWarning() << "OSListModel:" << unusableSizes
                   << "entries declared a size that could not be used"
                   << "(negative, past 2^64, or not a number) -- those rows"
                   << "show no download figure";
    }

    // Mark the first OS as recommended after architecture sorting. Done on
    // the new rows before they are compared, so the label is part of what
    // the comparison sees rather than a change applied afterwards.
    markRecommendedIn(next);

    applyRows(std::move(next));

    emit eventOsListParse(static_cast<quint32>(parseTimer.elapsed()), true);

    return true;
}

namespace {

// What makes two rows the same row rather than the same contents. The unit
// separator cannot appear in either field, so it cannot merge two keys that
// differ.
QString rowKey(const OSListModel::OS &os)
{
    return os.url + QChar(0x1F) + os.name;
}

bool sameContents(const OSListModel::OS &a, const OSListModel::OS &b)
{
    return a.name == b.name
        && a.description == b.description
        && a.devices == b.devices
        && a.capabilities == b.capabilities
        && a.icon == b.icon
        && a.initFormat == b.initFormat
        && a.releaseDate == b.releaseDate
        && a.url == b.url
        && a.subitemsJson == b.subitemsJson
        && a.subitemsUrl == b.subitemsUrl
        && a.tooltip == b.tooltip
        && a.website == b.website
        && a.extractSha256 == b.extractSha256
        && a.bmapUrl == b.bmapUrl
        && a.architecture == b.architecture
        && a.imageDownloadSize == b.imageDownloadSize
        && a.extractSize == b.extractSize
        && a.random == b.random
        && a.enableRPiConnect == b.enableRPiConnect;
}

} // namespace

void OSListModel::applyRows(QVector<OS> &&next)
{
    QStringList currentKeys;
    currentKeys.reserve(_osList.size());
    for (const OS &os : _osList)
        currentKeys << rowKey(os);

    QStringList nextKeys;
    nextKeys.reserve(next.size());
    for (const OS &os : next)
        nextKeys << rowKey(os);

    const rpi_model::RowDiff diff = rpi_model::planRowDiff(currentKeys, nextKeys);

    if (diff.removed > 0) {
        beginRemoveRows(QModelIndex(), diff.at, diff.at + diff.removed - 1);
        _osList.remove(diff.at, diff.removed);
        endRemoveRows();
    }

    if (diff.inserted > 0) {
        beginInsertRows(QModelIndex(), diff.at, diff.at + diff.inserted - 1);
        for (int i = 0; i < diff.inserted; ++i)
            _osList.insert(diff.at + i, next.at(diff.at + i));
        endInsertRows();
    }

    // The rows that stayed may still have new contents -- a cache status, a
    // size, the recommended label moving to a different entry.
    for (int i = 0; i < _osList.size(); ++i) {
        if (i >= diff.at && i < diff.at + diff.inserted)
            continue;  // just inserted, already current
        if (!sameContents(_osList.at(i), next.at(i))) {
            _osList[i] = next.at(i);
            emit dataChanged(index(i), index(i));
        }
    }
}

QVariantMap OSListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= _osList.size())
        return out;

    const QModelIndex idx = index(row, 0);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return out;
}

void OSListModel::softRefresh()
{
    if (_osList.isEmpty()) return;
    const QModelIndex first = index(0);
    const QModelIndex last = index(_osList.size() - 1);
    emit dataChanged(first, last);
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
        { CapabilitiesRole, "capabilities" },
        { ExtractSha256Role, "extract_sha256" },
        { BmapUrlRole, "bmap_url" },
        { ExtractSizeRole, "extract_size" },
        { IconRole, "icon" },
        { ImageDownloadSizeRole, "image_download_size" },
        { InitFormatRole, "init_format" },
        { ReleaseDataRole, "release_date" },
        { UrlRole, "url" },{ RandomRole, "random" },
        { SubItemsJsonRole, "subitems_json" },
        { TooltipRole, "tooltip" },
        { WebsiteRole, "website" },
        { ArchitectureRole, "architecture" },
        { PiConnectRole, "enable_rpi_connect" }
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
        case CapabilitiesRole:
            return os.capabilities;
        case ExtractSha256Role:
            return os.extractSha256;
        case BmapUrlRole:
            return os.bmapUrl;
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
        case PiConnectRole:
            return os.enableRPiConnect;
    }

    return {};
}

void OSListModel::markFirstAsRecommended() {
    markRecommendedIn(_osList);
}

void OSListModel::markRecommendedIn(QVector<OS> &rows) {
    const QString recommendedString =
        QStringLiteral(" (%1)").arg(QCoreApplication::translate("OSListModel", "Recommended"));

    // First pass: Remove any existing "(Recommended)" labels from all items
    for (int i = 0; i < rows.size(); i++) {
        OS &os = rows[i];
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
    // Skip internal items (Erase, Use custom) - these are fallbacks when OS list download fails
    for (int i = 0; i < rows.size(); i++) {
        OS &candidate = rows[i];

        // Skip internal items (e.g., "internal://format", "internal://custom")
        if (candidate.url.startsWith(QLatin1String("internal://"))) {
            continue;
        }

        // Found a real OS entry - mark it as recommended if appropriate
        if (!candidate.description.isEmpty() &&
            candidate.subitemsJson.isEmpty())
        {
            candidate.description += recommendedString;
        }
        break;  // Only mark the first real OS
    }
}
