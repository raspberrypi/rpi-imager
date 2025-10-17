/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022 Raspberry Pi Ltd
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import RpiImager

CheckBox {
    id: control
    Material.accent: Style.formControlActiveColor
    activeFocusOnTab: true
    
    // Accessibility properties
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.onToggleAction: toggle()

    Keys.onEnterPressed: toggle()
    Keys.onReturnPressed: toggle()
    Keys.onSpacePressed: toggle()
    
    Rectangle {
        // This rectangle serves as a high-contrast underline for focus
        anchors.left: control.contentItem.left
        anchors.right: control.contentItem.right
        anchors.top: control.contentItem.bottom
        anchors.topMargin: 2
        height: 2
        color: Style.button2FocusedBackgroundColor
        visible: control.activeFocus
    }
}
