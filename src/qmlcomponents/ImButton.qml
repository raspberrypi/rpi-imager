/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2

import RpiImager

Button {
    font.family: Style.fontFamily
    font.capitalization: Font.AllUppercase

    background: Rectangle {
        color: parent.enabled ? (parent.activeFocus ? Style.buttonFocusedBackgroundColor : (parent.hovered ? Style.buttonHoveredBackgroundColor : Style.buttonBackgroundColor)) : Qt.rgba(0, 0, 0, 0.1)
        radius: 4
    }

    contentItem: Text {
        text: parent.text
        font: parent.font
        color: parent.enabled ? Style.buttonForegroundColor : Qt.rgba(0, 0, 0, 0.3)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    activeFocusOnTab: true
    Accessible.onPressAction: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
