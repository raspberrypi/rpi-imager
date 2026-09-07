/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

#include "model_row_diff.h"
#include "hwlistmodel.h"
#include "imagewriter.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <QJsonValue>

HWListModel::HWListModel(ImageWriter &imageWriter)
    : QAbstractListModel(&imageWriter), _imageWriter(imageWriter) {}

bool HWListModel::reload()
{
    QJsonDocument doc = _imageWriter.getFilteredOSlistDocument();
    QJsonObject root = doc.object();

    if (root.isEmpty() || !doc.isObject()) {
        qWarning() << Q_FUNC_INFO << "Invalid root";
        return false;
    }

    QJsonValue imager = root.value("imager");

    if (!imager.isObject()) {
        qWarning() << Q_FUNC_INFO << "missing imager";
        return false;
    }

    QJsonValue devices = imager.toObject().value("devices");

    if (!devices.isArray()) {
        // just means list hasn't been loaded yet
        return false;
    }

    QVector<HardwareDevice> next;

    const QJsonArray deviceArray = devices.toArray();
    _hwDevices.reserve(deviceArray.size());
    int indexOfDefault = -1;
    for (const QJsonValue &deviceValue: deviceArray) {
        QJsonObject deviceObj = deviceValue.toObject();

        HardwareDevice hwDevice = {
            deviceObj["name"].toString(),
            deviceObj["tags"].toArray(),
            deviceObj["capabilities"].toArray(),
            [&]() {
                QString iconPath = deviceObj["icon"].toString();
                // Adjust icon path for wizard directory structure
                if (iconPath.startsWith("icons/")) {
                    iconPath = "../" + iconPath;
                }
                // Route remote icons via image provider to avoid HTTP/2 errors
                if (iconPath.startsWith("http://") || iconPath.startsWith("https://")) {
                    iconPath = QStringLiteral("image://icons/") + iconPath;
                }
                return iconPath;
            }(),
            deviceObj["description"].toString(),
            deviceObj["matching_type"].toString(),
            deviceObj["architecture"].toString()
        };
        next.append(hwDevice);

        if (deviceObj["default"].isBool() && deviceObj["default"].toBool())
            indexOfDefault = next.size() - 1;
    }

    applyRows(std::move(next));

    setCurrentIndex(indexOfDefault);

    return true;
}

namespace {

// What makes two boards the same board. The name is what the chooser is
// keyed on everywhere else -- currentName() is how the selection is reported
// -- and two entries never share one.
QString rowKey(const HWListModel::HardwareDevice &device)
{
    return device.name;
}

bool sameContents(const HWListModel::HardwareDevice &a,
                  const HWListModel::HardwareDevice &b)
{
    return a.name == b.name
        && a.tags == b.tags
        && a.capabilities == b.capabilities
        && a.icon == b.icon
        && a.description == b.description
        && a.matchingType == b.matchingType
        && a.architecture == b.architecture;
}

} // namespace

void HWListModel::applyRows(QVector<HardwareDevice> &&next)
{
    QStringList currentKeys;
    currentKeys.reserve(_hwDevices.size());
    for (const HardwareDevice &device : _hwDevices)
        currentKeys << rowKey(device);

    QStringList nextKeys;
    nextKeys.reserve(next.size());
    for (const HardwareDevice &device : next)
        nextKeys << rowKey(device);

    const rpi_model::RowDiff diff = rpi_model::planRowDiff(currentKeys, nextKeys);

    if (diff.removed > 0) {
        beginRemoveRows(QModelIndex(), diff.at, diff.at + diff.removed - 1);
        _hwDevices.remove(diff.at, diff.removed);
        endRemoveRows();
    }

    if (diff.inserted > 0) {
        beginInsertRows(QModelIndex(), diff.at, diff.at + diff.inserted - 1);
        for (int i = 0; i < diff.inserted; ++i)
            _hwDevices.insert(diff.at + i, next.at(diff.at + i));
        endInsertRows();
    }

    for (int i = 0; i < _hwDevices.size(); ++i) {
        if (i >= diff.at && i < diff.at + diff.inserted)
            continue;
        if (!sameContents(_hwDevices.at(i), next.at(i))) {
            _hwDevices[i] = next.at(i);
            emit dataChanged(index(i), index(i));
        }
    }
}

int HWListModel::rowCount(const QModelIndex &) const
{
    return _hwDevices.size();
}

QHash<int, QByteArray> HWListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {TagsRole, "tags"},
        {CapabilitiesRole, "capabilities"},
        {IconRole, "icon"},
        {DescriptionRole, "description"},
        {MatchingTypeRole, "matching_type"},
        {ArchitectureRole, "architecture"},
        {IsUsbBootConnectedRole, "isUsbBootConnected"}
    };
}

QVariant HWListModel::data(const QModelIndex &index, int role) const {
    const int row = index.row();
    if (row < 0 || row >= _hwDevices.size())
        return {};

    const HardwareDevice &device = _hwDevices[row];

    switch (HWListRole(role)) {
    case NameRole:
        return device.name;
    case TagsRole:
        return device.tags;
    case CapabilitiesRole:
        return device.capabilities;
    case IconRole:
        return device.icon;
    case DescriptionRole:
        return device.description;
    case MatchingTypeRole:
        return device.matchingType;
    case ArchitectureRole:
        return device.architecture;
    case IsUsbBootConnectedRole:
        for (const auto &chip : _connectedRpibootChips) {
            if (tagsMatchChip(device.tags, chip))
                return true;
        }
        return false;
    }

    return {};
}

QString HWListModel::currentName() const {
    if (_currentIndex < 0 || _currentIndex >= _hwDevices.size())
        return tr("CHOOSE DEVICE");

    HardwareDevice device = _hwDevices[_currentIndex];
    return device.name;
}

QString HWListModel::currentArchitecture() const {
    if (_currentIndex < 0 || _currentIndex >= _hwDevices.size())
        return QString();

    HardwareDevice device = _hwDevices[_currentIndex];
    return device.architecture;
}

void HWListModel::setCurrentIndex(int index) {
    if (_currentIndex == index)
        return;

    // Allow -1 to clear the selection
    if (index < -1 || index >= _hwDevices.size()) {
        qWarning() << Q_FUNC_INFO << "Invalid index" << index;
        return;
    }

    // Handle clearing selection (index == -1)
    if (index == -1) {
        qDebug() << "Clearing hardware device selection";
        _currentIndex = -1;
        _lastSelectedDeviceName.clear();
        
        Q_EMIT currentIndexChanged();
        Q_EMIT currentNameChanged();
        Q_EMIT currentArchitectureChanged();
        return;
    }

    const HardwareDevice &device = _hwDevices.at(index);

    // Only clear image source if the device actually changed (not just re-selecting same device)
    bool deviceChanged = (_lastSelectedDeviceName != device.name);
    
    _imageWriter.setHWFilterList(device.tags, device.isInclusive());
    _imageWriter.setHWCapabilitiesList(device.capabilities);
    
    if (deviceChanged) {
        qDebug() << "Hardware device changed from" << _lastSelectedDeviceName << "to" << device.name << "- clearing image selection";
        _imageWriter.setSrc({});
        _lastSelectedDeviceName = device.name;
    } else {
        qDebug() << "Hardware device re-selected (" << device.name << ") - preserving image selection";
    }

    _currentIndex = index;

    Q_EMIT currentIndexChanged();
    Q_EMIT currentNameChanged();
    Q_EMIT currentArchitectureChanged();
}

int HWListModel::currentIndex() const {
    return _currentIndex;
}

void HWListModel::setConnectedRpibootChips(const QStringList &chips) {
    if (_connectedRpibootChips == chips)
        return;
    _connectedRpibootChips = chips;
    if (!_hwDevices.isEmpty())
        emit dataChanged(index(0), index(_hwDevices.size() - 1), {IsUsbBootConnectedRole});
}

bool HWListModel::tagsMatchChip(const QJsonArray &tags, const QString &chipName) {
    QStringList prefixes;
    if (chipName == QLatin1String("BCM2712"))
        prefixes = {QStringLiteral("pi5-")};
    else if (chipName == QLatin1String("BCM2711"))
        prefixes = {QStringLiteral("pi4-")};
    else if (chipName.startsWith(QLatin1String("BCM2836")) || chipName == QLatin1String("BCM2836/7"))
        prefixes = {QStringLiteral("pi2-"), QStringLiteral("pi3-")};
    else if (chipName == QLatin1String("BCM2835"))
        prefixes = {QStringLiteral("pi1-")};
    else
        return false;

    for (const auto &tag : tags) {
        const QString t = tag.toString();
        for (const auto &prefix : prefixes) {
            if (t.startsWith(prefix))
                return true;
        }
    }
    return false;
}
