/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2

RadioButton {
    activeFocusOnTab: true
    
    // Add visual focus indicator
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        color: "transparent"
        border.color: parent.activeFocus ? "#0078d4" : "transparent"
        border.width: 2
        radius: 4
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
