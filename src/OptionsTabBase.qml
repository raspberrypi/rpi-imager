/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.15

import RpiImager

ScrollView {
    id: root

    property double scrollPosition

    font.family: Style.fontFamily
    Layout.fillWidth: true
    Layout.fillHeight: true

    // clip: true
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    ScrollBar.vertical.position: scrollPosition
    scrollPosition: scrollPosition
}
