/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2

import RpiImager

Button {
    id: control
    font.family: Style.fontFamily
    font.capitalization: Font.AllUppercase

    background: Rectangle {
        color: control.enabled ? (control.activeFocus ? Style.buttonFocusedBackgroundColor : (control.hovered ? Style.buttonHoveredBackgroundColor : Style.buttonBackgroundColor)) : Qt.rgba(0, 0, 0, 0.1)
        radius: 4
        border.color: control.enabled ? Style.popupBorderColor : Qt.rgba(0, 0, 0, 0.2)
        border.width: 1
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Style.buttonForegroundColor : Qt.rgba(0, 0, 0, 0.3)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    activeFocusOnTab: true
    Accessible.onPressAction: clicked()
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
