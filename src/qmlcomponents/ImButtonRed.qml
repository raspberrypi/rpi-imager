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
    
    // Access imageWriter from parent context
    property var imageWriter: {
        var item = parent;
        while (item) {
            if (item.imageWriter !== undefined) {
                return item.imageWriter;
            }
            item = item.parent;
        }
        return null;
    }

    background: Rectangle {
        color: control.enabled
               ? (control.activeFocus
                   ? Style.button2HoveredBackgroundColor
                   : (control.hovered ? Style.button2HoveredBackgroundColor : Style.button2BackgroundColor))
               : Qt.rgba(0, 0, 0, 0.1)
        radius: (control.imageWriter && control.imageWriter.isEmbeddedMode()) ? Style.buttonBorderRadiusEmbedded : 4
        antialiasing: true  // Smooth edges at non-integer scale factors
        clip: true  // Prevent content overflow at non-integer scale factors
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled
               ? (control.activeFocus || control.hovered ? Style.raspberryRed : Style.button2ForegroundColor)
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
