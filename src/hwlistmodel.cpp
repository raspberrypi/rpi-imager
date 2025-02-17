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
#include <qtcoreexports.h>

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

    const QJsonArray deviceArray = devices.toArray();
    _hwDevices.reserve(deviceArray.size());
    int indexOfDefault = -1;
    for (const QJsonValue &deviceValue: deviceArray) {
        QJsonObject deviceObj = deviceValue.toObject();

        HardwareDevice hwDevice = {
            deviceObj["name"].toString(),
            deviceObj["tags"].toArray(),
            deviceObj["icon"].toString(),
            deviceObj["description"].toString(),
            deviceObj["matching_type"].toString()
        };
        _hwDevices.append(hwDevice);

        if (deviceObj["default"].isBool() && deviceObj["default"].toBool())
            indexOfDefault = _hwDevices.size() - 1;
    }

    endResetModel();

    setCurrentIndex(indexOfDefault);

    return true;
}

void HWListModel::setSelectedIndex(int index)
{
    if (index < 0 || index >= _hwDevices.size()) {
        qWarning() << Q_FUNC_INFO << "Invalid index" << index;
        return;
    }

    const HardwareDevice &device = _hwDevices.at(index);

    _imageWriter.setHWFilterList(device.tags, device.isInclusive());

    setCurrentIndex(index);
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
        {IconRole, "icon"},
        {DescriptionRole, "description"},
        {MatchingTypeRole, "matching_type"}
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
    case IconRole:
        return device.icon;
    case DescriptionRole:
        return device.description;
    case MatchingTypeRole:
        return device.matchingType;
    }

    return {};
}

QString HWListModel::currentName() const {
    if (_currentIndex < 0 || _currentIndex >= _hwDevices.size())
        return tr("CHOOSE DEVICE");

    HardwareDevice device = _hwDevices[_currentIndex];
    return device.name;
}

void HWListModel::setCurrentIndex(int index) {
    if (_currentIndex == index)
        return;
    _currentIndex = index;

    Q_EMIT currentIndexChanged();
    Q_EMIT currentNameChanged();
}

int HWListModel::currentIndex() const {
    return _currentIndex;
}
