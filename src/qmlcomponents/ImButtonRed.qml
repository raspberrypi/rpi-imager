/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import RpiImager

Button {
    id: control
    font.family: Style.fontFamily
    font.pointSize: Style.fontSizeSm
    font.capitalization: Font.AllUppercase
    
    // Allow instances to provide a custom accessibility description
    property string accessibleDescription: ""
    
    background: Rectangle {
        color: control.enabled
               ? (control.activeFocus
                   ? Style.button2HoveredBackgroundColor
                   : (control.hovered ? Style.button2HoveredBackgroundColor : Style.button2BackgroundColor))
               : Qt.rgba(0, 0, 0, 0.1)
        radius: Style.cornerRadius(4)
        antialiasing: true  // Smooth edges at non-integer scale factors
        clip: true  // Prevent content overflow at non-integer scale factors
    }

    // Size to fit content, with minimum width for short labels
    implicitWidth: Math.max(Style.buttonWidthMinimum, implicitContentWidth + leftPadding + rightPadding)

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled
               ? (control.activeFocus || control.hovered ? Style.raspberryRed : Style.button2ForegroundColor)
               : Qt.rgba(0, 0, 0, 0.3)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight  // Truncate if layout constrains button below content width
    }

    activeFocusOnTab: true
    focusPolicy: Qt.TabFocus
    
    // Accessibility properties
    Accessible.role: Accessible.Button
    Accessible.name: CommonStrings.controlAccessibleName(text, accessibleDescription, enabled)
    Accessible.description: ""
    Accessible.onPressAction: clicked()
    
    Keys.onEnterPressed: clicked()
    Keys.onReturnPressed: clicked()
}
