/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2020 Raspberry Pi Ltd
 */

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

    beginResetModel();
    _currentIndex = -1;
    // Replace contents on reload to avoid duplicate entries when re-entering the step
    _hwDevices.clear();

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
        _hwDevices.append(hwDevice);

        if (deviceObj["default"].isBool() && deviceObj["default"].toBool())
            indexOfDefault = _hwDevices.size() - 1;
    }

    endResetModel();

    setCurrentIndex(indexOfDefault);

    return true;
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
        {ArchitectureRole, "architecture"}
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
