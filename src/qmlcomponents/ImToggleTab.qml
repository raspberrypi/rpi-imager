/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2025 Raspberry Pi Ltd
 */
import QtQuick 2.15
import QtQuick.Controls 2.15

import RpiImager

Button {
    id: control

    // Externally drive which tab is "on"
    property bool active: false
    
    // Allow instances to provide a custom accessibility description
    property string accessibleDescription: ""

    font.family: Style.fontFamily
    font.capitalization: Font.AllUppercase

    // Background matches ImButton when inactive, ImButtonRed when active
    background: Rectangle {
        color: control.enabled
              ? (control.active
                   ? (control.activeFocus
                        ? Style.button2HoveredBackgroundColor
                        : (control.hovered ? Style.button2HoveredBackgroundColor : Style.button2BackgroundColor))
                   : (control.activeFocus
                        ? Style.buttonFocusedBackgroundColor
                        : (control.hovered ? Style.buttonHoveredBackgroundColor : Style.buttonBackgroundColor)))
              : Qt.rgba(0, 0, 0, 0.1)
        radius: 4
        border.color: (control.enabled && !control.active) ? Style.popupBorderColor : "transparent"
        border.width: control.active ? 0 : 1
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled
              ? (control.active
                   ? ((control.activeFocus || control.hovered) ? Style.raspberryRed : Style.button2ForegroundColor)
                   : Style.buttonForegroundColor)
              : Qt.rgba(0, 0, 0, 0.3)
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
        } else if (active && desc !== "") {
            return name + ", " + desc + " (currently selected)"
        } else if (active) {
            return name + " (currently selected)"
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
