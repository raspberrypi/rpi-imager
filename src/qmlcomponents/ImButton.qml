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
        color: control.enabled ? (control.activeFocus ? Style.buttonFocusedBackgroundColor : (control.hovered ? Style.buttonHoveredBackgroundColor : Style.buttonBackgroundColor)) : Qt.rgba(0, 0, 0, 0.1)
        radius: (control.imageWriter && control.imageWriter.isEmbeddedMode()) ? Style.buttonBorderRadiusEmbedded : 4
        border.color: control.enabled ? Style.popupBorderColor : Qt.rgba(0, 0, 0, 0.2)
        border.width: 1
        antialiasing: true  // Smooth edges at non-integer scale factors
        clip: true  // Prevent content overflow at non-integer scale factors
    }

    // Override implicit width to prevent button from growing based on text length
    implicitWidth: Style.buttonWidthMinimum
    
    contentItem: Text {
        text: control.text
        font.family: control.font.family
        font.capitalization: control.font.capitalization
        font.pixelSize: control.font.pixelSize
        minimumPixelSize: Math.round(control.font.pixelSize * 0.7)  // Don't shrink below 70%
        fontSizeMode: Text.HorizontalFit
        color: control.enabled ? Style.buttonForegroundColor : Qt.rgba(0, 0, 0, 0.3)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight  // Fallback if still too long at minimum size
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
