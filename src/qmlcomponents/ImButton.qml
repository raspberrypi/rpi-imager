/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls

import RpiImager

Button {
    id: control
    font.family: Style.fontFamily
    font.capitalization: Font.AllUppercase
    
    // Allow instances to provide a custom accessibility description
    property string accessibleDescription: ""

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
    
    // Accessibility properties
    Accessible.role: Accessible.Button
    Accessible.name: {
        // Combine text with description in name since VoiceOver reads name more reliably
        var name = text
        var desc = accessibleDescription
        if (!enabled && desc !== "") {
            return name + ", " + desc + " (disabled)"
        } else if (!enabled) {
            return name + " (disabled)"
        } else if (desc !== "") {
            return name + ", " + desc
        } else {
            return name
        }
    }
    Accessible.description: ""
    Accessible.onPressAction: clicked()
    
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
