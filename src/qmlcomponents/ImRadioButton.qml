/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

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
            toggle()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: toggle()
    Keys.onReturnPressed: toggle()
}
