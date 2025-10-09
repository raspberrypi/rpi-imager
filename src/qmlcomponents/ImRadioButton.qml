/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022-2025 Raspberry Pi Ltd
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

RadioButton {
    Material.accent: Style.formControlActiveColor
    activeFocusOnTab: true
    
    // Add visual focus indicator
    Rectangle {
        anchors.fill: parent
        anchors.margins: Style.focusOutlineMargin
        color: Style.transparent
        border.color: parent.activeFocus ? Style.focusOutlineColor : Style.transparent
        border.width: Style.focusOutlineWidth
        radius: Style.focusOutlineRadius
        z: -1
    }
    
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space) {
            if (!checked)            // prevent unchecking the current one
                click()              // goes through the normal “mouse click” path
            event.accepted = true
        }
    }
    Keys.onEnterPressed: (event) => {
        if (!checked)
            click()
        event.accepted = true
    }

    Keys.onReturnPressed: (event) => {
        if (!checked)
          click()
        event.accepted = true
    }
}
